// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MotionBlurUtil.h"

#include <kodachi/attribute/AttributeUtils.h>

#include <scene_rdl2/scene/rdl2/Types.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>

#include <tbb/parallel_for.h>

namespace {

// each chunk of TBB work should require at least 100,000 clock cycles
// so set a high grain size since we are doing simple equations on
// potentially many values
constexpr size_t kTbbGrainSize = 50000;

bool
isSame(float a, float b)
{
    return std::fabs(a - b) < std::numeric_limits<float>::epsilon();
}

bool
contains(kodachi::array_view<float> sampleTimes, float sampleTime)
{
    return std::any_of(sampleTimes.begin(), sampleTimes.end(),
                [=](float t) { return isSame(t, sampleTime); });
}

arras::rdl2::Vec3fVector
toVec3fVector(kodachi::SampleAccessor<float>::const_reference sample)
{
    arras::rdl2::Vec3fVector vec(sample.size() / 3);

    std::memcpy(vec.data(), sample.data(), sample.size() * sizeof(float));

    return vec;
}

void
Vec3fVectorDeleter(void *context)
{
    std::default_delete<arras::rdl2::Vec3fVector>()(reinterpret_cast<arras::rdl2::Vec3fVector*>(context));
}

kodachi::FloatAttribute
toFloatAttr(arras::rdl2::Vec3fVector v)
{
    std::unique_ptr<arras::rdl2::Vec3fVector> temp(new arras::rdl2::Vec3fVector(std::move(v)));

    float* data = reinterpret_cast<float*>(temp->data());

    kodachi::FloatAttribute floatAttr(data,
                                      temp->size() * 3,
                                      3,
                                      temp.get(),
                                      Vec3fVectorDeleter);

    temp.release();

    return floatAttr;
}

kodachi::FloatAttribute
createSingleSampleAttr(const kodachi::FloatAttribute& attr)
{
    if (attr.getNumberOfTimeSamples() < 2) {
        return attr;
    }

    const auto sample = attr.getNearestSample(0.f);

    return kodachi::FloatAttribute(sample.data(), sample.size(), attr.getTupleSize());
}

struct HermiteInterpolationResult
{
    arras::rdl2::Vec3fVector pShutterOpen;
    arras::rdl2::Vec3fVector pShutterClose;
    arras::rdl2::Vec3fVector vShutterOpen;
    arras::rdl2::Vec3fVector vShutterClose;
};

arras::rdl2::Vec3fVector
interpolatePositionHermite(float t, size_t numPoints,
                           kodachi::SampleAccessor<float>::const_reference p0Sample,
                           kodachi::SampleAccessor<float>::const_reference p1Sample,
                           kodachi::SampleAccessor<float>::const_reference v0Sample,
                           kodachi::SampleAccessor<float>::const_reference v1Sample,
                           float fps)
{
    arras::rdl2::Vec3fVector positions(numPoints);

    // p(t) = (1-t)^2 (1+2t) p0 + t(1-t)^2 v0 - t^2 (1-t) v1 + t^2 (3-2t) p1
    const float a = ((1 - t) * (1 - t)) * (1 + 2 * t);
    const float b = t * ((1 - t) * (1 - t));
    const float c = (t * t) * (1 - t);
    const float d = (t * t) * (3 - 2 * t);

    tbb::parallel_for(tbb::blocked_range<size_t>(0, numPoints, kTbbGrainSize),
    [&](const tbb::blocked_range<size_t>& r)
    {
        for (size_t i = r.begin(); i < r.end(); ++i) {
            const size_t sampleIndex = i * 3;

            const arras::rdl2::Vec3f p0(p0Sample.data() + sampleIndex);
            const arras::rdl2::Vec3f p1(p1Sample.data() + sampleIndex);
            arras::rdl2::Vec3f v0(v0Sample.data() + sampleIndex);
            arras::rdl2::Vec3f v1(v1Sample.data() + sampleIndex);

            // convert velocities from units/second to units/frame
            v0 /= fps;
            v1 /= fps;

            positions[i] = (a * p0) + (b * v0) - (c * v1) + (d * p1);
        }
    });

    return positions;
}

arras::rdl2::Vec3fVector
interpolateVelocityHermite(float t, size_t numPoints,
                           kodachi::SampleAccessor<float>::const_reference p0Sample,
                           kodachi::SampleAccessor<float>::const_reference p1Sample,
                           kodachi::SampleAccessor<float>::const_reference v0Sample,
                           kodachi::SampleAccessor<float>::const_reference v1Sample,
                           float fps)
{
    arras::rdl2::Vec3fVector velocities(numPoints);

    // v(t) = 6t(1-t) (p1-p0) + (1-t)(1-3t) v0 + t(3t-2) v1
    const float a = (6 * t) * (1 - t);
    const float b = (1 - t) * (1 - 3 * t);
    const float c = t * (3 * t - 2);

    tbb::parallel_for(tbb::blocked_range<size_t>(0, numPoints, kTbbGrainSize),
    [&](const tbb::blocked_range<size_t>& r)
    {
        for (size_t i = r.begin(); i < r.end(); ++i) {
            const size_t sampleIndex = i * 3;

            const arras::rdl2::Vec3f p0(p0Sample.data() + sampleIndex);
            const arras::rdl2::Vec3f p1(p1Sample.data() + sampleIndex);
            arras::rdl2::Vec3f v0(v0Sample.data() + sampleIndex);
            arras::rdl2::Vec3f v1(v1Sample.data() + sampleIndex);

            // convert velocities from units/second to units/frame for the
            // interpolation, and then convert the resulting velocity back into
            // units/second
            v0 /= fps;
            v1 /= fps;

            velocities[i] = fps * ((a * (p1 - p0)) + (b * v0) + (c * v1));
        }
    });

    return velocities;
}

HermiteInterpolationResult
interpolateHermite(const kodachi::FloatAttribute& positionAttr,
                   const kodachi::FloatAttribute& velocityAttr,
                   float shutterOpen, float shutterClose, float fps)
{
    // There is no need to assume the the bounding sample times will be the
    // same for shutterOpen and shutterClose.
    // Additionally, if a shutter time falls on a sample time, we have no need
    // to interpolate.
    float lShutterOpen = 0.f;
    float rShutterOpen = 0.f;
    float lShutterClose = 0.f;
    float rShutterClose = 0.f;

    positionAttr.getBoundingSampleTimes(&lShutterOpen, &rShutterOpen, shutterOpen);
    positionAttr.getBoundingSampleTimes(&lShutterClose, &rShutterClose, shutterClose);

    const auto positionSamples = positionAttr.getSamples();
    const auto velocitySamples = velocityAttr.getSamples();

    const auto numPoints = positionSamples.getNumberOfValues() / 3;

    HermiteInterpolationResult result;

    // shutterOpen
    {
        const auto& pShutterOpenSample0 = positionSamples.getNearestSample(lShutterOpen);
        const auto& vShutterOpenSample0 = velocitySamples.getNearestSample(lShutterOpen);

        if (isSame(lShutterOpen, rShutterOpen)) {
            result.pShutterOpen = toVec3fVector(pShutterOpenSample0);
            result.vShutterOpen = toVec3fVector(vShutterOpenSample0);
        } else {
            const auto& pShutterOpenSample1 = positionSamples.getNearestSample(rShutterOpen);
            const auto& vShutterOpenSample1 = velocitySamples.getNearestSample(rShutterOpen);

            // Since, in general, we will not have [t0,t1] = [0,1], we remap t linearly to a parameter u whose value
            // ranges over [0,1], i.e. u=0 when t=t0, and u=1 when t=t1:
            //
            //   u = (t-t0) / (t1-t0)
            const float uShutterOpen =
                    (shutterOpen - lShutterOpen) / (rShutterOpen - lShutterOpen);

            result.pShutterOpen = interpolatePositionHermite(
                    uShutterOpen, numPoints,
                    pShutterOpenSample0, pShutterOpenSample1,
                    vShutterOpenSample0, vShutterOpenSample1,
                    fps);

            result.vShutterOpen = interpolateVelocityHermite(
                    uShutterOpen, numPoints,
                    pShutterOpenSample0, pShutterOpenSample1,
                    vShutterOpenSample0, vShutterOpenSample1,
                    fps);
        }
    }

    // shutterClose
    {
        const auto& pShutterCloseSample0 = positionSamples.getNearestSample(lShutterClose);
        const auto& vShutterCloseSample0 = velocitySamples.getNearestSample(lShutterClose);

        if (isSame(lShutterClose, rShutterClose)) {
            result.pShutterClose = toVec3fVector(pShutterCloseSample0);
            result.vShutterClose = toVec3fVector(vShutterCloseSample0);
        } else {
            const auto& pShutterCloseSample1 = positionSamples.getNearestSample(rShutterClose);
            const auto& vShutterCloseSample1 = velocitySamples.getNearestSample(rShutterClose);

            const float uShutterClose =
                    (shutterClose - lShutterClose) / (rShutterClose - lShutterClose);

            result.pShutterClose = interpolatePositionHermite(
                    uShutterClose, numPoints,
                    pShutterCloseSample0, pShutterCloseSample1,
                    vShutterCloseSample0, vShutterCloseSample1,
                    fps);

            result.vShutterClose = interpolateVelocityHermite(
                    uShutterClose, numPoints,
                    pShutterCloseSample0, pShutterCloseSample1,
                    vShutterCloseSample0, vShutterCloseSample1,
                    fps);
        }
    }

    return result;
}

arras::rdl2::MotionBlurType
getMotionBlurType(const kodachi::Attribute& motionBlurTypeAttr)
{
    if (motionBlurTypeAttr.isValid()) {
        const auto attrType = motionBlurTypeAttr.getType();
        if (attrType == kodachi::kAttrTypeInt) {
            return static_cast<arras::rdl2::MotionBlurType>(
                    kodachi::IntAttribute(motionBlurTypeAttr).getValue());
        } else if (attrType == kodachi::kAttrTypeString) {
            static const std::unordered_map<kodachi::StringAttribute,
                arras::rdl2::MotionBlurType, kodachi::AttributeHash> kMotionBlurMap
            {
                { "static",       arras::rdl2::MotionBlurType::STATIC },
                { "velocity",     arras::rdl2::MotionBlurType::VELOCITY },
                { "frame delta",  arras::rdl2::MotionBlurType::FRAME_DELTA },
                { "acceleration", arras::rdl2::MotionBlurType::ACCELERATION },
                { "hermite",      arras::rdl2::MotionBlurType::HERMITE },
                { "best",         arras::rdl2::MotionBlurType::BEST }
            };

            const auto iter = kMotionBlurMap.find(motionBlurTypeAttr);

            if (iter != kMotionBlurMap.end()) {
                return iter->second;
            }
        }
    }

    return arras::rdl2::MotionBlurType::BEST;
}

kodachi::StringAttribute
getMotionBlurTypeAttr(arras::rdl2::MotionBlurType mtb)
{
    static const kodachi::StringAttribute kStaticAttr("static");
    static const kodachi::StringAttribute kVelocityAttr("velocity");
    static const kodachi::StringAttribute kFrameDeltaAttr("frame delta");
    static const kodachi::StringAttribute kAccelerationAttr("acceleration");
    static const kodachi::StringAttribute kHermiteAttr("hermite");

    switch (mtb) {
    case arras::rdl2::MotionBlurType::VELOCITY: return kVelocityAttr;
    case arras::rdl2::MotionBlurType::FRAME_DELTA: return kFrameDeltaAttr;
    case arras::rdl2::MotionBlurType::ACCELERATION: return kAccelerationAttr;
    case arras::rdl2::MotionBlurType::HERMITE: return kHermiteAttr;
    }

    return kStaticAttr;
}

// returns true if there is a position and velocity
// with samples at time 0
bool
validateVelocity(const kodachi::FloatAttribute& positionAttr,
                 const kodachi::FloatAttribute& velocityAttr)
{
    const auto positionSamples = positionAttr.getSamples();
    const auto velocitySamples = velocityAttr.getSamples();

    if (!velocitySamples.isValid()) {
        return false;
    }

    if (velocitySamples.getNumberOfValues() != positionSamples.getNumberOfValues()) {
        return false;
    }

    const auto positionSampleTimes = positionSamples.getSampleTimes();
    const auto velocitySampleTimes = velocitySamples.getSampleTimes();

    return contains(velocitySampleTimes, 0.0f) &&
            contains(positionSampleTimes, 0.0f);
}

// returns true if there are bracketing position samples
bool
validateFrameDelta(const kodachi::FloatAttribute& positionAttr, float shutterOpen, float shutterClose)
{
    const auto positionSamples = positionAttr.getSamples();

    if (positionSamples.getNumberOfTimeSamples() < 2) {
        return false;
    }

    const auto sampleTimes = positionSamples.getSampleTimes();

    // require that there be bracketing position sample times
    return std::any_of(sampleTimes.begin(), sampleTimes.end(), [=](float t) { return t >= shutterOpen; })
    && std::any_of(sampleTimes.begin(), sampleTimes.end(), [=](float t) { return t <= shutterClose; });
}

// returns true if there is a position, velocity, and acceleration with
// samples at time 0
bool
validateAcceleration(const kodachi::FloatAttribute& positionAttr,
                     const kodachi::FloatAttribute& velocityAttr,
                     const kodachi::FloatAttribute& accelerationAttr)
{
    const auto positionSamples = positionAttr.getSamples();
    const auto velocitySamples = velocityAttr.getSamples();
    const auto accelerationSamples = accelerationAttr.getSamples();

    if (!velocitySamples.isValid()) {
        return false;
    }

    if (velocitySamples.getNumberOfValues() != positionSamples.getNumberOfValues()) {
        return false;
    }

    if (!accelerationSamples.isValid()) {
        return false;
    }

    if (accelerationSamples.getNumberOfValues() != positionSamples.getNumberOfValues()) {
        return false;
    }

    const auto positionSampleTimes = positionSamples.getSampleTimes();
    const auto velocitySampleTimes = velocitySamples.getSampleTimes();
    const auto accelerationSampleTimes = accelerationSamples.getSampleTimes();

    return contains(positionSampleTimes, 0.0f) &&
            contains(velocitySampleTimes, 0.0f) &&
            contains(accelerationSampleTimes, 0.0f);
}

// returns true if there are bracketing position samples and matching
// velocity samples
bool
validateHermite(const kodachi::FloatAttribute& positionAttr,
                const kodachi::FloatAttribute& velocityAttr,
                float shutterOpen, float shutterClose)
{
    const auto velocitySamples = velocityAttr.getSamples();

    if (velocitySamples.getNumberOfTimeSamples() < 2) {
        return false;
    }

    if (velocityAttr.getNumberOfValues() != positionAttr.getNumberOfValues()) {
        return false;
    }

    // do we have bracketing position samples
    float lShutterOpen = 0.f;
    float rShutterOpen = 0.f;
    float lShutterClose = 0.f;
    float rShutterClose = 0.f;

    if (!positionAttr.getBoundingSampleTimes(&lShutterOpen, &rShutterOpen, shutterOpen)) {
        return false;
    }

    if (!positionAttr.getBoundingSampleTimes(&lShutterClose, &rShutterClose, shutterClose)) {
        return false;
    }

    const auto velocitySampleTimes = velocitySamples.getSampleTimes();

    return contains(velocitySampleTimes, lShutterOpen)
            && contains(velocitySampleTimes, rShutterOpen)
            && contains(velocitySampleTimes, lShutterClose)
            && contains(velocitySampleTimes, rShutterClose);
}

arras::rdl2::MotionBlurType
getValidatedMotionBlurType(arras::rdl2::MotionBlurType requestedMotionBlurType,
                           const kodachi::FloatAttribute& positionAttr,
                           const kodachi::FloatAttribute& velocityAttr,
                           const kodachi::FloatAttribute& accelerationAttr,
                           float shutterOpen, float shutterClose)
{
    switch (requestedMotionBlurType) {
    case arras::rdl2::MotionBlurType::VELOCITY:
    {
        if (validateVelocity(positionAttr, velocityAttr)){
            return arras::rdl2::MotionBlurType::VELOCITY;
        }
        break;
    }
    case arras::rdl2::MotionBlurType::FRAME_DELTA:
    {
        if (validateFrameDelta(positionAttr, shutterOpen, shutterClose)) {
            return arras::rdl2::MotionBlurType::FRAME_DELTA;
        }
        break;
    }
    case arras::rdl2::MotionBlurType::ACCELERATION:
    {
        if (validateAcceleration(positionAttr, velocityAttr, accelerationAttr)){
            return arras::rdl2::MotionBlurType::ACCELERATION;
        }
        break;
    }
    case arras::rdl2::MotionBlurType::HERMITE:
    {
        if (validateHermite(positionAttr, velocityAttr, shutterOpen, shutterClose)) {
            return arras::rdl2::MotionBlurType::HERMITE;
        }
        break;
    }
    case arras::rdl2::MotionBlurType::BEST:
    {
        if (validateHermite(positionAttr, velocityAttr, shutterOpen, shutterClose)) {
            return arras::rdl2::MotionBlurType::HERMITE;
        }
        if (validateAcceleration(positionAttr, velocityAttr, accelerationAttr)){
            return arras::rdl2::MotionBlurType::ACCELERATION;
        }
        if (validateFrameDelta(positionAttr, shutterOpen, shutterClose)) {
            return arras::rdl2::MotionBlurType::FRAME_DELTA;
        }
        if (validateVelocity(positionAttr, velocityAttr)){
            return arras::rdl2::MotionBlurType::VELOCITY;
        }

        break;
    }
    }

    return arras::rdl2::MotionBlurType::STATIC;
}

kodachi::FloatAttribute
getAccelerationAsFloatAttribute(const kodachi::Attribute& accelerationAttr)
{
    if (accelerationAttr.isValid()) {
        const auto attrType = accelerationAttr.getType();
        if (attrType == kodachi::kAttrTypeGroup) {
            const kodachi::GroupAttribute accelerationArbAttr(accelerationAttr);
            const kodachi::FloatAttribute accelerationFloatAttr =
                    accelerationArbAttr.getChildByName("value");

            if (!accelerationFloatAttr.isValid()) {
                const kodachi::IntAttribute indexAttr =
                        accelerationArbAttr.getChildByName("index");
                kodachi::FloatAttribute indexedValueAttr =
                        accelerationArbAttr.getChildByName("indexedValue");
                indexedValueAttr = kodachi::interpolateAttr(
                        indexedValueAttr, 0);

                if (indexAttr.isValid() && indexedValueAttr.isValid()) {
                    return kodachi::unpackIndexedValue(indexAttr, indexedValueAttr, 3);
                }
            } else {
                return accelerationFloatAttr;
            }
        } else if (attrType == kodachi::kAttrTypeFloat) {
            return accelerationAttr;
        }
    }

    return kodachi::FloatAttribute{};
}

} // anonymous namespace

namespace kodachi_moonray {
namespace motion_blur_util {

kodachi::GroupAttribute
createMotionBlurAttributes(const kodachi::Attribute& motionBlurTypeAttr,
                           const kodachi::FloatAttribute& positionAttr,
                           const kodachi::FloatAttribute& velocityAttr,
                           const kodachi::Attribute& accelerationAttr,
                           float shutterOpen, float shutterClose, float fps)
{
    const int64_t numPositionValues = positionAttr.getNumberOfValues();

    if (numPositionValues == 0) {
        return kodachi::GroupAttribute("errorMessage",
                kodachi::StringAttribute("createMotionBlurAttributes: positionAttr has no values"), false);
    }

    if (numPositionValues % 3 != 0) {
        return kodachi::GroupAttribute("errorMessage",
                kodachi::StringAttribute("createMotionBlurAttributes: positionAttr does not contain a valid number of values"), false);
    }

    const kodachi::FloatAttribute accelerationFloatAttr =
            getAccelerationAsFloatAttribute(accelerationAttr);

    const arras::rdl2::MotionBlurType requestedMotionBlurType =
            getMotionBlurType(motionBlurTypeAttr);

    const arras::rdl2::MotionBlurType motionBlurType =
            getValidatedMotionBlurType(requestedMotionBlurType,
                                       positionAttr,
                                       velocityAttr,
                                       accelerationFloatAttr,
                                       shutterOpen, shutterClose);

    kodachi::GroupBuilder gb;

    if (requestedMotionBlurType != arras::rdl2::MotionBlurType::BEST
            && requestedMotionBlurType != motionBlurType) {
        const auto requestedTypeAttr = getMotionBlurTypeAttr(requestedMotionBlurType);
        const auto typeAttr = getMotionBlurTypeAttr(motionBlurType);

        std::stringstream ss;
        ss << "createMotionBlurAttributes: Insufficient data for requested motion blur type '"
                << requestedTypeAttr.getValueCStr()
                << "', falling back to '" << typeAttr.getValueCStr() << "'";
        gb.set("warningMessage", kodachi::StringAttribute(ss.str()));
    }

    gb.set("motionBlurType", getMotionBlurTypeAttr(motionBlurType));

    switch (motionBlurType) {
    case arras::rdl2::MotionBlurType::STATIC:
    {
        gb.set("attrs.vertex_list_0", kodachi::interpolateAttr(positionAttr, 0.f, 3));
        break;
    }
    case arras::rdl2::MotionBlurType::VELOCITY:
    {
        gb.set("attrs.vertex_list_0", createSingleSampleAttr(positionAttr));
        gb.set("attrs.velocity_list_0", createSingleSampleAttr(velocityAttr));
        break;
    }
    case arras::rdl2::MotionBlurType::FRAME_DELTA:
    {
        gb.set("attrs.vertex_list_0", kodachi::interpolateAttr(positionAttr, shutterOpen, 3));
        gb.set("attrs.vertex_list_1", kodachi::interpolateAttr(positionAttr, shutterClose, 3));
        break;
    }
    case arras::rdl2::MotionBlurType::ACCELERATION:
    {
        gb.set("attrs.vertex_list_0", createSingleSampleAttr(positionAttr));
        gb.set("attrs.velocity_list_0", createSingleSampleAttr(velocityAttr));
        gb.set("attrs.acceleration_list", createSingleSampleAttr(accelerationFloatAttr));
        break;
    }
    case arras::rdl2::MotionBlurType::HERMITE:
    {
        HermiteInterpolationResult result =
                interpolateHermite(positionAttr, velocityAttr, shutterOpen, shutterClose, fps);

        gb.set("attrs.vertex_list_0", toFloatAttr(std::move(result.pShutterOpen)))
          .set("attrs.vertex_list_1", toFloatAttr(std::move(result.pShutterClose)))
          .set("attrs.velocity_list_0", toFloatAttr(std::move(result.vShutterOpen)))
          .set("attrs.velocity_list_1", toFloatAttr(std::move(result.vShutterClose)));

        break;
    }
    }

    return gb.build();
}

kodachi::GroupAttribute
createStaticMotionBlurAttributes(const kodachi::FloatAttribute& positionAttr)
{
    const int64_t numPositionValues = positionAttr.getNumberOfValues();

    if (numPositionValues == 0) {
        return kodachi::GroupAttribute("errorMessage",
                kodachi::StringAttribute("createStaticMotionBlurAttributes: positionAttr has no values"), false);
    }

    if (numPositionValues % 3 != 0) {
        return kodachi::GroupAttribute("errorMessage",
                kodachi::StringAttribute("createStaticMotionBlurAttributes: positionAttr does not contain a valid number of values"), false);
    }

    kodachi::GroupBuilder gb;
    gb.set("motionBlurType", getMotionBlurTypeAttr(arras::rdl2::MotionBlurType::STATIC));
    gb.set("attrs.vertex_list_0", createSingleSampleAttr(positionAttr));

    return gb.build();
}

} // namespace motion_blur_util
} // namespace kodachi_moonray

