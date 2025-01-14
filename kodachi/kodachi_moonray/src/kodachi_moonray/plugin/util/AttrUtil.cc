// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "AttrUtil.h"

#include <scene_rdl2/scene/rdl2/Types.h>

#include <kodachi/logging/KodachiLogging.h>

using namespace arras;
using namespace kodachi;

namespace {
KdLogSetup("AttrUtil")

inline void
validateSize(const DataAttribute& attr, int64_t expected)
{
    if (!attr.isValid()) {
        throw kodachi_moonray::util::InvalidAttributeError::fromAttr(attr);
    }

    const int64_t actual = attr.getNumberOfValues();
    if (actual != expected) {
        std::stringstream ss;
        ss << "Unexpected Attribute size. Expected: "
           << expected << ", Actual: " << actual;
        throw kodachi_moonray::util::AttributeDataError(ss.str());
    }
}

inline std::size_t
getNumberOfTuples(const DataAttribute& attr, int64_t tupleSize)
{
    const std::size_t numValues = attr.getNumberOfValues();
    const std::ldiv_t divt = std::div(numValues, tupleSize);
    if (divt.rem != 0) {
        std::stringstream ss;
        ss << "Unexpected number of values. Expected multiple of "
           << tupleSize << ", Actual: " << numValues;
        throw kodachi_moonray::util::AttributeDataError(ss.str());
    }

    return divt.quot;
}

/////// Conversion Helpers /////////

template<class Rdl2Type, class FnAttrType>
inline Rdl2Type
asVec2(const FnAttrType& attr, const float time)
{
    auto nearestSample = attr.getNearestSample(time);
    return Rdl2Type(nearestSample[0], nearestSample[1]);
}

template<class Rdl2Type, class FnAttrType>
inline Rdl2Type
asVec3(const FnAttrType& attr, const float time)
{
    const auto nearestSample = attr.getNearestSample(time);
    return Rdl2Type(nearestSample[0], nearestSample[1], nearestSample[2]);
}

template<class Rdl2Type, class FnAttrType>
inline Rdl2Type
asVec4(const FnAttrType& attr, const float time)
{
    const auto nearestSample = attr.getNearestSample(time);
    return Rdl2Type(nearestSample[0],
                    nearestSample[1],
                    nearestSample[2],
                    nearestSample[3]);
}

template<class Rdl2Type, class FnAttrType>
inline Rdl2Type
asMat4(const FnAttrType& attr, float time)
{
    const auto nearestSample = attr.getNearestSample(time);

    return Rdl2Type(nearestSample[0],
                    nearestSample[1],
                    nearestSample[2],
                    nearestSample[3],
                    nearestSample[4],
                    nearestSample[5],
                    nearestSample[6],
                    nearestSample[7],
                    nearestSample[8],
                    nearestSample[9],
                    nearestSample[10],
                    nearestSample[11],
                    nearestSample[12],
                    nearestSample[13],
                    nearestSample[14],
                    nearestSample[15]);
}

template<typename Rdl2Type, typename FnAttrType>
Rdl2Type
asVector(const FnAttrType& attr, const float time)
{
    const std::size_t numTuples = getNumberOfTuples(attr, 1);

    const auto nearestSample = attr.getNearestSample(time);
    return Rdl2Type(nearestSample.begin(), nearestSample.end());
}

template <class VectorType, class FnAttrType>
VectorType
asVec2Vector(const FnAttrType& attr, const float time)
{
    const std::size_t numTuples = getNumberOfTuples(attr, 2);

    VectorType vec;
    vec.reserve(numTuples);

    const auto nearestSample = attr.getNearestSample(time);
    for (size_t i = 0; i < nearestSample.size(); i += 2) {
        vec.emplace_back(nearestSample[i], nearestSample[i + 1]);
    }

    return vec;
}

template <class VectorType, class FnAttrType>
VectorType
asVec3Vector(const FnAttrType& attr, const float time)
{
    const std::size_t numTuples = getNumberOfTuples(attr, 3);

    VectorType vec;
    vec.reserve(numTuples);

    const auto nearestSample = attr.getNearestSample(time);
    for (size_t i = 0; i < nearestSample.size(); i += 3) {
        vec.emplace_back(nearestSample[i],
                         nearestSample[i + 1],
                         nearestSample[i + 2]);
    }

    return vec;
}

template <class VectorType, class FnAttrType>
VectorType
asVec4Vector(const FnAttrType& attr, const float time)
{
    const std::size_t numTuples = getNumberOfTuples(attr, 4);

    VectorType vec;
    vec.reserve(numTuples);

    const auto nearestSample = attr.getNearestSample(time);
    for (size_t i = 0; i < nearestSample.size(); i += 4) {
        vec.emplace_back(nearestSample[i],
                         nearestSample[i + 1],
                         nearestSample[i + 2],
                         nearestSample[i + 3]);
    }

    return vec;
}

template <class VectorType, class FnAttrType>
VectorType
asMat4Vector(const FnAttrType& attr, const float time)
{
    const std::size_t numTuples = getNumberOfTuples(attr, 16);

    VectorType vec;
    vec.reserve(numTuples);

    const auto nearestSample = attr.getNearestSample(time);
    for (size_t i = 0; i < nearestSample.size(); i += 16) {
        vec.emplace_back(nearestSample[i],
                         nearestSample[i + 1],
                         nearestSample[i + 2],
                         nearestSample[i + 3],
                         nearestSample[i + 4],
                         nearestSample[i + 5],
                         nearestSample[i + 6],
                         nearestSample[i + 7],
                         nearestSample[i + 8],
                         nearestSample[i + 9],
                         nearestSample[i + 10],
                         nearestSample[i + 11],
                         nearestSample[i + 12],
                         nearestSample[i + 13],
                         nearestSample[i + 14],
                         nearestSample[i + 15]
                        );
    }

    return vec;
}

template <class ATTR>
inline typename ATTR::value_type
getValue(const ATTR& attr, const float time)
{
    if (attr.isValid()) {
        const typename ATTR::array_type nearestSample = attr.getNearestSample(time);
        if (!nearestSample.empty()) {
            return nearestSample[0];
        }
    }

    throw kodachi_moonray::util::InvalidAttributeError::fromAttr(attr);
}

template <class rdl2_type>
inline void HandleTruncation(const kodachi_moonray::util::TruncateBehavior behavior,
                             const DataAttribute& attr)
{
    if (behavior != kodachi_moonray::util::TruncateBehavior::IGNORE) {
        std::stringstream ss;
        ss << "Conversion from Attribute of type: '";
        kodachi::GetAttrTypeAsPrettyText(ss, attr);
        ss << "' to arras::rdl2::" << arras::rdl2::attributeTypeName<rdl2_type>();
        ss << ", possible loss of data.";
        if (behavior == kodachi_moonray::util::TruncateBehavior::WARN) {
            KdLogWarn(ss.str());
        } else {
            throw kodachi_moonray::util::TruncationError(ss.str());
        }
    }
}

template <class attr_type>
struct Interpolator
{
    using value_type = typename attr_type::value_type;

    static void deleter(void* context)
    {
        delete[] reinterpret_cast<value_type*>(context);
    }

    static Attribute
    fillIterpSample(const attr_type& attr, const float time)
    {
        const int64_t numValues = attr.getNumberOfValues();
        const int64_t tupleSize = attr.getTupleSize();

        std::unique_ptr<value_type[]> data(new value_type[numValues]);
        attr.fillInterpSample(data.get(), numValues, time);
        return attr_type(data.get(), numValues, tupleSize, data.release(), &deleter);
    }
};

void floatDeleter(void* context) {
    delete[] reinterpret_cast<float*>(context);
}

} // anonymous namespace

namespace kodachi_moonray {
namespace util {

InvalidAttributeError
InvalidAttributeError::fromAttr(const kodachi::Attribute& attr)
{
    std::stringstream ss;
    ss << "Attribute of type '";
    kodachi::GetAttrTypeAsPrettyText(ss, attr);
    ss << "' is not valid.";

    return InvalidAttributeError(ss.str());
}

/////// Convert kodachi Attribute to rdl2 Attribute Type /////////

// TYPE_BOOL
template<>
rdl2::Bool
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Bool;

    validateSize(attr, 1);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return getValue<IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return getValue<FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return getValue<DoubleAttribute>(attr, time);
    case kodachi::kAttrTypeString:
        break;
        // Do nothing for now? We could support "true" and "false" values
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_INT
template<>
rdl2::Int
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Int;

    validateSize(attr, 1);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return getValue<IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        HandleTruncation<return_t>(behavior, attr);
        return static_cast<return_t>(getValue<FloatAttribute>(attr, time));
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return static_cast<return_t>(getValue<DoubleAttribute>(attr, time));
    case kodachi::kAttrTypeString:
        return std::stoi(getValue<StringAttribute>(attr, time));
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_LONG
template<>
rdl2::Long
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Long;

    validateSize(attr, 1);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return getValue<IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        HandleTruncation<return_t>(behavior, attr);
        return static_cast<return_t>(getValue<FloatAttribute>(attr, time));
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return static_cast<return_t>(getValue<DoubleAttribute>(attr, time));
    case kodachi::kAttrTypeString:
        return std::stol(getValue<StringAttribute>(attr, time));
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_FLOAT
template<>
rdl2::Float
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Float;

    validateSize(attr, 1);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return static_cast<return_t>(getValue<IntAttribute>(attr, time));
    case kodachi::kAttrTypeFloat:
        return getValue<FloatAttribute>(attr, time);;
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return static_cast<return_t>(getValue<DoubleAttribute>(attr, time));
    case kodachi::kAttrTypeString:
        return std::stof(getValue<StringAttribute>(attr, time));
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_DOUBLE
template<>
rdl2::Double
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Double;

    validateSize(attr, 1);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return static_cast<return_t>(getValue<IntAttribute>(attr, time));
    case kodachi::kAttrTypeFloat:
        return static_cast<return_t>(getValue<FloatAttribute>(attr, time));
    case kodachi::kAttrTypeDouble:
        return getValue<DoubleAttribute>(attr, time);
    case kodachi::kAttrTypeString:
        return std::stod(getValue<StringAttribute>(attr, time));
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_STRING
template<>
rdl2::String
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::String;

    validateSize(attr, 1);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return std::to_string(getValue<IntAttribute>(attr, time));
    case kodachi::kAttrTypeFloat:
        return std::to_string(getValue<FloatAttribute>(attr, time));
    case kodachi::kAttrTypeDouble:
        return std::to_string(getValue<DoubleAttribute>(attr, time));
    case kodachi::kAttrTypeString:
        return getValue<StringAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_RGB
template<>
rdl2::Rgb
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    static_assert(std::is_same<rdl2::Rgb::Scalar, rdl2::Float>::value, "Assume Rgb has float scalar");

    using return_t = rdl2::Rgb;

    validateSize(attr, 3);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec3<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec3<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec3<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_RGBA
template<>
rdl2::Rgba
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    static_assert(std::is_same<rdl2::Rgba::Scalar, rdl2::Float>::value, "Assume Rgba has float scalar");

    using return_t = rdl2::Rgba;

    validateSize(attr, 4);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec4<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec4<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec4<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC2F
template<>
rdl2::Vec2f
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec2f;

    validateSize(attr, 2);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec2<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec2<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec2<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC2D
template<>
rdl2::Vec2d
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec2d;

    validateSize(attr, 2);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec2<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec2<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVec2<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC3F
template<>
rdl2::Vec3f
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec3f;

    validateSize(attr, 3);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec3<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec3<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec3<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC3D
template<>
rdl2::Vec3d
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec3d;

    validateSize(attr, 3);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec3<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec3<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVec3<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC4F
template<>
rdl2::Vec4f
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec4f;

    validateSize(attr, 4);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec4<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec4<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec4<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC4D
template<>
rdl2::Vec4d
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec4d;

    validateSize(attr, 4);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec4<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec4<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVec4<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_MAT4F
template<>
rdl2::Mat4f
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Mat4f;

    validateSize(attr, 16);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asMat4<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asMat4<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asMat4<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_MAT4D
template<>
rdl2::Mat4d
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Mat4d;

    validateSize(attr, 16);

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asMat4<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asMat4<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asMat4<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_BOOL_VECTOR
template<>
rdl2::BoolVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::BoolVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_INT_VECTOR
template<>
rdl2::IntVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::IntVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        HandleTruncation<return_t>(behavior, attr);
        return asVector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_LONG_VECTOR
template<>
rdl2::LongVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::LongVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        HandleTruncation<return_t>(behavior, attr);
        return asVector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_FLOAT_VECTOR
template<>
rdl2::FloatVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::FloatVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_DOUBLE_VECTOR
template<>
rdl2::DoubleVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::DoubleVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_STRING_VECTOR
template<>
rdl2::StringVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::StringVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeString:
        return asVector<return_t, StringAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_RGB_VECTOR
template<>
rdl2::RgbVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::RgbVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec3Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec3Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec3Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_RGBA_VECTOR
template<>
rdl2::RgbaVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::RgbaVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec4Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec4Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec4Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC2F_VECTOR
template<>
rdl2::Vec2fVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec2fVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec2Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec2Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec2Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC2D_VECTOR
template<>
rdl2::Vec2dVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec2dVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec2Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec2Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVec2Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC3F_VECTOR
template<>
rdl2::Vec3fVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec3fVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec3Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec3Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec3Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC3D_VECTOR
template<>
rdl2::Vec3dVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec3dVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec3Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec3Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVec3Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC4F_VECTOR
template<>
rdl2::Vec4fVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec4fVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec4Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec4Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asVec4Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_VEC4D_VECTOR
template<>
rdl2::Vec4dVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Vec4dVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asVec4Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asVec4Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asVec4Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_MAT4F_VECTOR
template<>
rdl2::Mat4fVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Mat4fVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asMat4Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asMat4Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        HandleTruncation<return_t>(behavior, attr);
        return asMat4Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

// TYPE_MAT4D_VECTOR
template<>
rdl2::Mat4dVector
rdl2Convert(const DataAttribute& attr, const float time, const TruncateBehavior behavior)
{
    using return_t = rdl2::Mat4dVector;

    if (!attr.isValid()) {
        throw util::InvalidAttributeError::fromAttr(attr);
    }

    switch (attr.getType()) {
    case kodachi::kAttrTypeInt:
        return asMat4Vector<return_t, IntAttribute>(attr, time);
    case kodachi::kAttrTypeFloat:
        return asMat4Vector<return_t, FloatAttribute>(attr, time);
    case kodachi::kAttrTypeDouble:
        return asMat4Vector<return_t, DoubleAttribute>(attr, time);
    }

    throw AttributeTypeError::invalidConversion<return_t>(attr);
}

} // namespace util
} // namespace kodachi_moonray

