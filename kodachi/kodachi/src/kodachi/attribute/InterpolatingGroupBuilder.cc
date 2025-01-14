// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/attribute/InterpolatingGroupBuilder.h>

#include <kodachi/attribute/AttributeUtils.h>

namespace {

constexpr float kFloatEpsilon = std::numeric_limits<float>::epsilon();

bool floatEquals(float l, float r) { return std::fabs(l - r) < kFloatEpsilon; }

}

namespace kodachi {

InterpolatingGroupBuilder::InterpolatingGroupBuilder(float shutterOpen,
                                                     float shutterClose)
:   mShutterOpen(shutterOpen)
,   mShutterClose(shutterClose)
,   mbEnabled(!floatEquals(mShutterOpen, mShutterClose))
{
}

InterpolatingGroupBuilder::InterpolatingGroupBuilder(float shutterOpen,
                                                     float shutterClose,
                                                     GroupBuilder::BuilderMode builderMode)
:   mGb(builderMode)
,   mShutterOpen(shutterOpen)
,   mShutterClose(shutterClose)
,   mbEnabled(!floatEquals(mShutterOpen, mShutterClose))
{
}

void
InterpolatingGroupBuilder::reset()
{
    mGb.reset();
}

bool
InterpolatingGroupBuilder::isValid() const
{
    return mGb.isValid();
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::set(const kodachi::string_view& path,
                               const kodachi::Attribute& attr,
                               bool groupInherit)
{
    kodachi::DataAttribute dataAttr(attr);
    if (!dataAttr.isValid() || dataAttr.getNumberOfTimeSamples() == 1) {
        // early out, this is either a group attribute or a single-sampled data
        // attribute so set it directly
        mGb.set(path, attr, groupInherit);
        return *this;
    }

    switch (attr.getType()) {
    case kAttrTypeFloat:
        mGb.set(path, interpolateAttr(FloatAttribute(dataAttr), mShutterOpen), groupInherit);
        break;
    case kAttrTypeDouble:
        mGb.set(path, interpolateAttr(DoubleAttribute(dataAttr), mShutterOpen), groupInherit);
        break;
    default:
        mGb.set(path, attr, groupInherit);
    }


    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::setBlurrable(const kodachi::string_view& path,
                                        const kodachi::Attribute& attr,
                                        bool groupInherit)
{
    if (!mbEnabled) {
        return set(path, attr, groupInherit);
    }

    kodachi::DataAttribute dataAttr(attr);
    if (!dataAttr.isValid() || dataAttr.getNumberOfTimeSamples() == 1) {
        // early out, this is either a group attribute or a single-sampled data
        // attribute so set it directly
        mGb.set(path, attr, groupInherit);
        return *this;
    }

    const int64_t numValues = dataAttr.getNumberOfValues();
    const int64_t tupleSize = dataAttr.getTupleSize();

    switch (attr.getType()) {
    case kAttrTypeFloat:
    {
        const FloatAttribute floatAttr(dataAttr);
        kodachi::FloatArray floatArray(new kodachi::Float(numValues * 2));
        floatAttr.fillInterpSample(floatArray.get(), numValues, mShutterOpen);
        floatAttr.fillInterpSample(floatArray.get() + numValues, numValues, mShutterClose);
        dataAttr = ZeroCopyFloatAttribute::create({mShutterOpen, mShutterClose},
                                                  std::move(floatArray),
                                                  numValues, tupleSize);
        break;
    }
    case kAttrTypeDouble:
    {
        const DoubleAttribute doubleAttr(dataAttr);
        kodachi::DoubleArray doubleArray(new kodachi::Double(numValues * 2));
        doubleAttr.fillInterpSample(doubleArray.get(), numValues, mShutterOpen);
        doubleAttr.fillInterpSample(doubleArray.get() + numValues, numValues, mShutterClose);

        dataAttr = ZeroCopyDoubleAttribute::create({mShutterOpen, mShutterClose},
                                                   std::move(doubleArray),
                                                   numValues, tupleSize);
        break;
    }
    default:
        mGb.set(path, attr, groupInherit);
    }

    return *this;
}

// Sets the passed in attribute without applying any interpolation
InterpolatingGroupBuilder&
InterpolatingGroupBuilder::setWithoutInterpolation(const kodachi::string_view& path,
                                                   const kodachi::Attribute& attr,
                                                   bool groupInherit)
{
    mGb.set(path, attr, groupInherit);

    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::del(const kodachi::string_view& path)
{
    mGb.del(path);

    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::update(const kodachi::GroupAttribute& attr)
{
    mGb.update(attr);

    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::deepUpdate(const kodachi::GroupAttribute& attr)
{
    mGb.deepUpdate(attr);

    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::reserve(int64_t n)
{
    mGb.reserve(n);

    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::setGroupInherit(bool groupInherit)
{
    mGb.setGroupInherit(groupInherit);

    return *this;
}

InterpolatingGroupBuilder&
InterpolatingGroupBuilder::sort()
{
    mGb.sort();

    return *this;
}

GroupAttribute
InterpolatingGroupBuilder::build(GroupBuilder::BuilderBuildMode builderMode)
{
    return mGb.build(builderMode);
}

} // namespace kodachi

