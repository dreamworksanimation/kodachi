// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// katana
#include <FnAttribute/FnAttribute.h>
#include <FnGeolib/op/FnOpDescriptionBuilder.h>
#include <FnGeolib/util/Path.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnGeolibServices/FnExpressionMath.h>
#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/CookInterfaceUtils.h>

// system
#include <algorithm>
#include <cmath>
#include <functional>
#include <sstream>
#include <type_traits>
#include <unordered_map>

namespace {
KdLogSetup("AttributeOperation");

constexpr FnKatAttributeType kIntType    = kFnKatAttributeTypeInt;
constexpr FnKatAttributeType kFloatType  = kFnKatAttributeTypeFloat;
constexpr FnKatAttributeType kDoubleType = kFnKatAttributeTypeDouble;
constexpr FnKatAttributeType kStringType = kFnKatAttributeTypeString;

// OpArg strings
const std::string sAttributeName("attributeName");
const std::string sCEL("CEL");
const std::string sCookDaps("cookDaps");
const std::string sMode("mode");
const std::string sOperation("operation");
const std::string sValue("value");
const std::string sConvertTo("convert_to");
const std::string sCopyTo("copy_to");

// Attribute strings
const std::string sAttributeOperations("attributeOperations");
const std::string sType("type");
const std::string sExpressionMathInputs("expressionMathInputs");

enum class Operation {
    // Binary Operations
    ADD,
    SUBTRACT,
    MULTIPY,
    DIVIDE,
    POW,
    MIN,
    MAX,
    COPYSIGN,
    FMOD,
    NUM_BINARY_OPS,

    // Unary Operations
    ABS,
    ACOS,
    ASIN,
    ATAN,
    CEIL,
    COS,
    EXP,
    EXP2,
    EXPM1,
    FLOOR,
    LOG,
    LOG10,
    LOG1P,
    LOG2,
    NEGATE,
    ROUND,
    SIN,
    SQRT,
    TAN,
    TRUNC,
    NUM_NUMERIC_OPS,

    // Data operations
    CONVERT,
    COPY,

    // ExpressionMath functions
    CLAMP,
    LERP,
    SMOOTHSTEP,
    FIT,
    CLAMP_FIT,      // cfit
    SOFT_CLAMP_FIT, // softcfit
    RETIME,
    RANDVAL,
    NOISE,
    SNOISE, // Signed Improved Perlin noise (Siggraph 2002)

    INVALID
};

const std::string sAdd("add");
const std::string sSubtract("subtract");
const std::string sMultiply("multiply");
const std::string sDivide("divide");
const std::string sPow("pow");
const std::string sMin("min");
const std::string sMax("max");
const std::string sCopysign("copysign");
const std::string sFmod("fmod");

const std::string sAbs("abs");
const std::string sAcos("acos");
const std::string sAsin("asin");
const std::string sAtan("atan");
const std::string sCeil("ceil");
const std::string sCos("cos");
const std::string sExp("exp");
const std::string sExp2("exp2");
const std::string sExpm1("expm1");
const std::string sFloor("floor");
const std::string sLog("log");
const std::string sLog10("log10");
const std::string sLog1p("log1p");
const std::string sLog2("log2");
const std::string sNegate("negate");
const std::string sRound("round");
const std::string sSin("sin");
const std::string sSqrt("sqrt");
const std::string sTan("tan");
const std::string sTrunc("trunc");

const std::string sClamp("clamp");
const std::string sLerp("lerp");
const std::string sSmoothstep("smoothstep");
const std::string sFit("fit");
const std::string sCFit("cfit");
const std::string sSoftCFit("softcfit");
const std::string sRetime("retime");
const std::string sRandomVal("random");
const std::string sNoise("noise");
const std::string sSignedNoise("signed_noise");

const std::string sConvert("convert");
const std::string sCopy("copy");

template <typename T>
inline T
num_from_str(const std::string& val_str)
{
    T result { };

    try {
        if (std::is_same<T, int>::value) {
            result = std::stoi(val_str);
        }
        else if (std::is_same<T, float>::value) {
            result = std::stof(val_str);
        }
        else if (std::is_same<T, double>::value) {
            result = std::stod(val_str);
        }
    }
    catch (... /* std::out_of_range, std::invalid_argument */) {
        return { };
    }

    return result;
}

Operation
toOperation(const FnAttribute::StringAttribute& operationAttr)
{
    static const std::unordered_map<std::string, Operation> operationMap
    {
        { sAdd,      Operation::ADD      },
        { sSubtract, Operation::SUBTRACT },
        { sMultiply, Operation::MULTIPY  },
        { sDivide,   Operation::DIVIDE   },
        { sPow,      Operation::POW      },
        { sMin,      Operation::MIN      },
        { sMax,      Operation::MAX      },
        { sCopysign, Operation::COPYSIGN },
        { sFmod,     Operation::FMOD     },

        { sAbs,      Operation::ABS      },
        { sAcos,     Operation::ACOS     },
        { sAsin,     Operation::ASIN     },
        { sAtan,     Operation::ATAN     },
        { sCeil,     Operation::CEIL     },
        { sCos,      Operation::COS      },
        { sExp,      Operation::EXP      },
        { sExp2,     Operation::EXP2     },
        { sExpm1,    Operation::EXPM1    },
        { sFloor,    Operation::FLOOR    },
        { sLog,      Operation::LOG      },
        { sLog10,    Operation::LOG10    },
        { sLog1p,    Operation::LOG1P    },
        { sLog2,     Operation::LOG2     },
        { sNegate,   Operation::NEGATE   },
        { sRound,    Operation::ROUND    },
        { sSin,      Operation::SIN      },
        { sSqrt,     Operation::SQRT     },
        { sTan,      Operation::TAN      },
        { sTrunc,    Operation::TRUNC    },

        { sClamp,       Operation::CLAMP },
        { sLerp,        Operation::LERP },
        { sSmoothstep,  Operation::SMOOTHSTEP },
        { sFit,         Operation::FIT },
        { sCFit,        Operation::CLAMP_FIT },
        { sSoftCFit,    Operation::SOFT_CLAMP_FIT },
        { sRetime,      Operation::RETIME },
        { sRandomVal,   Operation::RANDVAL },
        { sNoise,       Operation::NOISE },
        { sSignedNoise, Operation::SNOISE },

        { sConvert,  Operation::CONVERT  },
        { sCopy,     Operation::COPY  }
    };

    const auto iter = operationMap.find(operationAttr.getValue());
    if (iter != operationMap.end()) {
        return iter->second;
    }

    return Operation::INVALID;
}

FnKatAttributeType
getType(const std::string& type)
{
    if (type == "int") {
        return kFnKatAttributeTypeInt;
    } else if (type == "float") {
        return kFnKatAttributeTypeFloat;
    } else if (type == "double") {
        return kFnKatAttributeTypeDouble;
    } else if (type == "string") {
        return kFnKatAttributeTypeString;
    } else {
        return kFnKatAttributeTypeError;
    }
}

//--------------------------------------

bool
isBinaryOperation(Operation op)
{
    return op < Operation::NUM_BINARY_OPS;
}

bool
isNumericOp(Operation op)
{
    return op < Operation::NUM_NUMERIC_OPS;
}

bool
isConversionOperation(Operation op)
{
    return op == Operation::CONVERT;
}

bool
isCopyOperation(Operation op)
{
    return op == Operation::COPY;
}

bool
isDataOperation(Operation op)
{
    return isConversionOperation(op) || isCopyOperation(op);
}

bool
isExpressionMath(Operation op)
{
    return op >= Operation::CLAMP && op != Operation::INVALID;
}

//--------------------------------------

// Copies the data out of an Attribute so that we can apply multiple operations
// before creating a new Attribute from it.
template <class attr_t>
struct MutableAttribute
{
    using value_type = typename attr_t::value_type;

    MutableAttribute(const attr_t& attr)
        : mTupleSize(attr.getTupleSize())
        , mNumValues(attr.getNumberOfValues())
        , mNumTimeSamples(attr.getNumberOfTimeSamples())
    {
        mSampleTimes.reserve(mNumTimeSamples);
        mValues.reserve(mNumValues * mNumTimeSamples);
        for (int64_t timeIdx = 0; timeIdx < mNumTimeSamples; ++ timeIdx) {
            const float sampleTime = attr.getSampleTime(timeIdx);
            const typename attr_t::array_type sample = attr.getNearestSample(sampleTime);

            mSampleTimes.push_back(sampleTime);
            mValues.insert(mValues.end(), sample.begin(), sample.end());
        }
    }

    FnAttribute::Attribute toFnAttribute() {
        std::vector<const value_type*> valuePtrs;
        valuePtrs.reserve(mNumTimeSamples);

        for (int64_t timeIdx = 0; timeIdx < mNumTimeSamples; ++ timeIdx) {
            valuePtrs.push_back(&mValues[timeIdx * mNumValues]);
        }

        return attr_t(mSampleTimes.data(), mSampleTimes.size(),
                      valuePtrs.data(), mNumValues, mTupleSize);
    }

    std::vector<value_type>& getValues() { return mValues; }

public:
    const int64_t mTupleSize;
    const int64_t mNumValues;
    const int64_t mNumTimeSamples;

    std::vector<float> mSampleTimes;
    std::vector<value_type> mValues;
};

inline bool
isNumberAttr(const FnAttribute::Attribute& attribute)
{
    const auto attrType = attribute.getType();

    return attrType == kIntType
            || attrType == kFloatType
            || attrType == kDoubleType;
}

inline bool
isStringAttr(const FnAttribute::Attribute& attribute)
{
    return attribute.getType() == kStringType;
}

template <class return_type>
inline return_type
castAttr(const FnAttribute::Attribute& attribute)
{
    switch (attribute.getType()) {
    case kIntType:
        return FnAttribute::IntAttribute(attribute).getValue();
    case kFloatType:
        return FnAttribute::FloatAttribute(attribute).getValue();
    case kDoubleType:
        return FnAttribute::DoubleAttribute(attribute).getValue();
    }

    return return_type{};
}

template <class operation, class attr_t>
inline void
applyBinaryOperation(MutableAttribute<attr_t>& attr,
                     const FnAttribute::Attribute& valueAttr)
{
    const auto opValue = castAttr<typename attr_t::value_type>(valueAttr);

    for (auto& v : attr.getValues()) {
        v = operation()(v, opValue);
    }
}

template <class T>
struct Divide : public std::binary_function<T, T, T>
{
    T
    operator()(const T& x, const T& y) const {
        if (y == (T)0) {
            throw std::invalid_argument("Cannot divide by 0");
        }

        return x / y;
    }
};

template <class T>
struct Pow : public std::binary_function<T, T, T>
{
    T
    operator()(const T& x, const T& y) const {
        return std::pow(x, y);
    }
};

template <class T>
struct Min : public std::binary_function<T, T, T>
{
    T
    operator()(const T& x, const T& y) const {
        return std::min(x, y);
    }
};

template <class T>
struct Max : public std::binary_function<T, T, T>
{
    T
    operator()(const T& x, const T& y) const {
        return std::max(x, y);
    }
};

template <class T>
struct Copysign : public std::binary_function<T, T, T>
{
    T
    operator()(const T& x, const T& y) const {
        return std::copysign(x, y);
    }
};

template <class T>
struct Fmod : public std::binary_function<T, T, T>
{
    T
    operator()(const T& x, const T& y) const {
        return std::fmod(x, y);
    }
};

using OperationVec = std::vector<std::pair<Operation, FnAttribute::Attribute>>;

// Applies all operations to an Attribute and returns the result in an
// Attribute of the same type
// It is assumed that the data in operations is valid, and that any binary
// operations have a valid matching value
template <class attr_t>
attr_t
applyOperations(const attr_t& attr, const OperationVec& operations)
{
    using value_type = typename attr_t::value_type;

    MutableAttribute<attr_t> mutAttr(attr);

    for (auto& operation : operations) {
        switch (operation.first) {
        // binary operations
        case Operation::ADD:
            applyBinaryOperation<std::plus<value_type>>(mutAttr, operation.second);
            break;
        case Operation::SUBTRACT:
            applyBinaryOperation<std::minus<value_type>>(mutAttr, operation.second);
            break;
        case Operation::MULTIPY:
            applyBinaryOperation<std::multiplies<value_type>>(mutAttr, operation.second);
            break;
        case Operation::DIVIDE:
            applyBinaryOperation<Divide<value_type>>(mutAttr, operation.second);
            break;
        case Operation::POW:
            applyBinaryOperation<Pow<value_type>>(mutAttr, operation.second);
            break;
        case Operation::MIN:
            applyBinaryOperation<Min<value_type>>(mutAttr, operation.second);
            break;
        case Operation::MAX:
            applyBinaryOperation<Max<value_type>>(mutAttr, operation.second);
            break;
        case Operation::COPYSIGN:
            applyBinaryOperation<Copysign<value_type>>(mutAttr, operation.second);
            break;
        case Operation::FMOD:
            applyBinaryOperation<Fmod<value_type>>(mutAttr, operation.second);
            break;

        // unary operations
        case Operation::ABS:
            for (auto& v : mutAttr.getValues()) { v = std::abs(v); }
            break;
        case Operation::ACOS:
            for (auto& v : mutAttr.getValues()) { v = std::acos(v); }
            break;
        case Operation::ASIN:
            for (auto& v : mutAttr.getValues()) { v = std::asin(v); }
            break;
        case Operation::ATAN:
            for (auto& v : mutAttr.getValues()) { v = std::atan(v); }
            break;
        case Operation::CEIL:
            for (auto& v : mutAttr.getValues()) { v = std::ceil(v); }
            break;
        case Operation::COS:
            for (auto& v : mutAttr.getValues()) { v = std::cos(v); }
            break;
        case Operation::EXP:
            for (auto& v : mutAttr.getValues()) { v = std::exp(v); }
            break;
        case Operation::EXPM1:
            for (auto& v : mutAttr.getValues()) { v = std::expm1(v); }
            break;
        case Operation::EXP2:
            for (auto& v : mutAttr.getValues()) { v = std::exp2(v); }
            break;
        case Operation::FLOOR:
            for (auto& v : mutAttr.getValues()) { v = std::floor(v); }
            break;
        case Operation::LOG:
            for (auto& v : mutAttr.getValues()) { v = std::log(v); }
            break;
        case Operation::LOG1P:
            for (auto& v : mutAttr.getValues()) { v = std::log1p(v); }
            break;
        case Operation::LOG2:
            for (auto& v : mutAttr.getValues()) { v = std::log2(v); }
            break;
        case Operation::LOG10:
            for (auto& v : mutAttr.getValues()) { v = std::log10(v); }
            break;
        case Operation::NEGATE:
            for (auto& v : mutAttr.getValues()) { v = -v; }
            break;
        case Operation::ROUND:
            for (auto& v : mutAttr.getValues()) { v = std::round(v); }
            break;
        case Operation::SIN:
            for (auto& v : mutAttr.getValues()) { v = std::sin(v); }
            break;
        case Operation::SQRT:
            for (auto& v : mutAttr.getValues()) { v = std::sqrt(v); }
            break;
        case Operation::TAN:
            for (auto& v : mutAttr.getValues()) { v = std::tan(v); }
            break;
        case Operation::TRUNC:
            for (auto& v : mutAttr.getValues()) { v = std::trunc(v); }
            break;
        default: break;
        }
    }

    return mutAttr.toFnAttribute();
}

FnAttribute::Attribute
applyOperations(const FnAttribute::Attribute& attr, const OperationVec& operations)
{
    switch (attr.getType()) {
    case kFnKatAttributeTypeInt:
        return applyOperations<FnAttribute::IntAttribute>(attr, operations);
    case kFnKatAttributeTypeFloat:
        return applyOperations<FnAttribute::FloatAttribute>(attr, operations);
    case kFnKatAttributeTypeDouble:
        return applyOperations<FnAttribute::DoubleAttribute>(attr, operations);
    }

    return {};
}

//--------------------------------------------------------------

inline FnAttribute::GroupAttribute
buildExpressionMathArgs(Operation operation,
                        const FnAttribute::GroupAttribute& opArgs)
{
    FnAttribute::GroupBuilder gb;

    if (operation == Operation::CLAMP) {
        gb.set("lower_bound", opArgs.getChildByName("lower_bound"));
        gb.set("upper_bound", opArgs.getChildByName("upper_bound"));
    }
    else if (operation == Operation::LERP) {
        gb.set("lower_bound", opArgs.getChildByName("lower_bound"));
        gb.set("upper_bound", opArgs.getChildByName("upper_bound"));
        gb.set("t", opArgs.getChildByName("t"));
    }
    else if (operation == Operation::FIT ||
                operation == Operation::CLAMP_FIT ||
                    operation == Operation::SOFT_CLAMP_FIT) {
        gb.set("old_min", opArgs.getChildByName("old_min"));
        gb.set("old_max", opArgs.getChildByName("old_max"));
        gb.set("new_min", opArgs.getChildByName("new_min"));
        gb.set("new_max", opArgs.getChildByName("new_max"));
    }
    else if (operation == Operation::RETIME) {
        gb.set("frame",         opArgs.getChildByName("frame"));
        gb.set("start",         opArgs.getChildByName("start"));
        gb.set("end",           opArgs.getChildByName("end"));
        gb.set("hold_mode_in",  opArgs.getChildByName("hold_mode_in"));
        gb.set("hold_mode_out", opArgs.getChildByName("hold_mode_out"));
    }
    else if (operation == Operation::RANDVAL) {
        gb.set("lower_bound", opArgs.getChildByName("lower_bound"));
        gb.set("upper_bound", opArgs.getChildByName("upper_bound"));
        gb.set("auto_seed",   opArgs.getChildByName("auto_seed"));
        gb.set("seed",        opArgs.getChildByName("seed"));
    }
    else if (operation == Operation::NOISE || operation == Operation::SNOISE) {
        gb.set("dimensions", opArgs.getChildByName("dimensions"));
        gb.set("x", opArgs.getChildByName("x"));
        gb.set("y", opArgs.getChildByName("y"));
        gb.set("z", opArgs.getChildByName("z"));
        gb.set("w", opArgs.getChildByName("w"));
    }

    return gb.build();
}

template <typename attr_t>
inline void
clamp(MutableAttribute<attr_t>& attr, const FnAttribute::GroupAttribute& inputs)
{
    using value_type = typename attr_t::value_type;

    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::DoubleAttribute lowerBoundAttr = inputs.getChildByName("lower_bound");
    const FnAttribute::DoubleAttribute upperBoundAttr = inputs.getChildByName("upper_bound");
    if (!lowerBoundAttr.isValid() || !upperBoundAttr.isValid()) {
        return;
    }

    const value_type lowerBound = static_cast<value_type>(lowerBoundAttr.getValue());
    const value_type upperBound = static_cast<value_type>(upperBoundAttr.getValue());
    if (lowerBound > upperBound) {
        return;
    }

    for (auto& val : attr.getValues()) {
        val = FnKat::FnExpressionMath::clamp(val, lowerBound, upperBound);
    }
}

template <typename attr_t>
inline void
lerp(MutableAttribute<attr_t>& attr, const FnAttribute::GroupAttribute& inputs)
{
    using value_type = typename attr_t::value_type;

    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::DoubleAttribute lowerBoundAttr = inputs.getChildByName("lower_bound");
    const FnAttribute::DoubleAttribute upperBoundAttr = inputs.getChildByName("upper_bound");
    const FnAttribute::DoubleAttribute t_Attr = inputs.getChildByName("t");
    if (!lowerBoundAttr.isValid() || !upperBoundAttr.isValid() || !t_Attr.isValid()) {
        return;
    }

    const double lowerBound = lowerBoundAttr.getValue();
    const double upperBound = upperBoundAttr.getValue();
    const double t          = t_Attr.getValue();
    if (lowerBound > upperBound || t < 0.0 || t > 1.0) {
        return;
    }

    for (auto& val : attr.getValues()) {
        val = static_cast<value_type>(
                FnKat::FnExpressionMath::lerp(t, lowerBound, upperBound));
    }
}

template <typename attr_t>
inline void
smoothstep(MutableAttribute<attr_t>& attr)
{
    using value_type = typename attr_t::value_type;

    for (auto& val : attr.getValues()) {
        val = static_cast<value_type>(FnKat::FnExpressionMath::smoothstep(val));
    }
}

template <typename attr_t>
inline void
fit(MutableAttribute<attr_t>& attr, const FnAttribute::GroupAttribute& inputs, Operation op)
{
    using value_type = typename attr_t::value_type;

    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::DoubleAttribute oldMinAttr = inputs.getChildByName("old_min");
    const FnAttribute::DoubleAttribute oldMaxAttr = inputs.getChildByName("old_max");

    const FnAttribute::DoubleAttribute newMinAttr = inputs.getChildByName("new_min");
    const FnAttribute::DoubleAttribute newMaxAttr = inputs.getChildByName("new_max");

    if (!oldMinAttr.isValid() || !oldMaxAttr.isValid() ||
            !newMinAttr.isValid() || !newMaxAttr.isValid()) {
        return;
    }

    const value_type oldMinBound = static_cast<value_type>(oldMinAttr.getValue());
    const value_type oldMaxBound = static_cast<value_type>(oldMaxAttr.getValue());
    if (oldMinBound > oldMaxBound) {
        return;
    }

    const value_type newMinBound = static_cast<value_type>(newMinAttr.getValue());
    const value_type newMaxBound = static_cast<value_type>(newMaxAttr.getValue());
    if (newMinBound > newMaxBound) {
        return;
    }

    if (op == Operation::FIT) {
        for (auto& val : attr.getValues()) {
            val = static_cast<value_type>(
                    FnKat::FnExpressionMath::fit(val,
                                                 oldMinBound, oldMaxBound,
                                                 newMinBound, newMaxBound));
        }
    }
    else if (op == Operation::CLAMP_FIT) {
        for (auto& val : attr.getValues()) {
            val = static_cast<value_type>(
                    FnKat::FnExpressionMath::cfit(val,
                                                  oldMinBound, oldMaxBound,
                                                  newMinBound, newMaxBound));
        }
    }
    else if (op == Operation::SOFT_CLAMP_FIT) {
        for (auto& val : attr.getValues()) {
            val = static_cast<value_type>(
                    FnKat::FnExpressionMath::softcfit(val,
                                                      oldMinBound, oldMaxBound,
                                                      newMinBound, newMaxBound));
        }
    }
}

template <typename attr_t>
inline void
retime(MutableAttribute<attr_t>& attr, const FnAttribute::GroupAttribute& inputs)
{
    using value_type = typename attr_t::value_type;

    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::DoubleAttribute frameAttr = inputs.getChildByName("frame");
    const FnAttribute::DoubleAttribute startAttr = inputs.getChildByName("start");
    const FnAttribute::DoubleAttribute endAttr   = inputs.getChildByName("end");
    if (!frameAttr.isValid() || !startAttr.isValid() || !endAttr.isValid()) {
        return;
    }

    const double frame = frameAttr.getValue();
    const double start = startAttr.getValue();
    const double end   = endAttr.getValue();
    if (start > end) {
        return;
    }

    const FnAttribute::IntAttribute holdModeInAttr  = inputs.getChildByName("hold_mode_in");
    const FnAttribute::IntAttribute holdModeOutAttr = inputs.getChildByName("hold_mode_out");
    if (!holdModeInAttr.isValid() || !holdModeOutAttr.isValid()) {
        return;
    }

    const int holdModeIn  = holdModeInAttr.getValue();
    const int holdModeOut = holdModeOutAttr.getValue();

    for (auto& val : attr.getValues()) {
        val = static_cast<value_type>(
                FnKat::FnExpressionMath::retime(
                    frame,
                    start,
                    end,
                    static_cast<FnKat::FnExpressionMath::RetimeHoldMode>(holdModeIn),
                    static_cast<FnKat::FnExpressionMath::RetimeHoldMode>(holdModeOut)));
    }
}

inline std::size_t
generateSeed(const std::string& location, const FnAttribute::Attribute& attr)
{
    std::size_t seed = 0;
    std::size_t locationHash = std::hash<std::string>{}(location);
    seed ^= locationHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);

    std::size_t attrHash = attr.getHash().uint64();
    seed ^= attrHash + 0x9e3779b9 + (seed << 6) + (seed >> 2);

    return seed;
}

template <typename attr_t>
inline void
randomval(MutableAttribute<attr_t>& attr,
          const FnAttribute::GroupAttribute& inputs,
          std::size_t locAndAttrHash)
{
    using value_type = typename attr_t::value_type;

    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::DoubleAttribute lowerBoundAttr = inputs.getChildByName("lower_bound");
    const FnAttribute::DoubleAttribute upperBoundAttr = inputs.getChildByName("upper_bound");
    const FnAttribute::IntAttribute    autoSeedAttr   = inputs.getChildByName("auto_seed");

    if (!upperBoundAttr.isValid() || !lowerBoundAttr.isValid() || !autoSeedAttr.isValid()) {
        return;
    }

    const value_type lowerBound = static_cast<value_type>(lowerBoundAttr.getValue());
    const value_type upperBound = static_cast<value_type>(upperBoundAttr.getValue());
    if (lowerBound > upperBound) {
        return;
    }

    std::uniform_real_distribution<typename attr_t::value_type> rangedRNG(lowerBound, upperBound);
    const bool useSeed = (autoSeedAttr.getValue() == 0);
    if (useSeed) {
        const FnAttribute::IntAttribute seedAttr = inputs.getChildByName("seed");
        if (!seedAttr.isValid()) {
            return;
        }

        const int seed = seedAttr.getValue();
        std::mt19937 rng(seed);
        for (auto& val : attr.getValues()) {
            val = rangedRNG(rng);
        }
    }
    else {
        std::mt19937_64 rng(locAndAttrHash);
        for (auto& val : attr.getValues()) {
            val = rangedRNG(rng);
        }
    }
}

template <>
inline void
randomval<FnAttribute::IntAttribute>(MutableAttribute<FnAttribute::IntAttribute>& attr,
                                     const FnAttribute::GroupAttribute& inputs,
                                     std::size_t locAndAttrHash)
{
    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::DoubleAttribute lowerBoundAttr = inputs.getChildByName("lower_bound");
    const FnAttribute::DoubleAttribute upperBoundAttr = inputs.getChildByName("upper_bound");
    const FnAttribute::IntAttribute    autoSeedAttr   = inputs.getChildByName("auto_seed");

    if (!upperBoundAttr.isValid() || !lowerBoundAttr.isValid() || !autoSeedAttr.isValid()) {
        return;
    }

    const int lowerBound = static_cast<int>(lowerBoundAttr.getValue());
    const int upperBound = static_cast<int>(upperBoundAttr.getValue());
    if (lowerBound > upperBound) {
        return;
    }

    std::uniform_int_distribution<> rangedRNG(lowerBound, upperBound);
    const bool useSeed = (autoSeedAttr.getValue() == 0);
    if (useSeed) {
        const FnAttribute::IntAttribute seedAttr = inputs.getChildByName("seed");
        if (!seedAttr.isValid()) {
            return;
        }

        const int seed = seedAttr.getValue();
        std::mt19937 rng(seed);
        for (auto& val : attr.getValues()) {
            val = rangedRNG(rng);
        }
    }
    else {
        std::mt19937_64 rng(locAndAttrHash);
        for (auto& val : attr.getValues()) {
            val = rangedRNG(rng);
        }
    }
}

template <typename attr_t>
inline void
noise(MutableAttribute<attr_t>& attr, const FnAttribute::GroupAttribute& inputs, bool isSigned)
{
    using value_type = typename attr_t::value_type;

    if (!inputs.isValid()) {
        return;
    }

    const FnAttribute::IntAttribute dimensionsAttr = inputs.getChildByName("dimensions");
    if (!dimensionsAttr.isValid()) {
        return;
    }

    const int dimensions = dimensionsAttr.getValue();
    switch (dimensions)
    {
        case 1:
        {
            const FnAttribute::DoubleAttribute xAttr = inputs.getChildByName("x");
            if (!xAttr.isValid()) {
                return;
            }

            const float x = static_cast<float>(xAttr.getValue());

            if (isSigned) {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::snoise(x));
                }
            }
            else {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::noise(x));
                }
            }

            break;
        }

        case 2:
        {
            const FnAttribute::DoubleAttribute xAttr = inputs.getChildByName("x");
            const FnAttribute::DoubleAttribute yAttr = inputs.getChildByName("y");
            if (!xAttr.isValid() || !yAttr.isValid()) {
                return;
            }

            const float x = static_cast<float>(xAttr.getValue());
            const float y = static_cast<float>(yAttr.getValue());

            if (isSigned) {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::snoise(x, y));
                }
            }
            else {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::noise(x, y));
                }
            }

            break;
        }

        case 3:
        {
            const FnAttribute::DoubleAttribute xAttr = inputs.getChildByName("x");
            const FnAttribute::DoubleAttribute yAttr = inputs.getChildByName("y");
            const FnAttribute::DoubleAttribute zAttr = inputs.getChildByName("z");
            if (!xAttr.isValid() || !yAttr.isValid() || !zAttr.isValid()) {
                return;
            }

            const float x = static_cast<float>(xAttr.getValue());
            const float y = static_cast<float>(yAttr.getValue());
            const float z = static_cast<float>(zAttr.getValue());

            if (isSigned) {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::snoise(x, y, z));
                }
            }
            else {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::noise(x, y, z));
                }
            }

            break;
        }

        case 4:
        {
            const FnAttribute::DoubleAttribute xAttr = inputs.getChildByName("x");
            const FnAttribute::DoubleAttribute yAttr = inputs.getChildByName("y");
            const FnAttribute::DoubleAttribute zAttr = inputs.getChildByName("z");
            const FnAttribute::DoubleAttribute wAttr = inputs.getChildByName("w");
            if (!xAttr.isValid() || !yAttr.isValid() || !zAttr.isValid() || !wAttr.isValid()) {
                return;
            }

            const float x = static_cast<float>(xAttr.getValue());
            const float y = static_cast<float>(yAttr.getValue());
            const float z = static_cast<float>(zAttr.getValue());
            const float w = static_cast<float>(wAttr.getValue());

            if (isSigned) {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::snoise(x, y, z, w));
                }
            }
            else {
                for (auto& val : attr.getValues()) {
                    val = static_cast<value_type>(FnKat::FnExpressionMath::noise(x, y, z, w));
                }
            }
        }
    }
}

template <class attr_t>
attr_t
applyExpressionMathOp(const std::string& location,
                      const attr_t& attr,
                      const OperationVec& operations)
{
    MutableAttribute<attr_t> mutAttr(attr);

    for (auto& operation : operations) {
        switch (operation.first) {
        case Operation::CLAMP:
            clamp<attr_t>(mutAttr, operation.second);
            break;
        case Operation::LERP:
            lerp<attr_t>(mutAttr, operation.second);
            break;
        case Operation::SMOOTHSTEP:
            smoothstep<attr_t>(mutAttr);
            break;
        case Operation::FIT:
        case Operation::CLAMP_FIT:
        case Operation::SOFT_CLAMP_FIT:
            fit<attr_t>(mutAttr, operation.second, operation.first);
            break;
        case Operation::RETIME:
            retime<attr_t>(mutAttr, operation.second);
            break;
        case Operation::RANDVAL:
            randomval<attr_t>(mutAttr, operation.second, generateSeed(location, attr));
            break;
        case Operation::NOISE:
            noise<attr_t>(mutAttr, operation.second, false);
            break;
        case Operation::SNOISE:
            noise<attr_t>(mutAttr, operation.second, true);
            break;
        default: break;
        }
    }

    return mutAttr.toFnAttribute();
}

template <>
FnAttribute::IntAttribute
applyExpressionMathOp(const std::string& location,
                      const FnAttribute::IntAttribute& attr,
                      const OperationVec& operations)
{
    MutableAttribute<FnAttribute::IntAttribute> mutAttr(attr);

    for (auto& operation : operations) {
        switch (operation.first) {
        case Operation::CLAMP:
            clamp<FnAttribute::IntAttribute>(mutAttr, operation.second);
            break;
        case Operation::LERP:
            lerp<FnAttribute::IntAttribute>(mutAttr, operation.second);
            break;
        case Operation::RETIME:
            retime<FnAttribute::IntAttribute>(mutAttr, operation.second);
            break;
        case Operation::RANDVAL:
            randomval<FnAttribute::IntAttribute>(mutAttr,
                                                 operation.second,
                                                 generateSeed(location, attr));
            break;
        default: break;
        }
    }

    return mutAttr.toFnAttribute();
}

FnAttribute::Attribute
applyExpressionMathOp(const std::string& location,
                      const FnAttribute::Attribute& attr,
                      const OperationVec& operations)
{
    switch (attr.getType()) {
    case kFnKatAttributeTypeInt:
        return applyExpressionMathOp<FnAttribute::IntAttribute>(location, attr, operations);
    case kFnKatAttributeTypeFloat:
        return applyExpressionMathOp<FnAttribute::FloatAttribute>(location, attr, operations);
    case kFnKatAttributeTypeDouble:
        return applyExpressionMathOp<FnAttribute::DoubleAttribute>(location, attr, operations);
    }

    return {};
}

//--------------------------------------------------------------

template<class return_t, class value_t, class value_type>
FnAttribute::Attribute
toTypedAttribute(const std::vector<value_type>& values,
                 const int64_t tupleSize,
                 const int64_t numTimeSamples,
                 const int64_t numValues,
                 const std::vector<float>& sampleTimes)
{
    std::vector<value_t> newValues;
    newValues.reserve(values.size());
    for (const auto& v : values) {
        newValues.push_back(static_cast<value_t>(v));
    }

    std::vector<const value_t*> valuePtrs;
    valuePtrs.reserve(numTimeSamples);

    for (int64_t timeIdx = 0; timeIdx < numTimeSamples; ++ timeIdx) {
        valuePtrs.push_back(&newValues[timeIdx * numValues]);
    }

    return return_t(sampleTimes.data(), sampleTimes.size(),
                  valuePtrs.data(), numValues, tupleSize);
}

// casting from strings
template<class return_t, class value_t>
FnAttribute::Attribute
toTypedAttribute(const std::vector<std::string>& values,
                 const int64_t tupleSize,
                 const int64_t numTimeSamples,
                 const int64_t numValues,
                 const std::vector<float>& sampleTimes)
{
    std::vector<value_t> newValues;
    newValues.reserve(values.size());
    for (const auto& v : values) {
        newValues.push_back(num_from_str<value_t>(v));
    }

    std::vector<const value_t*> valuePtrs;
    valuePtrs.reserve(numTimeSamples);

    for (int64_t timeIdx = 0; timeIdx < numTimeSamples; ++ timeIdx) {
        valuePtrs.push_back(&newValues[timeIdx * numValues]);
    }

    return return_t(sampleTimes.data(), sampleTimes.size(),
                  valuePtrs.data(), numValues, tupleSize);
}

// casting numbers to strings
template<class value_type>
FnAttribute::StringAttribute
toStringAttribute(const std::vector<value_type>& values,
                 const int64_t tupleSize,
                 const int64_t numTimeSamples,
                 const int64_t numValues,
                 const std::vector<float>& sampleTimes)
{
    std::vector<std::string> newValues;
    newValues.reserve(values.size());
    for (const auto& v : values) {
        newValues.push_back(std::to_string(v));
    }

    std::vector<const char*> newValuesCstr;
    newValuesCstr.reserve(newValues.size());
    for (const auto& v : newValues) {
        newValuesCstr.push_back(v.c_str());
    }

    std::vector<const char**> valuePtrs;
    valuePtrs.reserve(numTimeSamples);

    for (int64_t timeIdx = 0; timeIdx < numTimeSamples; ++ timeIdx) {
        valuePtrs.push_back(&newValuesCstr[timeIdx * numValues]);
    }

    return FnAttribute::StringAttribute(sampleTimes.data(), sampleTimes.size(),
            valuePtrs.data(), numValues, tupleSize);
}

template<>
FnAttribute::StringAttribute
toStringAttribute(const std::vector<std::string>& values,
                 const int64_t tupleSize,
                 const int64_t numTimeSamples,
                 const int64_t numValues,
                 const std::vector<float>& sampleTimes)
{
    std::vector<std::string> newValues;
    newValues.reserve(values.size());
    for (const auto& v : values) {
        newValues.push_back(v);
    }

    std::vector<const char*> newValuesCstr;
    newValuesCstr.reserve(newValues.size());
    for (const auto& v : newValues) {
        newValuesCstr.push_back(v.c_str());
    }

    std::vector<const char**> valuePtrs;
    valuePtrs.reserve(numTimeSamples);

    for (int64_t timeIdx = 0; timeIdx < numTimeSamples; ++ timeIdx) {
        valuePtrs.push_back(&newValuesCstr[timeIdx * numValues]);
    }

    return FnAttribute::StringAttribute(sampleTimes.data(), sampleTimes.size(),
            valuePtrs.data(), numValues, tupleSize);
}

template <class attr_t>
FnAttribute::Attribute
convertAttribute(const attr_t& attr, const FnKatAttributeType& convertToType)
{
    MutableAttribute<attr_t> mutAttr(attr);

    switch(convertToType) {
    case kFnKatAttributeTypeString:
        return toStringAttribute(mutAttr.mValues,
                mutAttr.mTupleSize, mutAttr.mNumTimeSamples, mutAttr.mNumValues,
                mutAttr.mSampleTimes);
    case kFnKatAttributeTypeInt:
        return toTypedAttribute<FnAttribute::IntAttribute, int>(mutAttr.mValues,
                mutAttr.mTupleSize, mutAttr.mNumTimeSamples, mutAttr.mNumValues,
                mutAttr.mSampleTimes);
    case kFnKatAttributeTypeFloat:
        return toTypedAttribute<FnAttribute::FloatAttribute, float>(mutAttr.mValues,
                mutAttr.mTupleSize, mutAttr.mNumTimeSamples, mutAttr.mNumValues,
                mutAttr.mSampleTimes);
    case kFnKatAttributeTypeDouble:
        return toTypedAttribute<FnAttribute::DoubleAttribute, double>(mutAttr.mValues,
                mutAttr.mTupleSize, mutAttr.mNumTimeSamples, mutAttr.mNumValues,
                mutAttr.mSampleTimes);
    }

    KdLogError("Attempted to convert into unsupported type: " << convertToType);
    return {};
}

FnAttribute::Attribute
convertAttribute(const FnAttribute::Attribute& attr,
                 const FnAttribute::StringAttribute& convertToType)
{
    const auto type = getType(convertToType.getValue("", false));

    // no conversion needed
    if (attr.getType() == type) {
        return attr;
    }

    switch (attr.getType()) {
    case kFnKatAttributeTypeInt:
        return convertAttribute<FnAttribute::IntAttribute>(attr, type);
    case kFnKatAttributeTypeFloat:
        return convertAttribute<FnAttribute::FloatAttribute>(attr, type);
    case kFnKatAttributeTypeDouble:
        return convertAttribute<FnAttribute::DoubleAttribute>(attr, type);
    case kFnKatAttributeTypeString:
        return convertAttribute<FnAttribute::StringAttribute>(attr, type);
    }

    KdLogError("Attempted to convert unsupported attr type: " << attr.getType());
    return {};
}

// convenience struct to group operations as numeric or data operations
// numeric operations does not affect the inherent data type and can be
// performed together
// data operations operate on the data type or attribute level and should
// be performed individually
struct OpGroup
{
    OpGroup() {}

    OpGroup(Operation op, const FnAttribute::Attribute& valueAttr)
    {
        if (isDataOperation(op)) {
            mIsDataOp = true;
        }
        mOps.emplace_back(op, valueAttr);
    }

    void
    pushOp(Operation op, const FnAttribute::Attribute& valueAttr)
    {
        if (mIsDataOp && !mOps.empty()) {
            KdLogError("Data operations expected to be a single operation!");
            return;
        }
        mOps.emplace_back(op, valueAttr);
    }

    Operation
    getDataOperation() const
    {
        if (mIsDataOp) {
            assert(mOps.size() == 1);
            return mOps.front().first;
        }

        KdLogError("Attempted to get operation type from non-data op.");
        return {};
    }

    FnAttribute::Attribute
    getDataOpValue() const
    {
        if (mIsDataOp) {
            assert(mOps.size() == 1);
            return mOps.front().second;
        }

        KdLogError("Attempted to get value from non-data op.");
        return {};
    }

    bool mIsDataOp = false;
    OperationVec mOps;
};

struct AttributeOperationOp : public Foundry::Katana::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const FnKat::StringAttribute celAttr = interface.getOpArg(sCEL);
        if (!celAttr.isValid()) {
            return;
        }

        FnGeolibServices::FnGeolibCookInterfaceUtils::MatchesCELInfo info;
        FnGeolibServices::FnGeolibCookInterfaceUtils::matchesCEL(info,
                                                                 interface,
                                                                 celAttr);
        if (!info.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!info.matches) {
            return;
        }

        // The 'attributeName' and 'operation' attributes are required
        // If the operation is a binary operation, the 'value' attribute
        // is also required
        const FnAttribute::StringAttribute attributeNameAttr =
                                             interface.getOpArg(sAttributeName);
        if (!attributeNameAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "'attributeName' attribute not set");
            return;
        }

        const std::string attributeName = attributeNameAttr.getValue();

        const FnAttribute::StringAttribute operationAttr =
                                                 interface.getOpArg(sOperation);
        if (!operationAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "'operation' attribute not set");
            return;
        }

        const Operation operation = toOperation(operationAttr);

        if (operation == Operation::INVALID) {
            std::stringstream ss;
            ss << "Operation '" << operationAttr.getValueCStr() << "' is not supported";
            kodachi::ReportNonCriticalError(interface, ss.str());
            return;
        }

        const bool isExpMath = isExpressionMath(operation);
        FnAttribute::GroupAttribute expMathInputs;
        if (isExpMath) {
            expMathInputs = buildExpressionMathArgs(operation, interface.getOpArg());
        }

        const bool isBinaryOp = isBinaryOperation(operation);

        FnAttribute::Attribute valueAttr = interface.getOpArg(sValue);
        if (isBinaryOp && !isNumberAttr(valueAttr)) {
            FnKat::ReportWarning(interface, "'value' Op Arg is required for binary operation");
            return;
        }

        const bool isConvertOp = isConversionOperation(operation);
        if (isConvertOp) {
            valueAttr = interface.getOpArg(sConvertTo);
            if (!valueAttr.isValid() || valueAttr.getType() != kFnKatAttributeTypeString) {
                FnKat::ReportWarning(interface, "'convert_to' Op Arg is required for conversion operation");
                            return;
            }
        }

        const bool isCopyOp = isCopyOperation(operation);
        if (isCopyOp) {
            valueAttr = interface.getOpArg(sCopyTo);
            if (!valueAttr.isValid() || valueAttr.getType() != kFnKatAttributeTypeString) {
                FnKat::ReportWarning(interface, "'copy_to' Op Arg is required for copy operation");
                            return;
            }
        }

        // Deferred mode can be used to store operations for attributes that
        // haven't been set yet. If we're in deferred mode, add the operation
        // data to the attributeOperations stack, and return. The
        // AttributeOperationResolveOp will apply the operation later.
        const FnAttribute::IntAttribute modeAttr = interface.getOpArg(sMode);
        const bool deferred = (modeAttr.getValue(0, false) == 1);

        if (deferred) {
            // The location will have a group attribute structure that looks like:
            // -attributeOperations
            //      -encoded-attr-name
            //          -op1
            //              -operation (StringAttribute)
            //              -value (optional Number Attribute)

            // top-level
            FnAttribute::GroupBuilder attributeOperationsBuilder;
            const FnAttribute::GroupAttribute attributeOperationsAttr =
                                        interface.getAttr(sAttributeOperations);
            const std::string encodedName = FnAttribute::DelimiterEncode(attributeName);

            // attribute-level
            FnAttribute::GroupBuilder operationsBuilder;
            if (attributeOperationsAttr.isValid()) {
                attributeOperationsBuilder.deepUpdate(attributeOperationsAttr);

                const FnAttribute::GroupAttribute operationsAttr =
                            attributeOperationsAttr.getChildByName(encodedName);
                if (operationsAttr.isValid()) {
                    operationsBuilder.deepUpdate(operationsAttr);
                }
            }

            // individual operation
            FnAttribute::GroupBuilder operationBuilder;
            operationBuilder.set(sType, operationAttr);
            if (isBinaryOp || isDataOperation(operation)) {
                operationBuilder.set(sValue, valueAttr);
            }
            else if (isExpMath) {
                operationBuilder.set(sExpressionMathInputs, expMathInputs);
            }

            // operation name
            const FnAttribute::StringAttribute nodeNameAttr = interface.getOpArg("nodeName");
            const std::string operationBaseName = nodeNameAttr.getValue(sOperation, false);

            operationsBuilder.setWithUniqueName(operationBaseName, operationBuilder.build());
            attributeOperationsBuilder.set(encodedName, operationsBuilder.build());
            interface.setAttr(sAttributeOperations, attributeOperationsBuilder.build(), false);

            return;
        }

        // Not deferred, so the attribute should be set
        // If not, check if we should cook the daps and try again
        FnAttribute::Attribute attribute = interface.getAttr(attributeName);
        if (!attribute.isValid()) {

            const FnAttribute::IntAttribute cookDapsAttr = interface.getOpArg(sCookDaps);
            const bool cookDaps = cookDapsAttr.getValue(true, false);
            if (cookDaps) {
                const auto cookedDaps = kodachi::ThreadSafeCookDaps(interface, "");

                attribute = cookedDaps.getChildByName(attributeName);
            }

            if (!attribute.isValid()) {
                FnKat::ReportWarning(interface, "attribute is not set");
                return;
            }
        }

        if (isExpMath) {
            FnAttribute::Attribute attribute = interface.getAttr(attributeName);
            FnAttribute::Attribute newVal =
                    applyExpressionMathOp(interface.getInputLocationPath(),
                                          attribute,
                                          {{operation, expMathInputs}});

            if (newVal.isValid()) {
                interface.setAttr(attributeName, newVal);
            }
            return;
        }

        if (isCopyOp) {
            const std::string dstAttr = FnAttribute::StringAttribute(valueAttr).getValue("", false);
            if (dstAttr.empty()) {
                FnKat::ReportWarning(interface, "destination attribute is invalid");
                return;
            }
            interface.copyAttr(dstAttr, attributeName);
            return;
        }

        if (isNumericOp(operation) && !isNumberAttr(attribute)) {
            FnKat::ReportWarning(interface,
                               "cannot perform operations on non-number attributes");
            return;
        }

        try {
            const FnAttribute::Attribute newValue = isConvertOp ?
                           convertAttribute(attribute, FnAttribute::StringAttribute(valueAttr)) :
                           applyOperations(attribute, {{operation, valueAttr}});

            if (newValue.isValid()) {
                interface.setAttr(attributeName, newValue);
            }
        } catch (const std::exception& e) {
            std::stringstream ss;
            ss << "Exception applying immediate operations: " << e.what();
            kodachi::ReportNonCriticalError(interface, ss.str());
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;
        builder.setNumInputs(1);

        builder.setSummary("Apply a mathematical operation to an attribute");
        builder.setHelp("Most operations from the lua.math and c++ cmath libraries are available.");

        OpArgDescription attrNameDesc(AttrTypeDescription::kTypeStringAttribute, sAttributeName);
        attrNameDesc.setOptional(false);
        attrNameDesc.setDescription("The name of the attribute to apply the operation to.");

        OpArgDescription operationDesc(AttrTypeDescription::kTypeStringAttribute, sOperation);
        operationDesc.setOptional(false);
        operationDesc.setDescription("The operation to apply to the attribute.");

        OpArgDescription valueDesc(AttrTypeDescription::kTypeDoubleAttribute, sValue);
        valueDesc.setOptional(false);
        valueDesc.setDescription("Required for binary operations");

        OpArgDescription convertToDesc(AttrTypeDescription::kTypeStringAttribute, sConvertTo);
        convertToDesc.setOptional(false);
        convertToDesc.setDescription("Required for 'convert' operations");

        OpArgDescription modeDesc(AttrTypeDescription::kTypeIntAttribute, sMode);
        modeDesc.setOptional(true);
        modeDesc.setDefaultValue(FnAttribute::IntAttribute(0));
        modeDesc.setDescription("immediate (0) or deferred(1). Immediate applies the operation immediately. Deferred adds the operation to the attributeOperations stack, and will be applied by the AttributeOperations implicit resolver.");

        OpArgDescription cookDapsDesc(AttrTypeDescription::kTypeIntAttribute, sCookDaps);
        cookDapsDesc.setOptional(true);
        cookDapsDesc.setDefaultValue(FnAttribute::IntAttribute(true));
        cookDapsDesc.setDescription("Only applies to immediate mode. If set to true, the operation will be applied to the attribute's default value if not set.");

        builder.describeOpArg(OpArgDescription(AttrTypeDescription::kTypeStringAttribute, sCEL));
        builder.describeOpArg(attrNameDesc);
        builder.describeOpArg(operationDesc);
        builder.describeOpArg(valueDesc);
        builder.describeOpArg(convertToDesc);
        builder.describeOpArg(modeDesc);
        builder.describeOpArg(cookDapsDesc);

        return builder.build();
    }
};

struct AttributeOperationResolveOp : public Foundry::Katana::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const FnAttribute::GroupAttribute attributeOperationsAttr =
                                        interface.getAttr(sAttributeOperations);
        if (!attributeOperationsAttr.isValid()) {
            // nothing to do
            return;
        }

        for (std::int64_t idx = 0; idx < attributeOperationsAttr.getNumberOfChildren(); ++idx) {
            const std::string attributeName =
                    FnAttribute::DelimiterDecode(attributeOperationsAttr.getChildName(idx));

            FnAttribute::Attribute attribute = interface.getAttr(attributeName);
            if (!attribute.isValid()) {
                // Assume we always want to cook the daps in this case, since
                // the whole operation stack will fail otherwise
                const auto cookedDaps = kodachi::ThreadSafeCookDaps(interface, "");
                attribute = cookedDaps.getChildByName(attributeName);

                if (!attribute.isValid()) {
                    std::stringstream ss;
                    ss << "Invalid attribute '" << attributeName << "'";
                    FnKat::ReportWarning(interface, ss.str());
                    continue;
                }
            }

            const FnAttribute::GroupAttribute operations =
                    attributeOperationsAttr.getChildByIndex(idx);

            // separate regular ops from conversion ops
            // because coversion ops can cause a change in attr type
            // here we group operations into OpGroups until a conversion op
            // is encountered, in which we treat the convert op as its own OpGroup
            std::vector<OpGroup> operationVecs;

            for (std::int64_t opIdx = 0; opIdx < operations.getNumberOfChildren(); ++opIdx) {
                const FnAttribute::GroupAttribute operationAttr = operations.getChildByIndex(opIdx);
                const FnAttribute::StringAttribute opTypeAttr =
                                           operationAttr.getChildByName(sType);

                const Operation operation = toOperation(opTypeAttr);
                if (operation == Operation::INVALID) {
                    std::stringstream ss;
                    ss << "Skipping invalid operation '"
                       << opTypeAttr.getValueCStr() << "'";
                    FnKat::ReportWarning(interface, ss.str());
                    continue;
                }

                const FnAttribute::Attribute valueAttr =
                                           operationAttr.getChildByName(sValue);

                if (isDataOperation(operation)) {
                    // data ops are its own operation vec group
                    operationVecs.push_back(OpGroup(operation, valueAttr));
                    continue;
                }

                if (isBinaryOperation(operation) && !isNumberAttr(valueAttr)) {
                    FnKat::ReportWarning(interface,
                                         "'value' is not a valid number Op Arg");
                    continue;
                }

                if (operationVecs.empty() || operationVecs.back().mIsDataOp) {
                    operationVecs.push_back(OpGroup());
                }

                if (isExpressionMath(operation)) {
                    FnAttribute::Attribute inputsAttr =
                            operationAttr.getChildByName(sExpressionMathInputs);
                    operationVecs.back().pushOp(operation, inputsAttr);
                }
                else {
                    operationVecs.back().pushOp(operation, valueAttr);
                }
            }

            try {
                FnAttribute::Attribute newValue = attribute;
                uint errorCount = 0;
                for (const auto& opGroup : operationVecs) {

                    if (!opGroup.mIsDataOp && !isNumberAttr(newValue)) {
                        errorCount++;
                        continue;
                    }

                    if (!opGroup.mIsDataOp) {
                        const Operation operation = opGroup.mOps.front().first;
                        if (isExpressionMath(operation)) {
                            newValue = applyExpressionMathOp(interface.getInputLocationPath(),
                                                             newValue,
                                                             opGroup.mOps);
                        }
                        else {
                            newValue = applyOperations(newValue, opGroup.mOps);
                        }
                    } else {
                        switch (opGroup.getDataOperation()) {
                        case Operation::CONVERT:
                            newValue = convertAttribute(newValue,
                                    FnAttribute::StringAttribute(opGroup.getDataOpValue()));
                            break;
                        case Operation::COPY:
                        {
                            const std::string dstAttr =
                                    FnAttribute::StringAttribute(opGroup.getDataOpValue()).getValue("", false);
                              if (dstAttr.empty()) {
                                  FnKat::ReportWarning(interface, "destination attribute is invalid");
                                  continue;
                              }
                              // we want to copy the current state of the operated-on values,
                              // so use setAttr here instead of copy attr
                              interface.setAttr(dstAttr, newValue);
                        }
                            break;
                        default:
                            errorCount++;
                            continue;
                        }
                    }

                    if (!newValue.isValid()) {
                        FnKat::ReportWarning(interface, "Invalid operation result encountered.");
                        break;
                    }
                }

                if (errorCount) {
                    std::stringstream ss;
                    ss << "skipped " << errorCount << " invalid operations.";
                    FnKat::ReportWarning(interface, ss.str());
                }

                if (newValue.isValid()) {
                    interface.setAttr(attributeName, newValue);
                }
            } catch (const std::exception& e) {
                std::stringstream ss;
                ss << "Exception applying deferred operations: " << e.what();
                kodachi::ReportNonCriticalError(interface, ss.str());
            }
        }

        interface.deleteAttr(sAttributeOperations);
    }
};

DEFINE_GEOLIBOP_PLUGIN(AttributeOperationOp)
DEFINE_GEOLIBOP_PLUGIN(AttributeOperationResolveOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(AttributeOperationOp, "AttributeOperation", 0, 1);
    REGISTER_PLUGIN(AttributeOperationResolveOp, "AttributeOperationResolve", 0, 1);
}

