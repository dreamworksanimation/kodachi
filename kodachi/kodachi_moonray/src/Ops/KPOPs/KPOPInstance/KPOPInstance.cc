// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/StringView.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/XFormUtil.h>
#include <FnGeolib/util/Path.h>

#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathMatrixAlgo.h>
#include <OpenEXR/ImathQuat.h>

#include <unordered_set>

namespace {

KdLogSetup("KPOPInstance");

class KPOPInstanceSource : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface& interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface& interface)
    {
        // We want to convert 'instance source' locations and all their non-rdl2
        // descendants into GroupGeometry. Pass a flag as an OpArg to specify
        // whether there is a 'instance source' ancestor
        static const kodachi::StringAttribute kRdl2("rdl2");
        const kodachi::StringAttribute typeAttr = interface.getAttr("type");

        const bool isRdl2Location = typeAttr == kRdl2;
        const bool isInstanceSource = isRdl2Location && _isInstanceSource(interface);
        const bool isRdl2NonInstanceSourceLocation = isRdl2Location && !isInstanceSource;

        const kodachi::IntAttribute isInstanceSourceDescendantAttr =
                interface.getOpArg("isInstanceSourceDescendant");

        const bool isInstanceSourceDescendant = isInstanceSourceDescendantAttr.isValid();

        if (isInstanceSourceDescendant && isRdl2NonInstanceSourceLocation) {
            kodachi::GroupBuilder opArgsGb;
            opArgsGb.update(interface.getOpArg(""));
            opArgsGb.del("isInstanceSourceDescendant");
            interface.replaceChildTraversalOp("", opArgsGb.build());
        } else if (isInstanceSource && !isInstanceSourceDescendant) {
            kodachi::GroupBuilder opArgsGb;
            opArgsGb.update(interface.getOpArg(""));
            opArgsGb.set("isInstanceSourceDescendant", kodachi::IntAttribute(true));
            opArgsGb.set("shutterOpen", interface.getAttr("rdl2.meta.shutterOpen"));
            opArgsGb.set("shutterClose", interface.getAttr("rdl2.meta.shutterClose"));
            interface.replaceChildTraversalOp("", opArgsGb.build());
        }

        // SceneClass and SceneObject name
        if (isInstanceSource || (!isRdl2Location && isInstanceSourceDescendant)) {
            static const kodachi::StringAttribute kGroupGeometryAttr("GroupGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kGroupGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_GroupGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);

            interface.setAttr("rdl2.meta.isGroupGeometry", kodachi::IntAttribute(true));
            interface.setAttr("rdl2.meta.skipIDGeneration", kodachi::IntAttribute(false));
            if (!isInstanceSource) {
                // Purposefully leaving out 'isNode'. The leaf locations already
                // have their xform localized
                interface.setAttr("type", kRdl2);
                interface.setAttr("rdl2.meta.isGeometry", kodachi::IntAttribute(true));
                interface.setAttr("rdl2.meta.isLayerAssignable", kodachi::IntAttribute(true));
                interface.setAttr("rdl2.meta.kodachiType", typeAttr);
                interface.setAttr("rdl2.meta.shutterOpen", interface.getOpArg("shutterOpen"));
                interface.setAttr("rdl2.meta.shutterClose", interface.getOpArg("shutterClose"));

            }
        }
    }

private:
    static bool _isInstanceSource(kodachi::OpCookInterface& interface)
    {
        static const kodachi::StringAttribute kInstanceSource("instance source");

        const kodachi::StringAttribute kodachiType =
                interface.getAttr("rdl2.meta.kodachiType");

        return kodachiType == kInstanceSource;
    }
};

// Expand relative instanceSource paths to absolute ones. This is needed
// to support non-location-dependent kodachi_houdini_engine output.
kodachi::StringAttribute absPath(kodachi::OpCookInterface &interface,
                                 const kodachi::StringAttribute& sourceAttr)
{
    std::string location(interface.getInputLocationPath());
    auto&& relative(sourceAttr.getNearestSample(0));
    std::vector<std::string> absolute; absolute.reserve(relative.size());
    for (auto&& path : relative)
        absolute.push_back(FnKat::Util::Path::RelativeToAbsPath(location, path));
    return kodachi::StringAttribute(absolute);
}

class KPOPInstance: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and @rdl2.meta.kodachiType==\"instance\"}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::StringAttribute instanceSourceAttr =
                interface.getAttr("geometry.instanceSource");

        static const kodachi::StringAttribute kGroupGeometryAttr("GroupGeometry");
        interface.setAttr("rdl2.sceneObject.sceneClass", kGroupGeometryAttr, false);

        const std::string objectName =
                kodachi::concat(interface.getInputLocationPath(), "_GroupGeometry");
        interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);

        interface.setAttr("rdl2.sceneObject.attrs.references", absPath(interface, instanceSourceAttr));
    }
};

class KPOPGroupGeometry: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface& interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isGroupGeometry\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::StringAttribute children = interface.getPotentialChildren();
        for (const kodachi::string_view child : children.getNearestSample(0.f)) {
            interface.prefetch(child);
        }

        const std::string inputLocationPath = interface.getInputLocationPath();

        std::vector<std::string> childGeometry;
        for (const kodachi::string_view child : children.getNearestSample(0.f)) {
            const kodachi::IntAttribute isGeometryAttr =
                    interface.getAttr("rdl2.meta.isGeometry", child);
            if (isGeometryAttr.isValid()) {
                childGeometry.push_back(kodachi::concat(inputLocationPath, "/", child));
            }
        }

        if (!childGeometry.empty()) {
            interface.setAttr("rdl2.sceneObject.attrs.references",
                    kodachi::ZeroCopyStringAttribute::create(std::move(childGeometry)));
        }
    }
};

class KPOPInstanceArray: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and @rdl2.meta.kodachiType==\"instance array\"}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // A source attribute must exist, otherwise can't proceed
        const kodachi::StringAttribute instanceSourceAttr =
                interface.getAttr("geometry.instanceSource");
        if (!instanceSourceAttr.isValid()) {
            KdLogError("Missing 'geometry.instanceSource' attribute");
            return;
        }

        // SceneClass and SceneObject name
        {
            static const kodachi::StringAttribute kInstanceGeometryAttr("InstanceGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kInstanceGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_InstanceGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        }

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
            KdLogWarn("Missing 'geometry' attribute");
            return;
        }

        const kodachi::IntAttribute instanceIndexAttr =
                geometryAttr.getChildByName("instanceIndex");
        if (!instanceIndexAttr.isValid()) {
            KdLogError("Missing 'geometry.instanceIndex' attribute");
            return;
        }

        // Equivalent to number of instances
        const int64_t instanceCount = instanceIndexAttr.getNumberOfValues();

        const kodachi::DoubleAttribute instanceMatrixAttr =
                geometryAttr.getChildByName("instanceMatrix");

        if (instanceMatrixAttr.isValid() &&
                instanceMatrixAttr.getNumberOfValues() != instanceCount * 16) {
            kodachi::ReportNonCriticalError(interface,
                    "instanceMatrix count does not match instanceIndex count");
            return;
        }

        const kodachi::DoubleAttribute instanceTranslateAttr =
                geometryAttr.getChildByName("instanceTranslate");

        if (instanceTranslateAttr.isValid() &&
                instanceTranslateAttr.getNumberOfValues() != instanceCount * 3) {
            kodachi::ReportNonCriticalError(interface,
                    "instanceTranslate count does not match instanceIndex count");
            return;
        }

        const kodachi::DoubleAttribute instanceRotateXAttr =
                geometryAttr.getChildByName("instanceRotateX");

        if (instanceRotateXAttr.isValid() &&
                instanceRotateXAttr.getNumberOfValues() != instanceCount * 4) {
            kodachi::ReportNonCriticalError(interface,
                    "instanceRotateX count does not match instanceIndex count");
            return;
        }

        const kodachi::DoubleAttribute instanceRotateYAttr =
                geometryAttr.getChildByName("instanceRotateY");

        if (instanceRotateYAttr.isValid() &&
                instanceRotateYAttr.getNumberOfValues() != instanceCount * 4) {
            kodachi::ReportNonCriticalError(interface,
                    "instanceRotateY count does not match instanceIndex count");
            return;
        }

        const kodachi::DoubleAttribute instanceRotateZAttr =
                geometryAttr.getChildByName("instanceRotateZ");

        if (instanceRotateZAttr.isValid() &&
                instanceRotateZAttr.getNumberOfValues() != instanceCount * 4) {
            kodachi::ReportNonCriticalError(interface,
                    "instanceRotateZ count does not match instanceIndex count");
            return;
        }

        const kodachi::DoubleAttribute instanceScaleAttr =
                geometryAttr.getChildByName("instanceScale");

        if (instanceScaleAttr.isValid() &&
                instanceScaleAttr.getNumberOfValues() != instanceCount * 3) {
            kodachi::ReportNonCriticalError(interface,
                    "instanceScale count does not match instanceIndex count");
            return;
        }

        const bool isMotionBlurEnabled = kodachi::IntAttribute(
                interface.getAttr("rdl2.meta.mbEnabled")).getValue();
        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
        const float shutterClose = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterClose")).getValue();

        std::vector<Imath::M44d> instanceXform;
        std::vector<Imath::M44d> instanceXformMB;

        // for velocity calculations, we need positions at time 0 and
        // the previous or next sample, depending on shutter times being
        // negative or positive
        const float mbFrame = shutterOpen >= 0 ? 1.0f : -1.0f;

        // instanceMatrix
        if (instanceMatrixAttr.isValid()) {
            instanceXform.resize(instanceCount);
            if (isMotionBlurEnabled) {
                instanceXformMB.resize(instanceCount);
            }

            const auto instanceMatrixSamples = instanceMatrixAttr.getSamples();
            const auto matrixSampleTimes = instanceMatrixSamples.getSampleTimes();
            const int64_t numMatrixSamples = matrixSampleTimes.size();

            const std::array<float, 2> shutterTimes { 0.0f, mbFrame };

            const int numSamples = isMotionBlurEnabled ? 2 : 1;

            // Construct a temporary DoubleAttribute so that we can use the
            // XformUtil. We need to get the first pointer to each time sample
            std::vector<const double*> matrixData(numMatrixSamples);

            for (std::size_t i = 0; i < instanceCount; ++i) {
                for (int64_t s = 0; s < numMatrixSamples; ++s) {
                    matrixData[s] = instanceMatrixSamples[s].data() + (i * 16);
                }
                // Use the data from instanceMatrixAttr to create an attribute
                // for the single xform. This attribute does not own the data
                // so use the NullDeleter
                const kodachi::DoubleAttribute matrixAttr(matrixSampleTimes.data(),
                                                          numMatrixSamples,
                                                          matrixData.data(),
                                                          16,
                                                          16,
                                                          const_cast<double**>(matrixData.data()),
                                                          nullDeleter);

                const kodachi::GroupAttribute xformAttr("matrix", matrixAttr, false);

                const kodachi::DoubleAttribute interpolatedMatrixAttr =
                        kodachi::XFormUtil::CalcTransformMatrixAtTimes(
                                xformAttr, shutterTimes.data(), numSamples).first;

                const auto interpolatedMatrixSamples = interpolatedMatrixAttr.getSamples();

                setXformMatrix(instanceXform[i], interpolatedMatrixSamples.getNearestSample(0.0f).data());
                if (isMotionBlurEnabled) {
                    setXformMatrix(instanceXformMB[i], interpolatedMatrixSamples.getNearestSample(mbFrame).data());
                }
            }
        }

        // Apply SRT if attributes exist
        {
            XformBuilder xformBuilder(instanceCount, 0.0f,
                                      mbFrame, isMotionBlurEnabled);

            if (instanceScaleAttr.isValid()) {
                xformBuilder.scale(instanceScaleAttr);
            }

            if (instanceRotateXAttr.isValid()) {
                xformBuilder.rotate(instanceRotateXAttr);
            }

            if (instanceRotateYAttr.isValid()) {
                xformBuilder.rotate(instanceRotateYAttr);
            }

            if (instanceRotateZAttr.isValid()) {
                xformBuilder.rotate(instanceRotateZAttr);
            }

            if (instanceTranslateAttr.isValid()) {
                xformBuilder.translate(instanceTranslateAttr);
            }

            if (instanceXform.empty()) {
                instanceXform = std::move(xformBuilder.mLocalXform);
            } else if (!xformBuilder.mLocalXform.empty()) {
                for (std::size_t i = 0; i < instanceCount; ++i) {
                    instanceXform[i] *= xformBuilder.mLocalXform[i];
                }
            }

            if (instanceXformMB.empty()) {
                instanceXformMB = std::move(xformBuilder.mLocalXformMB);
            } else if (!xformBuilder.mLocalXformMB.empty()) {
                for (std::size_t i = 0; i < instanceCount; ++i) {
                    instanceXformMB[i] *= xformBuilder.mLocalXformMB[i];
                }
            }
        }

        // If we are skipping indicies we need to know before we start creating
        // the rdl2 attributes
        const std::vector<std::size_t> indices = createIndices(
                instanceCount, geometryAttr.getChildByName("instanceSkipIndex"));

        kodachi::FloatVector positionsVec;
        positionsVec.reserve(indices.size() * 3);

        kodachi::FloatVector scalesVec;
        scalesVec.reserve(indices.size() * 3);

        kodachi::FloatVector orientationsVec;
        orientationsVec.reserve(indices.size() * 4);

        const float fpsAttr = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.fps")).getValue();
        const float velocityScale = fpsAttr; // divided by shutter length of 1
        const bool useVelocity = !instanceXformMB.empty();

        kodachi::FloatVector velocitiesVec;
        if (useVelocity) {
            velocitiesVec.reserve(indices.size() * 3);
        }

        for (const std::size_t idx : indices) {
            Imath::M44d& imat = instanceXform[idx];

            // Extract position
            const Imath::V3d pos = imat.translation();
            positionsVec.push_back(static_cast<float>(pos.x));
            positionsVec.push_back(static_cast<float>(pos.y));
            positionsVec.push_back(static_cast<float>(pos.z));

            // Extract scale
            Imath::V3d scale(0);
            Imath::V3d shear;
            // shear and scale will be removed from imat
            // This is required to have correct quaternions
            // later while extracting orientations
            Imath::extractAndRemoveScalingAndShear(imat, scale, shear, false);
            scalesVec.push_back(static_cast<float>(scale.x));
            scalesVec.push_back(static_cast<float>(scale.y));
            scalesVec.push_back(static_cast<float>(scale.z));

            // Extract orientation
            const Imath::Quatd orientation = Imath::extractQuat(imat);
            orientationsVec.push_back(static_cast<float>(orientation.v.x));
            orientationsVec.push_back(static_cast<float>(orientation.v.y));
            orientationsVec.push_back(static_cast<float>(orientation.v.z));
            orientationsVec.push_back(static_cast<float>(orientation.r));

            if (useVelocity) {
                // Determine vector between start and end positions
                // imat2 is at time -1
                const Imath::M44d& imat2 = instanceXformMB[idx];
                const Imath::V3d pos2 = imat2.translation();

                const Imath::V3d velocity =
                        mbFrame > 0.0f ?
                                (pos2 - pos) * velocityScale : //
                                (pos - pos2) * velocityScale;

                velocitiesVec.emplace_back(static_cast<float>(velocity.x));
                velocitiesVec.emplace_back(static_cast<float>(velocity.y));
                velocitiesVec.emplace_back(static_cast<float>(velocity.z));
            }
        }

        // set attributes
        kodachi::GroupBuilder attrsGb;
        attrsGb.setGroupInherit(false)
               .update(interface.getAttr("rdl2.sceneObject.attrs"));

        // positions
        attrsGb.set("positions",
                kodachi::ZeroCopyFloatAttribute::create(std::move(positionsVec), 3));

        // orientations
        attrsGb.set("orientations",
                kodachi::ZeroCopyFloatAttribute::create(std::move(orientationsVec), 4));

        // scales
        attrsGb.set("scales",
                kodachi::ZeroCopyFloatAttribute::create(std::move(scalesVec), 3));

        // velocities
        if (!velocitiesVec.empty()) {
            attrsGb.set("velocities",
                kodachi::ZeroCopyFloatAttribute::create(std::move(velocitiesVec), 3));
        }

        // refIndices
        if (indices.size() == instanceCount) {
            attrsGb.set("refIndices", instanceIndexAttr);
        } else {
            kodachi::IntVector refIndices;
            refIndices.reserve(indices.size());

            const auto instanceIndexSample = instanceIndexAttr.getNearestSample(0.f);
            for (const std::size_t idx : indices) {
                refIndices.push_back(instanceIndexSample[idx]);
            }

            attrsGb.set("refIndices", kodachi::ZeroCopyIntAttribute::create(std::move(refIndices)));
        }

        // references
        attrsGb.set("references", absPath(interface, instanceSourceAttr));

        interface.setAttr("rdl2.sceneObject.attrs", attrsGb.build(), false);
    }

private:
    // no-op delete function to prevent DataAttributes from deleting their data
    // when they go out of scope
    static void nullDeleter(void*) {};

    // Creates a vector with values [0,numInstances) with any values from
    // skipIndexAttr removed
    static std::vector<std::size_t>
    createIndices(const std::size_t numInstances,
                  const kodachi::IntAttribute& skipIndexAttr)
    {
        std::vector<std::size_t> indices;

        if (!skipIndexAttr.isValid()) {
            indices.resize(numInstances);
            std::iota(indices.begin(), indices.end(), static_cast<std::size_t>(0));
        } else {
            // Foundry documentation doesn't say anything about skip index having
            // to be in increasing order, so sort it first to be safe
            const auto skipIndexSample = skipIndexAttr.getNearestSample(0.f);
            std::vector<std::size_t> skipIndex(skipIndexSample.begin(), skipIndexSample.end());
            std::sort(skipIndex.begin(), skipIndex.end());

            indices.reserve(numInstances - skipIndex.size());

            auto skipIter = skipIndex.begin();
            const auto skipEnd = skipIndex.end();

            for (std::size_t i = 0; i < numInstances; ++i) {
                if (skipIter != skipEnd && *skipIter == i) {
                    ++skipIter;
                    continue;
                }

                indices.push_back(i);
            }
        }

        return indices;
    }

    // Helper class for creating composite xforms from the individual SRT attrs
    class XformBuilder
    {
    public:
        XformBuilder(std::size_t instanceCount, float shutterOpen, float shutterClose, bool isMbEnabled)
        : mInstanceCount(instanceCount)
        , mShutterOpen(shutterOpen)
        , mShutterClose(shutterClose)
        , mIsMbEnabled(isMbEnabled)
        {}

        void scale(const kodachi::DoubleAttribute& scaleAttr)
        {
            lazyInit();

            const auto interpolatedDataAttrs = interpolateData(scaleAttr);

            const auto interpolatedData = getData(interpolatedDataAttrs);

            kodachi::array_view<const Imath::V3d> scaleArr(
                    toV3d(interpolatedData.first), mInstanceCount);

            for (std::size_t i = 0; i < scaleArr.size(); ++i) {
                mLocalXform[i].scale(scaleArr[i]);
            }

            kodachi::array_view<const Imath::V3d> scaleArrMB;
            if (interpolatedData.second) {
                scaleArrMB = kodachi::array_view<const Imath::V3d>(
                        toV3d(interpolatedData.second), mInstanceCount);
            }

            for (std::size_t i = 0; i < scaleArrMB.size(); ++i) {
                mLocalXformMB[i].scale(scaleArrMB[i]);
            }
        }

        void rotate(const kodachi::DoubleAttribute& rotateAttr)
        {
            lazyInit();

            const auto interpolatedDataAttrs = interpolateData(rotateAttr);

            const auto interpolatedData = getData(interpolatedDataAttrs);

            kodachi::array_view<const Imath::V4d> rotateArr(
                    toV4d(interpolatedData.first), mInstanceCount);

            for (std::size_t i = 0; i < rotateArr.size(); ++i) {
                const Imath::V4d& rotationVec = rotateArr[i];

                const double angle = toRadians(rotationVec[0]);
                const Imath::V3d axis(rotationVec[1], rotationVec[2], rotationVec[3]);
                Imath::M44d rotationMatrix;
                rotationMatrix.setAxisAngle(axis, angle);

                mLocalXform[i] *= rotationMatrix;
            }

            kodachi::array_view<const Imath::V4d> rotateArrMB;
            if (interpolatedData.second) {
                rotateArrMB = kodachi::array_view<const Imath::V4d>(
                        toV4d(interpolatedData.second), mInstanceCount);
            }

            for (std::size_t i = 0; i < rotateArrMB.size(); ++i) {
                const Imath::V4d& rotationVec = rotateArrMB[i];

                const double angle = toRadians(rotationVec[0]);
                const Imath::V3d axis(rotationVec[1], rotationVec[2], rotationVec[3]);
                Imath::M44d rotationMatrix;
                rotationMatrix.setAxisAngle(axis, angle);

                mLocalXformMB[i] *= rotationMatrix;
            }
        }

        void translate(const kodachi::DoubleAttribute& translateAttr)
        {
            lazyInit();

            const auto interpolatedDataAttrs = interpolateData(translateAttr);

            const auto interpolatedData = getData(interpolatedDataAttrs);

            kodachi::array_view<const Imath::V3d> translateArr(
                    toV3d(interpolatedData.first), mInstanceCount);

            for (std::size_t i = 0; i < translateArr.size(); ++i) {
                mLocalXform[i].translate(translateArr[i]);
            }

            kodachi::array_view<const Imath::V3d> translateArrMB;
            if (interpolatedData.second) {
                translateArrMB = kodachi::array_view<const Imath::V3d>(
                        toV3d(interpolatedData.second), mInstanceCount);
            }

            for (std::size_t i = 0; i < translateArrMB.size(); ++i) {
                mLocalXformMB[i].translate(translateArrMB[i]);
            }
        }

        std::vector<Imath::M44d> mLocalXform;
        std::vector<Imath::M44d> mLocalXformMB;

    private:
        const Imath::V3d*
        toV3d(const double* data)
        {
            return reinterpret_cast<const Imath::V3d*>(data);
        }

        const Imath::V4d*
        toV4d(const double* data)
        {
            return reinterpret_cast<const Imath::V4d*>(data);
        }

        void lazyInit()
        {
            if (mLocalXform.empty()) {
                mLocalXform.resize(mInstanceCount);
                for (auto& xform : mLocalXform) { xform.makeIdentity(); }
            }

            if (mIsMbEnabled && mLocalXformMB.empty()) {
                mLocalXformMB.resize(mInstanceCount);
                for (auto& xform : mLocalXformMB) { xform.makeIdentity(); }
            }
        }

        std::pair<kodachi::DoubleAttribute, kodachi::DoubleAttribute>
        interpolateData(const kodachi::DataAttribute& dataAttr)
        {
            kodachi::DoubleAttribute dataShutterOpenAttr =
                    kodachi::interpolateAttr(dataAttr, mShutterOpen);

            kodachi::DoubleAttribute dataShutterCloseAttr;
            if (mIsMbEnabled) {
                dataShutterCloseAttr =
                        kodachi::interpolateAttr(dataAttr, mShutterClose);
            }

            return { std::move(dataShutterOpenAttr), std::move(dataShutterCloseAttr) };
        }

        std::pair<const double*, const double*>
        getData(const std::pair<kodachi::DoubleAttribute, kodachi::DoubleAttribute>& dataAttrs)
        {
            const double* dataShutterOpen = nullptr;
            {
                const auto dataSample = dataAttrs.first.getNearestSample(0.f);
                dataShutterOpen = dataSample.data();
            }

            const double* dataShutterClose = nullptr;
            if (dataAttrs.second.isValid()){
                const auto dataSample = dataAttrs.second.getNearestSample(0.f);
                dataShutterClose = dataSample.data();
            }

            return { dataShutterOpen, dataShutterClose };
        }

        inline double
        toRadians(const double degrees)
        {
            constexpr double kPiOver180 = M_PI / 180.0;

            return degrees * kPiOver180;
        }

        std::size_t mInstanceCount;
        float mShutterOpen;
        float mShutterClose;
        bool mIsMbEnabled;
    };

    inline static void
    setXformMatrix(Imath::M44d& mat, const double* arr)
    {
        if (arr) {
            mat[0][0] = arr[0];
            mat[0][1] = arr[1];
            mat[0][2] = arr[2];
            mat[0][3] = arr[3];
            mat[1][0] = arr[4];
            mat[1][1] = arr[5];
            mat[1][2] = arr[6];
            mat[1][3] = arr[7];
            mat[2][0] = arr[8];
            mat[2][1] = arr[9];
            mat[2][2] = arr[10];
            mat[2][3] = arr[11];
            mat[3][0] = arr[12];
            mat[3][1] = arr[13];
            mat[3][2] = arr[14];
            mat[3][3] = arr[15];
        }
    }
};

class KPOPAutoInstancing: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface& interface)
    {
        if (interface.atRoot()) {
            const kodachi::IntAttribute autoInstancingAttr =
                    interface.getAttr("moonrayGlobalStatements.autoInstancing");

            // If autoInstancing is disabled there is no need to run this op
            if (!autoInstancingAttr.getValue(true, false)) {
                interface.stopChildTraversal();
            }

            return;
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isGeometry\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // If instance.ID has already been set by the user then use that
        const kodachi::StringAttribute instanceIdAttr =
                interface.getAttr("instance.ID");

        if (instanceIdAttr.isValid()) {
            return;
        }

        const kodachi::IntAttribute autoInstancingEnabledAttr =
                interface.getAttr("rdl2.meta.autoInstancing.enabled");

        if (!autoInstancingEnabledAttr.isValid()) {
            return;
        }

        // If no attrs were set then we don't have enough information to
        // create an instance ID
        const kodachi::GroupAttribute autoInstancingAttrs =
                interface.getAttr("rdl2.meta.autoInstancing.attrs");

        if (!autoInstancingAttrs.isValid()) {
            return;
        }

        interface.setAttr("instance.ID",
                          kodachi::StringAttribute(autoInstancingAttrs.getHash().str()),
                          false);
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPInstance)
DEFINE_KODACHIOP_PLUGIN(KPOPInstanceArray)
DEFINE_KODACHIOP_PLUGIN(KPOPInstanceSource)
DEFINE_KODACHIOP_PLUGIN(KPOPGroupGeometry)
DEFINE_KODACHIOP_PLUGIN(KPOPAutoInstancing)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPInstance, "KPOPInstance", 0, 1);
    REGISTER_PLUGIN(KPOPInstanceArray, "KPOPInstanceArray", 0, 1);
    REGISTER_PLUGIN(KPOPInstanceSource, "KPOPInstanceSource", 0, 1);
    REGISTER_PLUGIN(KPOPGroupGeometry, "KPOPGroupGeometry", 0, 1);
    REGISTER_PLUGIN(KPOPAutoInstancing, "KPOPAutoInstancing", 0, 1);
}

