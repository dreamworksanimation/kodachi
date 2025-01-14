// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cppunit/TestFixture.h>
#include <cppunit/extensions/HelperMacros.h>

#include <kodachi/attribute/Attribute.h>

#include "../AttrUtil.h"

using namespace kodachi;

#ifndef SCONS_REZ_KATANA_ROOT
#error "SCONS_REZ_KATANA_ROOT must be defined in the SConscript"
#endif

/**
 * Stream operators to enable CPPUNIT_ASSERT_EQUAL for vector and deque types
 */
template <class T>
inline std::ostream& operator << (std::ostream& os, const std::vector<T>& v)
{
    os << "{";
    for (auto& x : v) {
        os << " " << x;
    }
    os << " }";

    return os;
}

template <class T>
inline std::ostream& operator << (std::ostream& os, const std::deque<T>& v)
{
    os << "{";
    for (auto& x : v) {
        os << " " << x;
    }
    os << " }";

    return os;
}

namespace kodachi_moonray {
namespace unittest {

struct TestAttrUtil : public CppUnit::TestFixture
{
    using bool_t   = arras::rdl2::Bool;
    using int_t    = arras::rdl2::Int;
    using long_t   = arras::rdl2::Long;
    using float_t  = arras::rdl2::Float;
    using double_t = arras::rdl2::Double;
    using string_t = arras::rdl2::String;
    using rgb_t    = arras::rdl2::Rgb;
    using rgba_t   = arras::rdl2::Rgba;
    using vec2f_t  = arras::rdl2::Vec2f;
    using vec2d_t  = arras::rdl2::Vec2d;
    using vec3f_t  = arras::rdl2::Vec3f;
    using vec3d_t  = arras::rdl2::Vec3d;
    using vec4f_t  = arras::rdl2::Vec4f;
    using vec4d_t  = arras::rdl2::Vec4d;
    using mat4f_t  = arras::rdl2::Mat4f;
    using mat4d_t  = arras::rdl2::Mat4d;

    using boolvector_t = arras::rdl2::BoolVector;
    using intvector_t = arras::rdl2::IntVector;
    using longvector_t = arras::rdl2::LongVector;
    using floatvector_t = arras::rdl2::FloatVector;
    using doublevector_t = arras::rdl2::DoubleVector;
    using stringvector_t = arras::rdl2::StringVector;
    using rgbvector_t = arras::rdl2::RgbVector;
    using rgbavector_t = arras::rdl2::RgbaVector;
    using vec2fvector_t = arras::rdl2::Vec2fVector;
    using vec2dvector_t = arras::rdl2::Vec2dVector;
    using vec3fvector_t = arras::rdl2::Vec3fVector;
    using vec3dvector_t = arras::rdl2::Vec3dVector;
    using vec4fvector_t = arras::rdl2::Vec4fVector;
    using vec4dvector_t = arras::rdl2::Vec4dVector;
    using mat4fvector_t = arras::rdl2::Mat4fVector;
    using mat4dvector_t = arras::rdl2::Mat4dVector;

    void setUp() override
    {
        const std::string katanaRoot(SCONS_REZ_KATANA_ROOT);
        FnAttribute::Bootstrap(katanaRoot + "/ext");
    }

    template <class ATTR, size_t N>
    ATTR makeMultiSampleAttr(std::array<float, N> times,
                             std::array<const typename ATTR::value_type*, N> values,
                             int64_t valueCount, int64_t tupleSize)
    {
        return ATTR(times.data(), times.size(), values.data(), valueCount, tupleSize);
    }

    void testBool()
    {
        const IntAttribute invalidAttr;
        const IntAttribute intFalse(false);
        const IntAttribute intTrue(true);
        const IntAttribute intTrue2(-1);
        const FloatAttribute floatFalse(0.f);
        const FloatAttribute floatTrue(1.f);
        const DoubleAttribute doubleFalse(0.0);
        const DoubleAttribute doubleTrue(1.0);
        const StringAttribute stringAttr("true");

        const std::array<IntAttribute::value_type, 2> multiValue { true, false };
        const IntAttribute multiValueAttr(multiValue.data(), multiValue.size(), 1);

        const IntAttribute multiSampleAttr =
                makeMultiSampleAttr<IntAttribute, 2>({ -0.05f, 0.6f },
                                                     { multiValue.data(), multiValue.data() + 1 },
                                                     1, 1);

        // happy path
        CPPUNIT_ASSERT_EQUAL(false, util::rdl2Convert<bool_t>(intFalse));
        CPPUNIT_ASSERT_EQUAL(true, util::rdl2Convert<bool_t>(intTrue));
        CPPUNIT_ASSERT_EQUAL(true, util::rdl2Convert<bool_t>(intTrue2));
        CPPUNIT_ASSERT_EQUAL(false, util::rdl2Convert<bool_t>(floatFalse));
        CPPUNIT_ASSERT_EQUAL(true, util::rdl2Convert<bool_t>(floatTrue));
        CPPUNIT_ASSERT_EQUAL(false, util::rdl2Convert<bool_t>(doubleFalse));
        CPPUNIT_ASSERT_EQUAL(true, util::rdl2Convert<bool_t>(doubleTrue));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<bool_t>(intFalse, 0.f, util::TruncateBehavior::THROW));

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<bool_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<bool_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<bool_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(true, util::rdl2Convert<bool_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(false, util::rdl2Convert<bool_t>(multiSampleAttr, 0.5f));
    }

    void testInt()
    {
        constexpr int_t kIntMin = std::numeric_limits<int_t>::min();
        constexpr int_t kIntMax = std::numeric_limits<int_t>::max();

        const IntAttribute invalidAttr;
        const IntAttribute int1Attr(1);
        const IntAttribute intMinAttr(kIntMin);
        const IntAttribute intMaxAttr(kIntMax);
        const FloatAttribute floatAttr(1.1f);
        const DoubleAttribute doubleAttr(2.2);
        const StringAttribute stringAttr("1");
        const StringAttribute stringInvalidArgAttr("one");
        const StringAttribute stringOutOfRangeAttr(std::to_string(int64_t(kIntMax) + 1));

        const std::array<IntAttribute::value_type, 2> multiValue { 0, 1 };
        const IntAttribute multiValueAttr(multiValue.data(), multiValue.size(), 1);

        const IntAttribute multiSampleAttr =
                makeMultiSampleAttr<IntAttribute, 2>({ -0.05f, 0.6f },
                                                     { multiValue.data(), multiValue.data() + 1 },
                                                     1, 1);

        // happy path
        CPPUNIT_ASSERT_EQUAL(1, util::rdl2Convert<int_t>(int1Attr));
        CPPUNIT_ASSERT_EQUAL(kIntMin, util::rdl2Convert<int_t>(intMinAttr));
        CPPUNIT_ASSERT_EQUAL(kIntMax, util::rdl2Convert<int_t>(intMaxAttr));
        CPPUNIT_ASSERT_EQUAL(1, util::rdl2Convert<int_t>(stringAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<int_t>(int1Attr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(1, util::rdl2Convert<int_t>(floatAttr)); // warn
        CPPUNIT_ASSERT_EQUAL(2, util::rdl2Convert<int_t>(doubleAttr)); // warn
        CPPUNIT_ASSERT_EQUAL(1, util::rdl2Convert<int_t>(floatAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_EQUAL(2, util::rdl2Convert<int_t>(doubleAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<int_t>(floatAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<int_t>(doubleAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // stoi exceptions
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<int_t>(stringInvalidArgAttr), std::invalid_argument);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<int_t>(stringOutOfRangeAttr), std::out_of_range);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<int_t>(invalidAttr), util::InvalidAttributeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<int_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(0, util::rdl2Convert<int_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(1, util::rdl2Convert<int_t>(multiSampleAttr, 0.5f));
    }

    void testLong()
    {
        constexpr int_t kIntMin = std::numeric_limits<int_t>::min();
        constexpr int_t kIntMax = std::numeric_limits<int_t>::max();

        constexpr long_t kLongMin = std::numeric_limits<long_t>::min();
        constexpr long_t kLongMax = std::numeric_limits<long_t>::max();

        // Can't convert long min/max to floating point and back due to loss
        // of precision. Use numbers out of int range
        constexpr long_t kLongSmall = (long_t)std::numeric_limits<int_t>::min() * 2l;
        constexpr long_t kLongBig = (long_t)std::numeric_limits<int_t>::max() * 2l + 2;

        constexpr float_t kFloatSmall = kLongSmall;
        constexpr float_t kFloatBig = kLongBig;

        constexpr double_t kDoubleSmall = kLongSmall;
        constexpr double_t kDoubleBig = kLongBig;

        const std::string longMinStr = std::to_string(kLongMin);
        const std::string longMaxStr = std::to_string(kLongMax);

        const IntAttribute invalidAttr;
        const IntAttribute intMinAttr(kIntMin);
        const IntAttribute intMaxAttr(kIntMax);

        const FloatAttribute floatSmallAttr(kFloatSmall);
        const FloatAttribute floatBigAttr(kFloatBig);

        const DoubleAttribute doubleSmallAttr(kDoubleSmall);
        const DoubleAttribute doubleBigAttr(kDoubleBig);

        const StringAttribute strMinAttr(longMinStr);
        const StringAttribute strMaxAttr(longMaxStr);
        const StringAttribute stringInvalidArgAttr("one");

        const std::array<IntAttribute::value_type, 2> multiValue { 0, 1 };
        const IntAttribute multiValueAttr(multiValue.data(), multiValue.size(), 1);

        const IntAttribute multiSampleAttr =
                makeMultiSampleAttr<IntAttribute, 2>({ -0.05f, 0.6f },
                                                     { multiValue.data(), multiValue.data() + 1 },
                                                     1, 1);

        // happy path
        CPPUNIT_ASSERT_EQUAL((long_t)kIntMin, util::rdl2Convert<long_t>(intMinAttr));
        CPPUNIT_ASSERT_EQUAL((long_t)kIntMax, util::rdl2Convert<long_t>(intMaxAttr));
        CPPUNIT_ASSERT_EQUAL(kLongMin, util::rdl2Convert<long_t>(strMinAttr));
        CPPUNIT_ASSERT_EQUAL(kLongMax, util::rdl2Convert<long_t>(strMaxAttr));

        // truncation
        CPPUNIT_ASSERT_EQUAL((long_t)kFloatSmall, util::rdl2Convert<long_t>(floatSmallAttr));
        CPPUNIT_ASSERT_EQUAL((long_t)kFloatBig, util::rdl2Convert<long_t>(floatBigAttr));
        CPPUNIT_ASSERT_EQUAL((long_t)kDoubleSmall, util::rdl2Convert<long_t>(doubleSmallAttr));
        CPPUNIT_ASSERT_EQUAL((long_t)kDoubleBig, util::rdl2Convert<long_t>(doubleBigAttr));
        CPPUNIT_ASSERT_EQUAL((long_t)kFloatSmall, util::rdl2Convert<long_t>(floatSmallAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_EQUAL((long_t)kDoubleSmall, util::rdl2Convert<long_t>(doubleSmallAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<long_t>(intMaxAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<long_t>(floatSmallAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<long_t>(doubleSmallAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // stol exceptions
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<long_t>(stringInvalidArgAttr), std::invalid_argument);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<long_t>(invalidAttr), util::InvalidAttributeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<long_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL((long_t)0, util::rdl2Convert<long_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL((long_t)1, util::rdl2Convert<long_t>(multiSampleAttr, 0.5f));
    }

    void testFloat()
    {
        constexpr float_t kFltLow = std::numeric_limits<float_t>::lowest();
        constexpr float_t kFltMin = std::numeric_limits<float_t>::min();
        constexpr float_t kFltMax = std::numeric_limits<float_t>::max();

        const IntAttribute intAttr(5);

        const FloatAttribute invalidAttr;
        const FloatAttribute floatLowAttr(kFltLow);
        const FloatAttribute floatMinAttr(kFltMin);
        const FloatAttribute floatMaxAttr(kFltMax);

        const DoubleAttribute doubleLowAttr(kFltLow);
        const DoubleAttribute doubleMinAttr(kFltMin);
        const DoubleAttribute doubleMaxAttr(kFltMax);

        const StringAttribute strLowAttr(std::to_string(kFltLow));
        const StringAttribute strLaxAttr(std::to_string(kFltMax));
        const StringAttribute stringInvalidArgAttr("one point five");

        const std::array<FloatAttribute::value_type, 2> multiValue { 0.f, 1.f };
        const FloatAttribute multiValueAttr(multiValue.data(), multiValue.size(), 1);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({ -0.05f, 0.6f },
                                                       { multiValue.data(), multiValue.data() + 1 },
                                                       1, 1);

        // happy path
        CPPUNIT_ASSERT_EQUAL(5.f, util::rdl2Convert<float_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(kFltLow, util::rdl2Convert<float_t>(floatLowAttr));
        CPPUNIT_ASSERT_EQUAL(kFltMin, util::rdl2Convert<float_t>(floatMinAttr));
        CPPUNIT_ASSERT_EQUAL(kFltMax, util::rdl2Convert<float_t>(floatMaxAttr));
        CPPUNIT_ASSERT_EQUAL(kFltLow, util::rdl2Convert<float_t>(strLowAttr));
        CPPUNIT_ASSERT_EQUAL(kFltMax, util::rdl2Convert<float_t>(strLaxAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<float_t>(floatLowAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(kFltLow, util::rdl2Convert<float_t>(doubleLowAttr));
        CPPUNIT_ASSERT_EQUAL(kFltMin, util::rdl2Convert<float_t>(doubleMinAttr));
        CPPUNIT_ASSERT_EQUAL(kFltMax, util::rdl2Convert<float_t>(doubleMaxAttr));
        CPPUNIT_ASSERT_EQUAL(kFltLow, util::rdl2Convert<float_t>(doubleLowAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<float_t>(doubleLowAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // stof exceptions
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<float_t>(stringInvalidArgAttr), std::invalid_argument);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<float_t>(invalidAttr), util::InvalidAttributeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<float_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(0.f, util::rdl2Convert<float_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(1.f, util::rdl2Convert<float_t>(multiSampleAttr, 0.5f));
    }

    void testDouble()
    {
        constexpr float_t kFltLow = std::numeric_limits<float_t>::lowest();
        constexpr float_t kFltMin = std::numeric_limits<float_t>::min();
        constexpr float_t kFltMax = std::numeric_limits<float_t>::max();

        constexpr double_t kDblLow = std::numeric_limits<double_t>::lowest();
        constexpr double_t kDlbMin = std::numeric_limits<double_t>::min();
        constexpr double_t kDlbMax = std::numeric_limits<double_t>::max();

        const IntAttribute intAttr(0);

        const FloatAttribute floatLowAttr(kFltLow);
        const FloatAttribute floatMinAttr(kFltMin);
        const FloatAttribute floatMaxAttr(kFltMax);

        const DoubleAttribute invalidAttr;
        const DoubleAttribute doubleLowAttr(kDblLow);
        const DoubleAttribute doubleMinAttr(kDlbMin);
        const DoubleAttribute doubleMaxAttr(kDlbMax);

        const StringAttribute strLowAttr(std::to_string(kDblLow));
        const StringAttribute strLaxAttr(std::to_string(kDlbMax));
        const StringAttribute stringInvalidArgAttr("one point five");

        const std::array<DoubleAttribute::value_type, 2> multiValue { 0.0, 1.0 };
        const DoubleAttribute multiValueAttr(multiValue.data(), multiValue.size(), 1);

        const DoubleAttribute multiSampleAttr =
                makeMultiSampleAttr<DoubleAttribute, 2>({ -0.05f, 0.6f },
                                                        { multiValue.data(), multiValue.data() + 1 },
                                                        1, 1);

        // happy path
        CPPUNIT_ASSERT_EQUAL(0.0, util::rdl2Convert<double_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL((double_t)kFltLow, util::rdl2Convert<double_t>(floatLowAttr));
        CPPUNIT_ASSERT_EQUAL((double_t)kFltMin, util::rdl2Convert<double_t>(floatMinAttr));
        CPPUNIT_ASSERT_EQUAL((double_t)kFltMax, util::rdl2Convert<double_t>(floatMaxAttr));
        CPPUNIT_ASSERT_EQUAL(kDblLow, util::rdl2Convert<double_t>(doubleLowAttr));
        CPPUNIT_ASSERT_EQUAL(kDlbMin, util::rdl2Convert<double_t>(doubleMinAttr));
        CPPUNIT_ASSERT_EQUAL(kDlbMax, util::rdl2Convert<double_t>(doubleMaxAttr));
        CPPUNIT_ASSERT_EQUAL(kDblLow, util::rdl2Convert<double_t>(strLowAttr));
        CPPUNIT_ASSERT_EQUAL(kDlbMax, util::rdl2Convert<double_t>(strLaxAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<double_t>(doubleLowAttr, 0.f, util::TruncateBehavior::THROW));

        // stod exceptions
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<double_t>(stringInvalidArgAttr), std::invalid_argument);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<double_t>(invalidAttr), util::InvalidAttributeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<double_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(0.0, util::rdl2Convert<double_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(1.0, util::rdl2Convert<double_t>(multiSampleAttr, 0.5f));
    }

    void testString()
    {
        const int_t intVal = 1;
        const float_t fltVal = 1.f;
        const double_t dblVal = 1.0;
        const string_t strVal = "one";
        const string_t strFoo = "foo";
        const string_t strBar = "bar";

        const IntAttribute intAttr(intVal);
        const FloatAttribute fltAttr(fltVal);
        const DoubleAttribute dblAttr(dblVal);
        const StringAttribute strAttr(strVal);
        const StringAttribute invalidAttr;

        std::array<const char*, 2> multiValue { strFoo.c_str(), strBar.c_str() };
        const StringAttribute multiValueAttr(multiValue.data(), multiValue.size(), 1);

        const std::array<float, 2> times { -0.05f, 0.6f };
        std::array<const char**, 2> values { &multiValue[0], &multiValue[1] };
        const StringAttribute multiSampleAttr(times.data(), 2, values.data(), 1, 1);

        // happy path
        CPPUNIT_ASSERT_EQUAL(std::to_string(intVal), util::rdl2Convert<string_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(std::to_string(fltVal), util::rdl2Convert<string_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(std::to_string(dblVal), util::rdl2Convert<string_t>(dblAttr));
        CPPUNIT_ASSERT_EQUAL(strVal, util::rdl2Convert<string_t>(strAttr));

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<string_t>(invalidAttr), util::InvalidAttributeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<string_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(strFoo, util::rdl2Convert<string_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(strBar, util::rdl2Convert<string_t>(multiSampleAttr, 0.5f));
    }

    void testRgb()
    {
        const rgb_t rgbVal   { 0.5f, 1.f, 0.f };
        const rgb_t rgbVal2   { 0.4f, 0.9f, 0.1f };
        const rgba_t rgbaVal { 0.5f, 1.f, 0.f, 1.f};
        const std::array<double_t, 3> rgbDblArr { 0.5, 1.0, 0.0 };

        const rgb_t rgbIntVal { 255.f, 255.f, 0.f };
        const std::array<int_t, 3> rgbIntArr { 255, 255, 0 };

        const FloatAttribute rgbAttr(&rgbVal[0], 3, 3);
        const FloatAttribute rgbaAttr(&rgbaVal[0], 4, 4);
        const DoubleAttribute rgbDblAttr(rgbDblArr.data(), 3, 3);
        const IntAttribute rgbIntAttr(rgbIntArr.data(), 3, 3);
        const FloatAttribute invalidAttr;

        const StringAttribute stringAttr({"red", "white", "blue"}, 3);

        std::array<rgb_t, 2> multiValue { rgbVal, rgbVal2 };
        const FloatAttribute multiValueAttr(&multiValue[0][0], 6, 3);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({-0.05, 0.6},
                                                       {&rgbVal[0], &rgbVal2[0]},
                                                       3, 3);

        // happy path
        CPPUNIT_ASSERT_EQUAL(rgbVal, util::rdl2Convert<rgb_t>(rgbAttr));
        CPPUNIT_ASSERT_EQUAL(rgbIntVal, util::rdl2Convert<rgb_t>(rgbIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<rgb_t>(rgbAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(rgbVal, util::rdl2Convert<rgb_t>(rgbDblAttr));
        CPPUNIT_ASSERT_EQUAL(rgbVal, util::rdl2Convert<rgb_t>(rgbDblAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgb_t>(rgbDblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgb_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgb_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgb_t>(rgbaAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgb_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(rgbVal, util::rdl2Convert<rgb_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(rgbVal2, util::rdl2Convert<rgb_t>(multiSampleAttr, 0.5f));
    }

    void testRgba()
    {
        const rgb_t rgbVal   { 0.5f, 1.f, 0.f };
        const rgba_t rgbaVal { 0.5f, 1.f, 0.f, 1.f};
        const rgba_t rgbaVal2 { 0.4f,0.9f, 0.1f, 1.f};
        const std::array<double_t, 4> rgbaDblArr { 0.5, 1.0, 0.0, 1.0 };

        const rgba_t rgbaIntVal { 255.f, 255.f, 0.f, 255.f };
        const std::array<int_t, 4> rgbaIntArr { 255, 255, 0, 255 };

        const FloatAttribute rgbAttr(&rgbVal[0], 3, 3);
        const FloatAttribute rgbaAttr(&rgbaVal[0], 4, 4);
        const DoubleAttribute rgbaDblAttr(rgbaDblArr.data(), 4, 4);
        const IntAttribute rgbaIntAttr(rgbaIntArr.data(), 4, 4);
        const FloatAttribute invalidAttr;

        const StringAttribute stringAttr({"red", "white", "blue", "green"}, 4);

        std::array<rgba_t, 2> multiValue { rgbaVal, rgbaVal2 };
        const FloatAttribute multiValueAttr(&multiValue[0][0], 8, 4);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({-0.05, 0.6},
                                                       {&rgbaVal[0], &rgbaVal2[0]},
                                                       4, 4);

        // happy path
        CPPUNIT_ASSERT_EQUAL(rgbaVal, util::rdl2Convert<rgba_t>(rgbaAttr));
        CPPUNIT_ASSERT_EQUAL(rgbaIntVal, util::rdl2Convert<rgba_t>(rgbaIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<rgba_t>(rgbaAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(rgbaVal, util::rdl2Convert<rgba_t>(rgbaDblAttr));
        CPPUNIT_ASSERT_EQUAL(rgbaVal, util::rdl2Convert<rgba_t>(rgbaDblAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgba_t>(rgbaDblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgba_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgba_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgba_t>(rgbAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgba_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(rgbaVal, util::rdl2Convert<rgba_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(rgbaVal2, util::rdl2Convert<rgba_t>(multiSampleAttr, 0.5f));
    }

    void testVec2f()
    {
        const vec2f_t vec2fVal(0.5f, 1.0f);
        const vec2f_t vec2fVal2(0.8f, 0.9f);
        const vec2d_t vec2dVal(0.5, 1.0);
        const vec3f_t vec3fVal(0.5f, 1.0f, 1.0f);
        const vec2f_t vec2fIntVal(5.f, -7.f);
        const std::array<int, 2> vec2fIntArr {5, -7};

        const IntAttribute vec2fIntAttr(vec2fIntArr.data(), 2, 2);
        const FloatAttribute vec2fAttr(&vec2fVal[0], 2, 2);
        const DoubleAttribute vec2dAttr(&vec2dVal[0], 2, 2);
        const FloatAttribute vec3fAttr(&vec3fVal[0], 3, 3);
        const FloatAttribute invalidAttr;
        const StringAttribute stringAttr({"one", "two"}, 2);

        std::array<vec2f_t, 2> multiValue { vec2fVal, vec2fVal2 };
        const FloatAttribute multiValueAttr(&multiValue[0][0], 4, 2);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({-0.05, 0.6},
                                                       {&vec2fVal[0], &vec2fVal2[0]},
                                                       2, 2);

        // happy path
        CPPUNIT_ASSERT_EQUAL(vec2fVal, util::rdl2Convert<vec2f_t>(vec2fAttr));
        CPPUNIT_ASSERT_EQUAL(vec2fIntVal, util::rdl2Convert<vec2f_t>(vec2fIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec2f_t>(vec2fAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(vec2fVal, util::rdl2Convert<vec2f_t>(vec2dAttr));
        CPPUNIT_ASSERT_EQUAL(vec2fVal, util::rdl2Convert<vec2f_t>(vec2dAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2f_t>(vec2dAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2f_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2f_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2f_t>(vec3fAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2f_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(vec2fVal, util::rdl2Convert<vec2f_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(vec2fVal2, util::rdl2Convert<vec2f_t>(multiSampleAttr, 0.5f));
    }

    void testVec2d()
    {
        const vec2f_t vec2fVal(0.5f, 1.0f);
        const vec2d_t vec2dVal(0.5, 1.0);
        const vec2d_t vec2dVal2(0.8, 0.9);
        const vec3d_t vec3dVal(0.5, 1.0, 1.0);
        const vec2d_t vec2dIntVal(5.0, -7.0);
        const std::array<int, 2> vec2dIntArr {5, -7};

        const IntAttribute vec2dIntAttr(vec2dIntArr.data(), 2, 2);
        const FloatAttribute vec2fAttr(&vec2fVal[0], 2, 2);
        const DoubleAttribute vec2dAttr(&vec2dVal[0], 2, 2);
        const DoubleAttribute vec3dAttr(&vec3dVal[0], 3, 3);
        const DoubleAttribute invalidAttr;

        const StringAttribute stringAttr({ "foo", "bar" }, 2);

        std::array<vec2d_t, 2> multiValue { vec2dVal, vec2dVal2 };
        const DoubleAttribute multiValueAttr(&multiValue[0][0], 4, 2);

        const DoubleAttribute multiSampleAttr =
                makeMultiSampleAttr<DoubleAttribute, 2>({-0.05, 0.6},
                                                        {&vec2dVal[0], &vec2dVal2[0]},
                                                        2, 2);

        // happy path
        CPPUNIT_ASSERT_EQUAL(vec2dIntVal, util::rdl2Convert<vec2d_t>(vec2dIntAttr));
        CPPUNIT_ASSERT_EQUAL(vec2dVal, util::rdl2Convert<vec2d_t>(vec2fAttr));
        CPPUNIT_ASSERT_EQUAL(vec2dVal, util::rdl2Convert<vec2d_t>(vec2dAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec2d_t>(vec2dAttr, 0.f, util::TruncateBehavior::THROW));

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2d_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2d_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2d_t>(vec3dAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2d_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(vec2dVal, util::rdl2Convert<vec2d_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(vec2dVal2, util::rdl2Convert<vec2d_t>(multiSampleAttr, 0.5f));
    }

    void testVec3f()
    {
        const vec3f_t vec3fVal(0.5f, 1.0f, 1.0f);
        const vec3f_t vec3fVal2(0.8f, 0.9f, 0.1f);
        const vec2f_t vec2fVal(0.5f, 1.0f);
        const vec3d_t vec3dVal(0.5, 1.0, 1.0);
        const vec3f_t vec3fIntVal(5.f, -7.f, 1.f);
        const std::array<int, 3> vec3fIntArr {5, -7, 1};

        const IntAttribute vec3fIntAttr(vec3fIntArr.data(), 3, 3);
        const FloatAttribute vec3fAttr(&vec3fVal[0], 3, 3);
        const FloatAttribute vec2fAttr(&vec2fVal[0], 2, 2);
        const DoubleAttribute vec3dAttr(&vec3dVal[0], 3, 3);
        const FloatAttribute invalidAttr;
        const StringAttribute stringAttr({ "foo", "bar", "baz" }, 3);

        std::array<vec3f_t, 2> multiValue { vec3fVal, vec3fVal2 };
        const FloatAttribute multiValueAttr(&multiValue[0][0], 6, 3);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({-0.05, 0.6},
                                                       {&vec3fVal[0], &vec3fVal2[0]},
                                                       3, 3);

        // happy path
        CPPUNIT_ASSERT_EQUAL(vec3fVal, util::rdl2Convert<vec3f_t>(vec3fAttr));
        CPPUNIT_ASSERT_EQUAL(vec3fIntVal, util::rdl2Convert<vec3f_t>(vec3fIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec3f_t>(vec3fAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(vec3fVal, util::rdl2Convert<vec3f_t>(vec3dAttr));
        CPPUNIT_ASSERT_EQUAL(vec3fVal, util::rdl2Convert<vec3f_t>(vec3dAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3f_t>(vec3dAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3f_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3f_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3f_t>(vec2fAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3f_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(vec3fVal, util::rdl2Convert<vec3f_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(vec3fVal2, util::rdl2Convert<vec3f_t>(multiSampleAttr, 0.5f));
    }

    void testVec3d()
    {
        const vec3d_t vec3dVal(0.5, 1.0, 1.0);
        const vec3d_t vec3dVal2(0.8, 0.9, 0.1);
        const vec2d_t vec2dVal(0.5, 1.0);
        const vec3f_t vec3fVal(0.5f, 1.0f, 1.0f);
        const vec3d_t vec3dIntVal(5.0, -7.0, 1.0);
        const std::array<int, 3> vec3dIntArr {5, -7, 1};

        const IntAttribute vec3dIntAttr(vec3dIntArr.data(), 3, 3);
        const DoubleAttribute vec3dAttr(&vec3dVal[0], 3, 3);
        const DoubleAttribute vec2dAttr(&vec2dVal[0], 2, 2);
        const FloatAttribute  vec3fAttr(&vec3fVal[0], 3, 3);
        const StringAttribute stringAttr({ "foo", "bar", "baz" }, 3);
        const DoubleAttribute invalidAttr;

        std::array<vec3d_t, 2> multiValue { vec3dVal, vec3dVal2 };
        const DoubleAttribute multiValueAttr(&multiValue[0][0], 6, 3);

        const DoubleAttribute multiSampleAttr =
                makeMultiSampleAttr<DoubleAttribute, 2>({-0.05, 0.6},
                                                        {&vec3dVal[0], &vec3dVal2[0]},
                                                        3, 3);

        // happy path
        CPPUNIT_ASSERT_EQUAL(vec3dVal, util::rdl2Convert<vec3d_t>(vec3dAttr));
        CPPUNIT_ASSERT_EQUAL(vec3dVal, util::rdl2Convert<vec3d_t>(vec3fAttr));
        CPPUNIT_ASSERT_EQUAL(vec3dIntVal, util::rdl2Convert<vec3d_t>(vec3dIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec3d_t>(vec3dAttr, 0.f, util::TruncateBehavior::THROW));

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3d_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3d_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3d_t>(vec2dAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3d_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(vec3dVal, util::rdl2Convert<vec3d_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(vec3dVal2, util::rdl2Convert<vec3d_t>(multiSampleAttr, 0.5f));
    }

    void testVec4f()
    {
        const vec4f_t vec4fVal(0.5f, 1.f, 1.f, 1.f);
        const vec4f_t vec4fVal2(0.8f, 0.9f, 0.1f, 1.f);
        const vec3f_t vec3fVal(0.5f, 1.0f, 0.1f);
        const vec4d_t vec4dVal(0.5, 1.0, 1.0, 1.0);
        const vec4f_t vec4fIntVal(5.f, -7.f, 1.f, 0.f);
        const std::array<int, 4> vec4fIntArr {5, -7, 1, 0};

        const IntAttribute vec4fIntAttr(vec4fIntArr.data(), 4, 4);
        const FloatAttribute vec4fAttr(&vec4fVal[0], 4, 4);
        const FloatAttribute vec3fAttr(&vec3fVal[0], 3, 3);
        const DoubleAttribute vec4dAttr(&vec4dVal[0], 4, 4);
        const FloatAttribute invalidAttr;
        const StringAttribute stringAttr({ "foo", "bar", "baz", "dwa" }, 4);

        std::array<vec4f_t, 2> multiValue { vec4fVal, vec4fVal2 };
        const FloatAttribute multiValueAttr(&multiValue[0][0], 8, 4);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({-0.05, 0.6},
                                                       {&vec4fVal[0], &vec4fVal2[0]},
                                                       4, 4);

        // happy path
        CPPUNIT_ASSERT_EQUAL(vec4fVal, util::rdl2Convert<vec4f_t>(vec4fAttr));
        CPPUNIT_ASSERT_EQUAL(vec4fIntVal, util::rdl2Convert<vec4f_t>(vec4fIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec4f_t>(vec4fAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(vec4fVal, util::rdl2Convert<vec4f_t>(vec4dAttr));
        CPPUNIT_ASSERT_EQUAL(vec4fVal, util::rdl2Convert<vec4f_t>(vec4dAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4f_t>(vec4dAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4f_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4f_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4f_t>(vec3fAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4f_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(vec4fVal, util::rdl2Convert<vec4f_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(vec4fVal2, util::rdl2Convert<vec4f_t>(multiSampleAttr, 0.5f));
    }

    void testVec4d()
    {
        const vec4d_t vec4dVal(0.5, 1.0, 1.0, 0.0);
        const vec4d_t vec4dVal2(0.8, 0.9, 0.1, 0.3);
        const vec3d_t vec3dVal(0.5, 1.0, 0.1);
        const vec4f_t vec4fVal(0.5f, 1.f, 1.f, 0.f);
        const vec4d_t vec4dIntVal(5.0, -7.0, 1.0, 0.0);
        const std::array<int, 4> vec4dIntArr {5, -7, 1, 0};

        const IntAttribute vec4dIntAttr(vec4dIntArr.data(), 4, 4);
        const DoubleAttribute vec4dAttr(&vec4dVal[0], 4, 4);
        const DoubleAttribute vec3dAttr(&vec3dVal[0], 3, 3);
        const FloatAttribute  vec4fAttr(&vec4fVal[0], 4, 4);
        const StringAttribute stringAttr({ "foo", "bar", "baz", "dwa" }, 4);
        const DoubleAttribute invalidAttr;

        std::array<vec4d_t, 2> multiValue { vec4dVal, vec4dVal2 };
        const DoubleAttribute multiValueAttr(&multiValue[0][0], 8, 4);

        const DoubleAttribute multiSampleAttr =
                makeMultiSampleAttr<DoubleAttribute, 2>({-0.05, 0.6},
                                                        {&vec4dVal[0], &vec4dVal2[0]},
                                                        4, 4);

        // happy path
        CPPUNIT_ASSERT_EQUAL(vec4dVal, util::rdl2Convert<vec4d_t>(vec4dAttr));
        CPPUNIT_ASSERT_EQUAL(vec4dVal, util::rdl2Convert<vec4d_t>(vec4fAttr));
        CPPUNIT_ASSERT_EQUAL(vec4dIntVal, util::rdl2Convert<vec4d_t>(vec4dIntAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec4d_t>(vec4dAttr, 0.f, util::TruncateBehavior::THROW));

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4d_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4d_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4d_t>(vec3dAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4d_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(vec4dVal, util::rdl2Convert<vec4d_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(vec4dVal2, util::rdl2Convert<vec4d_t>(multiSampleAttr, 0.5f));
    }

    void testMat4f()
    {
        using mat4i_t = arras::math::Mat4<arras::math::Vec4<arras::rdl2::Int>>;

        const mat4f_t mat4fVal(1.f, 0.f, 0.f, 0.f,
                               0.f, 1.f, 0.f, 0.f,
                               0.f, 0.f, 1.f, 0.f,
                               0.f, 0.f, 0.f, 1.f);
        const mat4f_t mat4fVal2(1.f, 1.f, 1.f, 1.f,
                                0.f, 1.f, 0.f, 0.f,
                                0.f, 0.f, 1.f, 0.f,
                                0.f, 0.f, 0.f, 1.f);
        const mat4d_t mat4dVal(1.0, 0.0, 0.0, 0.0,
                               0.0, 1.0, 0.0, 0.0,
                               0.0, 0.0, 1.0, 0.0,
                               0.0, 0.0, 0.0, 1.0);
        const mat4i_t mat4iVal(1, 0, 0, 0,
                               0, 1, 0, 0,
                               0, 0, 1, 0,
                               0, 0, 0, 1);
        const vec4f_t vec4fVal(0.5f, 1.f, 1.f, 1.f);

        const FloatAttribute mat4fAttr(&mat4fVal[0][0], 16, 16);
        const FloatAttribute mat4fAttr2(&mat4fVal2[0][0], 16, 16);
        const DoubleAttribute mat4dAttr(&mat4dVal[0][0], 16, 16);
        const IntAttribute mat4iAttr(&mat4iVal[0][0], 16, 16);
        const FloatAttribute vec4fAttr(&vec4fVal[0], 4, 4);
        const FloatAttribute invalidAttr;
        const StringAttribute stringAttr({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p"}, 16);

        std::array<mat4f_t, 2> multiValue { mat4fVal, mat4fVal2 };
        const FloatAttribute multiValueAttr(&multiValue[0][0][0], 32, 16);

        const FloatAttribute multiSampleAttr =
                makeMultiSampleAttr<FloatAttribute, 2>({-0.05, 0.6},
                                                        {&mat4fVal[0][0], &mat4fVal2[0][0]},
                                                        16, 16);

        // happy path
        CPPUNIT_ASSERT_EQUAL(mat4fVal, util::rdl2Convert<mat4f_t>(mat4fAttr));
        CPPUNIT_ASSERT_EQUAL(mat4fVal, util::rdl2Convert<mat4f_t>(mat4iAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<mat4f_t>(mat4fAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_EQUAL(mat4fVal, util::rdl2Convert<mat4f_t>(mat4dAttr));
        CPPUNIT_ASSERT_EQUAL(mat4fVal, util::rdl2Convert<mat4f_t>(mat4dAttr, 0.f, util::TruncateBehavior::IGNORE));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4f_t>(mat4dAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4f_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4f_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4f_t>(vec4fAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4f_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(mat4fVal, util::rdl2Convert<mat4f_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(mat4fVal2, util::rdl2Convert<mat4f_t>(multiSampleAttr, 0.5f));
    }

    void testMat4d()
    {
        using mat4i_t = arras::math::Mat4<arras::math::Vec4<arras::rdl2::Int>>;

        const mat4d_t mat4dVal(1.0, 0.0, 0.0, 0.0,
                               0.0, 1.0, 0.0, 0.0,
                               0.0, 0.0, 1.0, 0.0,
                               0.0, 0.0, 0.0, 1.0);
        const mat4d_t mat4dVal2(1.0, 1.0, 1.0, 1.0,
                                0.0, 1.0, 0.0, 0.0,
                                0.0, 0.0, 1.0, 0.0,
                                0.0, 0.0, 0.0, 1.0);
        const mat4f_t mat4fVal(1.f, 0.f, 0.f, 0.f,
                               0.f, 1.f, 0.f, 0.f,
                               0.f, 0.f, 1.f, 0.f,
                               0.f, 0.f, 0.f, 1.f);
        const mat4i_t mat4iVal(1, 0, 0, 0,
                               0, 1, 0, 0,
                               0, 0, 1, 0,
                               0, 0, 0, 1);
        const vec4d_t vec4dVal(0.5, 1.0, 1.0, 1.0);

        const DoubleAttribute mat4dAttr(&mat4dVal[0][0], 16, 16);
        const DoubleAttribute mat4dAttr2(&mat4dVal2[0][0], 16, 16);
        const FloatAttribute mat4fAttr(&mat4fVal[0][0], 16, 16);
        const IntAttribute mat4iAttr(&mat4iVal[0][0], 16, 16);
        const DoubleAttribute vec4dAttr(&vec4dVal[0], 4, 4);
        const DoubleAttribute invalidAttr;
        const StringAttribute stringAttr({"a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p"}, 16);

        std::array<mat4d_t, 2> multiValue { mat4dVal, mat4dVal2 };
        const DoubleAttribute multiValueAttr(&multiValue[0][0][0], 32, 16);

        const DoubleAttribute multiSampleAttr =
                makeMultiSampleAttr<DoubleAttribute, 2>({-0.05, 0.6},
                                                        {&mat4dVal[0][0], &mat4dVal2[0][0]},
                                                        16, 16);

        // happy path
        CPPUNIT_ASSERT_EQUAL(mat4dVal, util::rdl2Convert<mat4d_t>(mat4dAttr));
        CPPUNIT_ASSERT_EQUAL(mat4dVal, util::rdl2Convert<mat4d_t>(mat4fAttr));
        CPPUNIT_ASSERT_EQUAL(mat4dVal, util::rdl2Convert<mat4d_t>(mat4iAttr));

        // truncation
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<mat4d_t>(mat4dAttr, 0.f, util::TruncateBehavior::THROW));

        // invalid attr
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4d_t>(invalidAttr), util::InvalidAttributeError);

        // invalid type
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4d_t>(stringAttr), util::AttributeTypeError);

        // invalid size
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4d_t>(vec4dAttr), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4d_t>(multiValueAttr), util::AttributeDataError);

        // multiple time samples
        CPPUNIT_ASSERT_EQUAL(mat4dVal, util::rdl2Convert<mat4d_t>(multiSampleAttr, 0.f));
        CPPUNIT_ASSERT_EQUAL(mat4dVal2, util::rdl2Convert<mat4d_t>(multiSampleAttr, 0.5f));
    }

    void testBoolVector()
    {
        const boolvector_t boolVec { true, false, true };
        const std::array<int_t, 3> intArr {1, 0, 1};
        const std::array<float_t, 3> fltArr {1.f, 0.f, 2.f};
        const std::array<double_t, 3> dblArr {2.7, 0.0, 1.5};

        const IntAttribute intAttr(intArr.data(), 3, 1);
        const FloatAttribute fltAttr(fltArr.data(), 3, 1);
        const DoubleAttribute dblAttr(dblArr.data(), 3, 1);
        const IntAttribute invalidAttr;
        const StringAttribute strAttr({"true", "false", "true"});

        CPPUNIT_ASSERT_EQUAL(boolVec, util::rdl2Convert<boolvector_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(boolVec, util::rdl2Convert<boolvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(boolVec, util::rdl2Convert<boolvector_t>(dblAttr));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<boolvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<boolvector_t>(strAttr), util::AttributeTypeError);
    }

    void testIntVector()
    {
        const intvector_t intVec { 0, -5, 9 };
        const std::array<float_t, 3> fltArr { 0.3f, -5.2f, 9.1f };
        const std::array<double_t, 3> dblArr { 0.9, -5.1, 9.8 };

        const IntAttribute intAttr(intVec.data(), 3, 1);
        const FloatAttribute fltAttr(fltArr.data(), 3, 1);
        const DoubleAttribute dblAttr(dblArr.data(), 3, 1);
        const IntAttribute invalidAttr;
        const StringAttribute strAttr({"0", "-5", "9"});

        CPPUNIT_ASSERT_EQUAL(intVec, util::rdl2Convert<intvector_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(intVec, util::rdl2Convert<intvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(intVec, util::rdl2Convert<intvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<intvector_t>(intAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<intvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<intvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<intvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<intvector_t>(strAttr), util::AttributeTypeError);
    }

    void testLongVector()
    {
        constexpr long_t kLongSmall = (long_t)std::numeric_limits<int_t>::min() * 2l;
        constexpr long_t kLongBig = (long_t)std::numeric_limits<int_t>::max() * 2l + 2l;

        const longvector_t longVec { kLongSmall, kLongBig, 9l };
        const std::array<float_t, 3> fltArr { (float)kLongSmall, (float)kLongBig, 9.f };
        const std::array<double_t, 3> dblArr { (double)kLongSmall, (double)kLongBig, 9.0 };

        const intvector_t intVec { 0, -5, 9 };
        const longvector_t longIntVec { 0l, -5l, 9l };

        const IntAttribute intAttr(intVec.data(), 3, 1);
        const FloatAttribute fltAttr(fltArr.data(), 3, 1);
        const DoubleAttribute dblAttr(dblArr.data(), 3, 1);
        const IntAttribute invalidAttr;
        const StringAttribute strAttr({"0", "-5", "9"});

        CPPUNIT_ASSERT_EQUAL(longIntVec, util::rdl2Convert<longvector_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(longVec, util::rdl2Convert<longvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(longVec, util::rdl2Convert<longvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<longvector_t>(intAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<longvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<longvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<longvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<longvector_t>(strAttr), util::AttributeTypeError);
    }

    void testFloatVector()
    {
        const intvector_t intVec { 0, -5, 9 };
        const floatvector_t floatVec { 0.f, -5.f, 9.f };
        const doublevector_t doubleVec { 0.0, -5.0, 9.0 };

        const IntAttribute intAttr(intVec.data(), intVec.size(), 1);
        const FloatAttribute fltAttr(floatVec.data(), floatVec.size(), 1);
        const DoubleAttribute dblAttr(doubleVec.data(), doubleVec.size(), 1);
        const FloatAttribute invalidAttr;
        const StringAttribute strAttr({"0.0", "-5.0", "9.0"});

        CPPUNIT_ASSERT_EQUAL(floatVec, util::rdl2Convert<floatvector_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(floatVec, util::rdl2Convert<floatvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(floatVec, util::rdl2Convert<floatvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<floatvector_t>(intAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<floatvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<floatvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<floatvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<floatvector_t>(strAttr), util::AttributeTypeError);
    }

    void testDoubleVector()
    {
        const intvector_t intVec { 0, -5, 9 };
        const floatvector_t floatVec { 0.f, -5.f, 9.f };
        const doublevector_t doubleVec { 0.0, -5.0, 9.0 };

        const IntAttribute intAttr(intVec.data(), intVec.size(), 1);
        const FloatAttribute fltAttr(floatVec.data(), floatVec.size(), 1);
        const DoubleAttribute dblAttr(doubleVec.data(), doubleVec.size(), 1);
        const DoubleAttribute invalidAttr;
        const StringAttribute strAttr({"0.0", "-5.0", "9.0"});

        CPPUNIT_ASSERT_EQUAL(doubleVec, util::rdl2Convert<doublevector_t>(intAttr));
        CPPUNIT_ASSERT_EQUAL(doubleVec, util::rdl2Convert<doublevector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(doubleVec, util::rdl2Convert<doublevector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<doublevector_t>(intAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<doublevector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<doublevector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<doublevector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<doublevector_t>(strAttr), util::AttributeTypeError);
    }

    void testStringVector()
    {
        const intvector_t intVec { 0, -5, 9 };
        const floatvector_t floatVec { 0.f, -5.f, 9.f };
        const doublevector_t doubleVec { 0.0, -5.0, 9.0 };
        const stringvector_t stringVec {"0.0", "-5.0", "9.0"};

        const IntAttribute intAttr(intVec.data(), intVec.size(), 1);
        const FloatAttribute fltAttr(floatVec.data(), floatVec.size(), 1);
        const DoubleAttribute dblAttr(doubleVec.data(), doubleVec.size(), 1);
        const StringAttribute invalidAttr;
        const StringAttribute strAttr(stringVec);

        CPPUNIT_ASSERT_EQUAL(stringVec, util::rdl2Convert<stringvector_t>(strAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<stringvector_t>(strAttr, 0.f, util::TruncateBehavior::THROW));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<stringvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<stringvector_t>(intAttr), util::AttributeTypeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<stringvector_t>(fltAttr), util::AttributeTypeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<stringvector_t>(dblAttr), util::AttributeTypeError);
    }

    void testRgbVector()
    {
        const rgbvector_t rgbVec { { 0.f, 0.5f, 1.f }, { 1.f, 0.5f, 0.f }, { 0.f, 0.f, 1.f } };
        const doublevector_t doubleVec { 0.0, 0.5, 1.0, 1.0, 0.5, 0.0, 0.0, 0.0, 1.0 };

        const FloatAttribute fltAttr(&rgbVec.front()[0], 9, 3);
        const FloatAttribute fltWrongTupleSize(&rgbVec.front()[0], 8, 4);
        const FloatAttribute fltWrongSize(&rgbVec.front()[0], 8, 3);
        const DoubleAttribute dblAttr(doubleVec.data(), 9, 3);
        const FloatAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(rgbVec, util::rdl2Convert<rgbvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(rgbVec, util::rdl2Convert<rgbvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<rgbvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbvector_t>(fltWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbvector_t>(fltWrongSize), util::AttributeDataError);
    }

    void testRgbaVector()
    {
        const rgbavector_t rgbaVec { { 0.f, 0.5f, 1.f, 1.f }, { 1.f, 0.5f, 0.f, 1.f }, { 0.f, 0.f, 1.f, 1.f } };
        const doublevector_t doubleVec { 0.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0 };

        const FloatAttribute fltAttr(&rgbaVec.front()[0], 12, 4);
        const FloatAttribute fltWrongTupleSize(&rgbaVec.front()[0], 12, 3);
        const FloatAttribute fltWrongSize(&rgbaVec.front()[0], 11, 4);
        const DoubleAttribute dblAttr(doubleVec.data(), 12, 4);
        const FloatAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(rgbaVec, util::rdl2Convert<rgbavector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(rgbaVec, util::rdl2Convert<rgbavector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<rgbavector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbavector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbavector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbavector_t>(fltWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<rgbavector_t>(fltWrongSize), util::AttributeDataError);
    }

    void testVec2fVector()
    {
        vec2fvector_t vec2fVec;
        vec2fVec.emplace_back(0.f, 0.5f);
        vec2fVec.emplace_back(1.f, 0.5f);
        vec2fVec.emplace_back(0.f, 0.f);

        vec2dvector_t vec2dVec;
        vec2dVec.emplace_back(0.0, 0.5);
        vec2dVec.emplace_back(1.0, 0.5);
        vec2dVec.emplace_back(0.0, 0.0);

        const FloatAttribute fltAttr(&vec2fVec.front()[0], 6, 2);
        const FloatAttribute fltWrongTupleSize(&vec2fVec.front()[0], 6, 3);
        const FloatAttribute fltWrongSize(&vec2fVec.front()[0], 5, 2);
        const DoubleAttribute dblAttr(&vec2dVec.front()[0], 6, 2);
        const FloatAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(vec2fVec, util::rdl2Convert<vec2fvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(vec2fVec, util::rdl2Convert<vec2fvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec2fvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2fvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2fvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2fvector_t>(fltWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2fvector_t>(fltWrongSize), util::AttributeDataError);
    }

    void testVec2dVector()
    {
        vec2fvector_t vec2fVec;
        vec2fVec.emplace_back(0.f, 0.5f);
        vec2fVec.emplace_back(1.f, 0.5f);
        vec2fVec.emplace_back(0.f, 0.f);

        vec2dvector_t vec2dVec;
        vec2dVec.emplace_back(0.0, 0.5);
        vec2dVec.emplace_back(1.0, 0.5);
        vec2dVec.emplace_back(0.0, 0.0);

        const FloatAttribute fltAttr(&vec2fVec.front()[0], 6, 2);
        const DoubleAttribute dblAttr(&vec2dVec.front()[0], 6, 2);
        const DoubleAttribute dblWrongTupleSize(&vec2dVec.front()[0], 6, 3);
        const DoubleAttribute dblWrongSize(&vec2dVec.front()[0], 5, 2);
        const DoubleAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(vec2dVec, util::rdl2Convert<vec2dvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(vec2dVec, util::rdl2Convert<vec2dvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec2dvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec2dvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2dvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2dvector_t>(dblWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec2dvector_t>(dblWrongSize), util::AttributeDataError);
    }

    void testVec3fVector()
    {
        vec3fvector_t vec3fVec;
        vec3fVec.emplace_back(0.f, 0.5f, 1.f);
        vec3fVec.emplace_back(1.f, 0.5f, 0.f);
        vec3fVec.emplace_back(0.f, 0.f, 1.f);

        vec3dvector_t vec3dVec;
        vec3dVec.emplace_back(0.0, 0.5, 1.0);
        vec3dVec.emplace_back(1.0, 0.5, 0.0);
        vec3dVec.emplace_back(0.0, 0.0, 1.0);

        const FloatAttribute fltAttr(&vec3fVec.front()[0], 9, 3);
        const FloatAttribute fltWrongTupleSize(&vec3fVec.front()[0], 8, 4);
        const FloatAttribute fltWrongSize(&vec3fVec.front()[0], 8, 3);
        const DoubleAttribute dblAttr(&vec3dVec.front()[0], 9, 3);
        const FloatAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(vec3fVec, util::rdl2Convert<vec3fvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(vec3fVec, util::rdl2Convert<vec3fvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec3fvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3fvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3fvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3fvector_t>(fltWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3fvector_t>(fltWrongSize), util::AttributeDataError);
    }

    void testVec3dVector()
    {
        vec3fvector_t vec3fVec;
        vec3fVec.emplace_back(0.f, 0.5f, 1.f);
        vec3fVec.emplace_back(1.f, 0.5f, 0.f);
        vec3fVec.emplace_back(0.f, 0.f, 1.f);

        vec3dvector_t vec3dVec;
        vec3dVec.emplace_back(0.0, 0.5, 1.0);
        vec3dVec.emplace_back(1.0, 0.5, 0.0);
        vec3dVec.emplace_back(0.0, 0.0, 1.0);

        const FloatAttribute fltAttr(&vec3fVec.front()[0], 9, 3);
        const DoubleAttribute dblAttr(&vec3dVec.front()[0], 9, 3);
        const DoubleAttribute dblWrongTupleSize(&vec3dVec.front()[0], 8, 4);
        const DoubleAttribute dblWrongSize(&vec3dVec.front()[0], 8, 3);
        const DoubleAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(vec3dVec, util::rdl2Convert<vec3dvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(vec3dVec, util::rdl2Convert<vec3dvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec3dvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec3dvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3dvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3dvector_t>(dblWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec3dvector_t>(dblWrongSize), util::AttributeDataError);
    }

    void testVec4fVector()
    {
        vec4fvector_t vec4fVec;
        vec4fVec.emplace_back(0.f, 0.5f, 1.f, 1.f);
        vec4fVec.emplace_back(1.f, 0.5f, 0.f, 1.f);
        vec4fVec.emplace_back(0.f, 0.f, 1.f, 1.f);

        vec4dvector_t vec4dVec;
        vec4dVec.emplace_back(0.0, 0.5, 1.0, 1.0);
        vec4dVec.emplace_back(1.0, 0.5, 0.0, 1.0);
        vec4dVec.emplace_back(0.0, 0.0, 1.0, 1.0);

        const FloatAttribute fltAttr(&vec4fVec.front()[0], 12, 4);
        const FloatAttribute fltWrongTupleSize(&vec4fVec.front()[0], 12, 3);
        const FloatAttribute fltWrongSize(&vec4fVec.front()[0], 11, 4);
        const DoubleAttribute dblAttr(&vec4dVec.front()[0], 12, 4);
        const FloatAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(vec4fVec, util::rdl2Convert<vec4fvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(vec4fVec, util::rdl2Convert<vec4fvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec4fvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4fvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4fvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4fvector_t>(fltWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4fvector_t>(fltWrongSize), util::AttributeDataError);
    }

    void testVec4dVector()
    {
        vec4fvector_t vec4fVec;
        vec4fVec.emplace_back(0.f, 0.5f, 1.f, 1.f);
        vec4fVec.emplace_back(1.f, 0.5f, 0.f, 1.f);
        vec4fVec.emplace_back(0.f, 0.f, 1.f, 1.f);

        vec4dvector_t vec4dVec;
        vec4dVec.emplace_back(0.0, 0.5, 1.0, 1.0);
        vec4dVec.emplace_back(1.0, 0.5, 0.0, 1.0);
        vec4dVec.emplace_back(0.0, 0.0, 1.0, 1.0);

        const FloatAttribute fltAttr(&vec4fVec.front()[0], 12, 4);
        const DoubleAttribute dblAttr(&vec4dVec.front()[0], 12, 4);
        const DoubleAttribute dblWrongTupleSize(&vec4dVec.front()[0], 12, 3);
        const DoubleAttribute dblWrongSize(&vec4dVec.front()[0], 11, 4);
        const DoubleAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(vec4dVec, util::rdl2Convert<vec4dvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(vec4dVec, util::rdl2Convert<vec4dvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec4dvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<vec4dvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4dvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4dvector_t>(dblWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<vec4dvector_t>(dblWrongSize), util::AttributeDataError);
    }

    void testMat4fVector()
    {
        mat4fvector_t mat4fVec;
        mat4fVec.emplace_back(1.f, 0.f, 0.f, 0.f,
                              0.f, 1.f, 0.f, 0.f,
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);
        mat4fVec.emplace_back(1.f, 1.f, 1.f, 1.f,
                              0.f, 1.f, 0.f, 1.f,
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);
        mat4fVec.emplace_back(1.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 1.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);

        mat4dvector_t mat4dVec;
        mat4dVec.emplace_back(1.0, 0.0, 0.0, 0.0,
                              0.0, 1.0, 0.0, 0.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.0, 0.0, 0.0, 1.0);
        mat4dVec.emplace_back(1.0, 1.0, 1.0, 1.0,
                              0.0, 1.0, 0.0, 1.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.0, 0.0, 0.0, 1.0);
        mat4dVec.emplace_back(1.0, 0.0, 0.0, 0.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.0, 1.0, 0.0, 0.0,
                              0.0, 0.0, 0.0, 1.0);

        const FloatAttribute fltAttr(&mat4fVec.front()[0][0], 48, 16);
        const FloatAttribute fltWrongTupleSize(&mat4fVec.front()[0][0], 48, 12);
        const FloatAttribute fltWrongSize(&mat4fVec.front()[0][0], 45, 16);
        const DoubleAttribute dblAttr(&mat4dVec.front()[0][0], 48, 16);
        const FloatAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(mat4fVec, util::rdl2Convert<mat4fvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(mat4fVec, util::rdl2Convert<mat4fvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<mat4fvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4fvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW), util::TruncationError);

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4fvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4fvector_t>(fltWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4fvector_t>(fltWrongSize), util::AttributeDataError);
    }

    void testMat4dVector()
    {
        mat4fvector_t mat4fVec;
        mat4fVec.emplace_back(1.f, 0.f, 0.f, 0.f,
                              0.f, 1.f, 0.f, 0.f,
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);
        mat4fVec.emplace_back(1.f, 1.f, 1.f, 1.f,
                              0.f, 1.f, 0.f, 1.f,
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);
        mat4fVec.emplace_back(1.f, 0.f, 0.f, 0.f,
                              0.f, 0.f, 1.f, 0.f,
                              0.f, 1.f, 0.f, 0.f,
                              0.f, 0.f, 0.f, 1.f);

        mat4dvector_t mat4dVec;
        mat4dVec.emplace_back(1.0, 0.0, 0.0, 0.0,
                              0.0, 1.0, 0.0, 0.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.0, 0.0, 0.0, 1.0);
        mat4dVec.emplace_back(1.0, 1.0, 1.0, 1.0,
                              0.0, 1.0, 0.0, 1.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.0, 0.0, 0.0, 1.0);
        mat4dVec.emplace_back(1.0, 0.0, 0.0, 0.0,
                              0.0, 0.0, 1.0, 0.0,
                              0.0, 1.0, 0.0, 0.0,
                              0.0, 0.0, 0.0, 1.0);

        const FloatAttribute fltAttr(&mat4fVec.front()[0][0], 48, 16);
        const DoubleAttribute dblAttr(&mat4dVec.front()[0][0], 48, 16);
        const DoubleAttribute dblWrongTupleSize(&mat4dVec.front()[0][0], 48, 12);
        const DoubleAttribute dblWrongSize(&mat4dVec.front()[0][0], 45, 16);
        const DoubleAttribute invalidAttr;

        CPPUNIT_ASSERT_EQUAL(mat4dVec, util::rdl2Convert<mat4dvector_t>(fltAttr));
        CPPUNIT_ASSERT_EQUAL(mat4dVec, util::rdl2Convert<mat4dvector_t>(dblAttr));

        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<mat4dvector_t>(fltAttr, 0.f, util::TruncateBehavior::THROW));
        CPPUNIT_ASSERT_NO_THROW(util::rdl2Convert<mat4dvector_t>(dblAttr, 0.f, util::TruncateBehavior::THROW));

        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4dvector_t>(invalidAttr), util::InvalidAttributeError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4dvector_t>(dblWrongTupleSize), util::AttributeDataError);
        CPPUNIT_ASSERT_THROW(util::rdl2Convert<mat4dvector_t>(dblWrongSize), util::AttributeDataError);
    }

    CPPUNIT_TEST_SUITE(TestAttrUtil);
    CPPUNIT_TEST( testBool );
    CPPUNIT_TEST( testInt );
    CPPUNIT_TEST( testLong );
    CPPUNIT_TEST( testFloat );
    CPPUNIT_TEST( testDouble );
    CPPUNIT_TEST( testString );
    CPPUNIT_TEST( testRgb );
    CPPUNIT_TEST( testRgba );
    CPPUNIT_TEST( testVec2f );
    CPPUNIT_TEST( testVec2d );
    CPPUNIT_TEST( testVec3f );
    CPPUNIT_TEST( testVec3d );
    CPPUNIT_TEST( testVec4f );
    CPPUNIT_TEST( testVec4d );
    CPPUNIT_TEST( testMat4f );
    CPPUNIT_TEST( testMat4d );

    CPPUNIT_TEST( testBoolVector );
    CPPUNIT_TEST( testIntVector );
    CPPUNIT_TEST( testLongVector );
    CPPUNIT_TEST( testFloatVector );
    CPPUNIT_TEST( testDoubleVector );
    CPPUNIT_TEST( testStringVector );
    CPPUNIT_TEST( testRgbVector );
    CPPUNIT_TEST( testRgbaVector );
    CPPUNIT_TEST( testVec2fVector );
    CPPUNIT_TEST( testVec2dVector );
    CPPUNIT_TEST( testVec3fVector );
    CPPUNIT_TEST( testVec3dVector );
    CPPUNIT_TEST( testVec4fVector );
    CPPUNIT_TEST( testVec4dVector );
    CPPUNIT_TEST( testMat4fVector );
    CPPUNIT_TEST( testMat4dVector );

    CPPUNIT_TEST_SUITE_END();
};

}
}

