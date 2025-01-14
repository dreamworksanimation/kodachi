// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi_moonray/kodachi_geometry/PrimitiveAttributeUtil.h>

#include <kodachi/attribute/Attribute.h>
#include <rendering/shading/PrimitiveAttribute.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>

namespace {

// Moonray does not allow multi-sampled bool,
// int or string primitive attributes
template <class value_type, class attr_type>
void
addIntegralPrimitiveAttribute(arras::shading::PrimitiveAttributeTable& primitiveAttributeTable,
                              const arras::shading::AttributeKey& attrKey,
                              arras::shading::AttributeRate rate,
                              const attr_type& valueAttr,
                              const kodachi::IntAttribute& indexAttr,
                              const attr_type& indexedValueAttr,
                              const arras::rdl2::Geometry* geometry)
{
    if (indexAttr.isValid() && indexedValueAttr.isValid()) {
        primitiveAttributeTable.addAttribute(
                arras::shading::TypedAttributeKey<value_type>(attrKey),
                rate,
                kodachi_moonray::unpackIndexedValue<value_type, attr_type>(indexAttr, indexedValueAttr));
    } else if (valueAttr.isValid()) {
        const auto sample = valueAttr.getNearestSample(0.f);

        primitiveAttributeTable.addAttribute(
                arras::shading::TypedAttributeKey<value_type>(attrKey),
                rate,
                std::vector<value_type>(sample.begin(), sample.end()));
    } else {
        geometry->error("Error adding primitive attribute '", attrKey.getName(), "'");
    }
}

template <typename scalar_type>
bool
interpolateToFloatingPointVector(const kodachi::DataAttribute& data,
                                 scalar_type* fillArray, float motionStep)
{
    const auto dataType = data.getType();

    if (dataType == kodachi::kAttrTypeFloat) {
        // float to float, so we can use the fillArray directly
        const kodachi::FloatAttribute floatData(data);
        std::vector<float> interpolatedData(data.getNumberOfValues());
        floatData.fillInterpSample(interpolatedData.data(), interpolatedData.size(), motionStep);

        std::copy(interpolatedData.begin(), interpolatedData.end(), fillArray);

        return true;
    } else if (dataType == kodachi::kAttrTypeDouble) {
        // we need to downconvert to float, so interpolate to double first
        const kodachi::DoubleAttribute doubleData(data);

        std::vector<double> interpolatedData(data.getNumberOfValues());
        doubleData.fillInterpSample(interpolatedData.data(), interpolatedData.size(), motionStep);

        std::copy(interpolatedData.begin(), interpolatedData.end(), fillArray);

        return true;
    }

    return false;
}

// There can be cases where float data is passed in as double, so we need
// to downcast accordingly
template <class value_type, typename scalar_type = typename value_type::Scalar>
void
addFloatingPointPrimitiveAttribute(arras::shading::PrimitiveAttributeTable& primitiveAttributeTable,
                                   const arras::shading::AttributeKey& attrKey,
                                   arras::shading::AttributeRate rate,
                                   const kodachi::DataAttribute& valueAttr,
                                   const kodachi::IntAttribute& indexAttr,
                                   const kodachi::DataAttribute& indexedValueAttr,
                                   const std::vector<float>& motionSteps,
                                   const arras::rdl2::Geometry* geometry)
{
    constexpr size_t tupleSize = sizeof(value_type) / sizeof(scalar_type);

    std::vector<std::vector<value_type>> data;

    if (indexAttr.isValid() && indexedValueAttr.isValid()) {
        const size_t numTuples = indexAttr.getNumberOfValues();

        if (indexedValueAttr.getNumberOfTimeSamples() > 1) {
            for (const auto motionStep : motionSteps) {
                std::vector<scalar_type> indexedSample(indexedValueAttr.getNumberOfValues());

                if (interpolateToFloatingPointVector(indexedValueAttr, indexedSample.data(), motionStep)) {
                    std::vector<value_type> sampleData(numTuples);

                    kodachi_moonray::unpackIndexedValue(indexAttr, indexedSample.data(),
                            reinterpret_cast<scalar_type*>(sampleData.data()), tupleSize);

                    data.emplace_back(std::move(sampleData));
                } else {
                    geometry->error("Error interpolating indexedValue of primitive attribute '",
                                    attrKey.getName(), "' to float");
                    return;
                }
            }
        } else {
            std::vector<value_type> sampleData(numTuples);
            scalar_type* array = reinterpret_cast<scalar_type*>(sampleData.data());

            const auto valueType = indexedValueAttr.getType();
            if (valueType == kodachi::kAttrTypeFloat) {
                const kodachi::FloatAttribute floatAttr(indexedValueAttr);
                const auto indexedSample = floatAttr.getNearestSample(0.f);
                kodachi_moonray::unpackIndexedValue(indexAttr, indexedSample.data(), array, tupleSize);
            } else if (valueType == kodachi::kAttrTypeDouble) {
                const kodachi::DoubleAttribute doubleAttr(indexedValueAttr);
                const auto indexedSample = doubleAttr.getNearestSample(0.f);
                kodachi_moonray::unpackIndexedValue(indexAttr, indexedSample.data(), array, tupleSize);
            } else {
                geometry->error("indexedValue attribute of primitive attribute '",
                                attrKey.getName(), "' is not float or double");
                return;
            }

            data.emplace_back(std::move(sampleData));
        }
    } else if (valueAttr.isValid()) {
        const size_t numTuples = valueAttr.getNumberOfValues() / tupleSize;

        if (valueAttr.getNumberOfTimeSamples() > 1) {
            for (const auto motionStep : motionSteps) {
                std::vector<value_type> interpolatedSample(numTuples);
                scalar_type* array = reinterpret_cast<scalar_type*>(interpolatedSample.data());

                if (interpolateToFloatingPointVector(valueAttr, array, motionStep)) {
                    data.emplace_back(std::move(interpolatedSample));
                } else {
                    geometry->error("Error interpolating value of primitive attribute '",
                                    attrKey.getName(), "' to float");
                    return;
                }
            }
        } else {
            std::vector<value_type> sampleData(numTuples);
            scalar_type* array = reinterpret_cast<scalar_type*>(sampleData.data());

            const auto valueType = valueAttr.getType();
            if (valueType == kodachi::kAttrTypeFloat) {
                const kodachi::FloatAttribute floatAttr(valueAttr);
                const auto floatSample = floatAttr.getNearestSample(0.f);
                std::copy(floatSample.begin(), floatSample.end(), array);
            } else if (valueType == kodachi::kAttrTypeDouble) {
                const kodachi::DoubleAttribute doubleAttr(valueAttr);
                const auto doubleSample = doubleAttr.getNearestSample(0.f);
                std::copy(doubleSample.begin(), doubleSample.end(), array);
            } else {
                geometry->error("value attribute of primitive attribute '",
                                attrKey.getName(), "' is not float or double");
                return;
            }

            data.emplace_back(std::move(sampleData));
        }
    } else {
        geometry->error("Error adding primitive attribute '", attrKey.getName(), "'");
        return;
    }

    primitiveAttributeTable.addAttribute(
            arras::shading::TypedAttributeKey<value_type>(attrKey),
            rate, std::move(data));
}

} // anonymous namespace

namespace kodachi_moonray {

// Scope and interpolationType attrs can map to different AttributeRates depending on the geometry
// so let the calling procedural determine the rate
using RateFunc = std::function<arras::shading::AttributeRate(const kodachi::StringAttribute&,
                                                             const kodachi::StringAttribute&)>;

void processArbitraryData(const kodachi::GroupAttribute& arbitraryAttrs,
                          arras::shading::PrimitiveAttributeTable& primitiveAttributeTable,
                          const arras::shading::AttributeKeySet& requestedAttributes,
                          const std::vector<float>& motionSteps,
                          const arras::rdl2::Geometry* geometry,
                          const RateFunc& rateFunc)
{
    for (const auto& attrKey : requestedAttributes) {
        const std::string attrName(attrKey.getName());

        const kodachi::GroupAttribute arbAttr =
                arbitraryAttrs.getChildByName(attrName);

        if (!arbAttr.isValid()) {
            geometry->debug("requested attribute '", attrName, "' not found.");
            continue;
        }

        // attribute rate //
        const kodachi::StringAttribute scopeAttr = arbAttr.getChildByName("scope");
        if (!scopeAttr.isValid()) {
            geometry->error("Arbitrary attribute '", attrName, "' is missing 'scope'");
            continue;
        }
        const kodachi::StringAttribute interpAttr = arbAttr.getChildByName("interpolationType");

        const arras::shading::AttributeRate rate = rateFunc(scopeAttr, interpAttr);

        // attribute value //
        const kodachi::Attribute valueAttr = arbAttr.getChildByName("value");

        const kodachi::IntAttribute indexAttr = arbAttr.getChildByName("index");
        const kodachi::Attribute indexedValueAttr = arbAttr.getChildByName("indexedValue");

        if (!valueAttr.isValid() && !(indexAttr.isValid() && indexedValueAttr.isValid())) {
            geometry->error("Arbitrary attribute '", attrName, "' is missing 'value' or 'index' and 'indexedValue'");
            continue;
        }

        // attribute type //
        // we'll try to use the requested type, if the attribute can't be interpreted as that type,
        // use the given type
        const arras::rdl2::AttributeType type = attrKey.getType();
        switch (type) {
        case arras::rdl2::TYPE_BOOL:
            addIntegralPrimitiveAttribute<arras::rdl2::Bool, kodachi::IntAttribute>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, geometry);
            break;
        case arras::rdl2::TYPE_INT:
            addIntegralPrimitiveAttribute<arras::rdl2::Int, kodachi::IntAttribute>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, geometry);
            break;
        case arras::rdl2::TYPE_LONG:
            addIntegralPrimitiveAttribute<arras::rdl2::Long, kodachi::IntAttribute>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, geometry);
            break;
        case arras::rdl2::TYPE_STRING:
            addIntegralPrimitiveAttribute<arras::rdl2::String, kodachi::StringAttribute>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, geometry);
            break;
        case arras::rdl2::TYPE_FLOAT:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Float, float>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_RGB:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Rgb>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_RGBA:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Rgba>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_VEC2F:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Vec2f>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_VEC3F:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Vec3f>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_MAT4F:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Mat4f>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_DOUBLE:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Double, double>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_VEC2D:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Vec2d>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_VEC3D:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Vec3d>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_VEC4D:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Vec4d>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        case arras::rdl2::TYPE_MAT4D:
            addFloatingPointPrimitiveAttribute<arras::rdl2::Mat4d>(
                    primitiveAttributeTable, attrKey, rate,
                    valueAttr, indexAttr, indexedValueAttr, motionSteps, geometry);
            break;
        }
    } // end requested attribute loop
}

template <class attr_t, typename>
std::unique_ptr<ArbitraryDataBuilderBase>
createArbitraryDataBuilder(const kodachi::StringAttribute& iScope,
                           const kodachi::StringAttribute& iInputType,
                           const kodachi::IntAttribute& iElementSize,
                           const size_t tupleSize)
{
  std::unique_ptr<ArbitraryDataBuilder<attr_t>> db(
          new ArbitraryDataBuilder<attr_t>(tupleSize));
  db->mScope = iScope;
  db->mInputType = iInputType;
  db->mElementSize = iElementSize;

  return std::move(db);
}

std::unique_ptr<ArbitraryDataBuilderBase>
initArbitraryDataBuilder(const kodachi::StringAttribute& iScope,
                         const kodachi::StringAttribute& iInputType,
                         const kodachi::IntAttribute& iElementSize)
{
    static const std::string kFloat("float");

    const auto dataPair = getInputTypeData(iInputType);

    if (dataPair.first != PrimitiveType::UNKNOWN) {
        switch(dataPair.first) {
        case PrimitiveType::FLOAT: {
            size_t tupleSize = dataPair.second;
            if (iInputType == kFloat && iElementSize.isValid()) {
                tupleSize = iElementSize.getValue();
            }
            return createArbitraryDataBuilder<kodachi::FloatAttribute>(
                    iScope, iInputType, iElementSize, tupleSize);
            }
            break;
        case PrimitiveType::DOUBLE:
            return createArbitraryDataBuilder<kodachi::DoubleAttribute>(
                    iScope, iInputType, iElementSize, dataPair.second);
            break;
        case PrimitiveType::INT:
            return createArbitraryDataBuilder<kodachi::IntAttribute>(
                    iScope, iInputType, iElementSize, dataPair.second);
            break;
        case PrimitiveType::STRING:
            return createArbitraryDataBuilder<kodachi::StringAttribute>(
                    iScope, iInputType, iElementSize, dataPair.second);
            break;
        }
    }

    // unsupported type
    return nullptr;
}

// returns primitive data type and tuple size
// based on inputType
std::pair<PrimitiveType, size_t>
getInputTypeData(const kodachi::StringAttribute& inputType)
{
    static const std::unordered_map<kodachi::StringAttribute, std::pair<PrimitiveType, size_t>,
        kodachi::AttributeHash> kInputTypeMap
    {
        { "float"   , { PrimitiveType::FLOAT , 1 }},
        { "double"  , { PrimitiveType::DOUBLE, 1 }},
        { "int"     , { PrimitiveType::INT   , 1 }},
        // * not natively valid to katana, but supported
        //   by moonray -------------------------------
        { "unsigned", { PrimitiveType::INT   , 1 }},
        { "uint"    , { PrimitiveType::INT   , 1 }},
        { "long"    , { PrimitiveType::INT   , 1 }},
        { "ulong"   , { PrimitiveType::INT   , 1 }},
        { "bool"    , { PrimitiveType::INT   , 1 }},
        // -------------------------------------------- *
        { "string"  , { PrimitiveType::STRING, 1 }},
        { "color3"  , { PrimitiveType::FLOAT , 3 }},
        { "color4"  , { PrimitiveType::FLOAT , 4 }},
        { "normal2" , { PrimitiveType::FLOAT , 2 }},
        { "normal3" , { PrimitiveType::FLOAT , 3 }},
        { "vector2" , { PrimitiveType::FLOAT , 2 }},
        { "vector3" , { PrimitiveType::FLOAT , 3 }},
        { "vector4" , { PrimitiveType::FLOAT , 4 }},
        { "point2"  , { PrimitiveType::FLOAT , 2 }},
        { "point3"  , { PrimitiveType::FLOAT , 3 }},
        { "point4"  , { PrimitiveType::FLOAT , 4 }},
        { "matrix9" , { PrimitiveType::FLOAT , 9 }},
        { "matrix16", { PrimitiveType::FLOAT , 16 }}
    };

    const auto it = kInputTypeMap.find(inputType);
    if (it != kInputTypeMap.end()) {
        return (*it).second;
    }

    return { PrimitiveType::UNKNOWN, 0 };
}

// validates the arbitrary data against the provided data set size
bool
validateArbitraryAttribute(const kodachi::GroupAttribute& arbitraryAttr,
                           const size_t pointCount,
                           const size_t vertexCount,
                           const size_t faceCount,
                           std::string& error)
{
    static const std::string kScope       ("scope");
    static const std::string kInputType   ("inputType");
    static const std::string kElementSize ("elementSize");
    static const std::string kValue       ("value");
    static const std::string kIndexedValue("indexedValue");
    static const std::string kIndex       ("index");

    static const std::string kPrimitive  ("primitive");
    static const std::string kFace       ("face");
    static const std::string kPoint      ("point");
    static const std::string kVertex     ("vertex");

    std::ostringstream err;

    const kodachi::StringAttribute scope =
            arbitraryAttr.getChildByName(kScope);
    const kodachi::StringAttribute inputType =
            arbitraryAttr.getChildByName(kInputType);

    if (!scope.isValid() || !inputType.isValid()) {
        error = "Missing scope and/or inputType.";
        return false;
    }

    // expected primitive data type and tuplesize
    const auto dataPair = getInputTypeData(inputType);
    if (dataPair.first == PrimitiveType::UNKNOWN) {
        err << "Unsupported input type: " <<
                inputType.getValue("(missing)", false);
        error = err.str();
        return false;
    }

    kodachi::DataAttribute data =
            arbitraryAttr.getChildByName(kValue);
    bool isIndexed = false;
    if (!data.isValid()) {
        // indexed - data count depends on size of index list
        isIndexed = true;
        data = arbitraryAttr.getChildByName(kIndex);
        if (!data.isValid()) {
            error = "Missing data values.";
            return false;
        }
    }

    // tuple size depends on input type, *unless* it's
    // float primitive type, which means it
    // could depend on elementSize ... *unless* it's indexed
    size_t tupleSize = dataPair.second;
    if (dataPair.first == PrimitiveType::FLOAT && !isIndexed) {
        const kodachi::IntAttribute elementSize =
                    arbitraryAttr.getChildByName(kElementSize);
        if (elementSize.isValid()) {
            tupleSize = elementSize.getValue();
        }
    }

    // validate data sizes
    size_t count = data.getNumberOfValues() / tupleSize;
    if (scope == kPrimitive && count != 1) {
        err << "Data count mismatch; Expected scope 'primitive' count of 1, "
            << "got " << count;
        error = err.str();
        return false;
    } else if (scope == kFace && count != faceCount) {
        err << "Data count mismatch; Expected scope 'face' count of "
            << faceCount << ", got " << count;
        error = err.str();
        return false;
    } else if (scope == kPoint && count != pointCount) {
        err << "Data count mismatch; Expected scope 'point' count of "
            << pointCount << ", got " << count;
        error = err.str();
        return false;
    } else if (scope == kVertex && count != vertexCount) {
        err << "Data count mismatch; Expected scope 'vertex' count of "
            << vertexCount << ", got " << count;
        error = err.str();
        return false;
    }

    // validate data type
    if (isIndexed) {
        data = arbitraryAttr.getChildByName(kIndexedValue);
        if (!data.isValid()) {
            error = "Missing data values.";
            return false;
        }
    }

    switch (dataPair.first) {
    case PrimitiveType::FLOAT:
        if (!kodachi::FloatAttribute(data).isValid()) {
            error = "Invalid data type, expected 'float'.";
            return false;
        }
        break;
    case PrimitiveType::DOUBLE:
        if (!kodachi::DoubleAttribute(data).isValid()) {
            error = "Invalid data type, expected 'double'.";
            return false;
        }
        break;
    case PrimitiveType::INT:
        if (!kodachi::IntAttribute(data).isValid()) {
            error = "Invalid data type, expected 'int'.";
            return false;
        }
        break;
    case PrimitiveType::STRING:
        if (!kodachi::StringAttribute(data).isValid()) {
            error = "Invalid data type, expected 'string'.";
            return false;
        }
        break;
    }

    // validated
    return true;
}

} // namespace kodachi_moonray

