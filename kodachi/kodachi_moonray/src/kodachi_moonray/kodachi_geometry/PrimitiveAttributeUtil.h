// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/ArrayView.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyDataBuilder.h>

#include <arras/rendering/shading/AttributeKey.h>
#include <rendering/shading/PrimitiveAttribute.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>

namespace kodachi_moonray {

// Scope can map to different AttributeRates depending on the geometry
// so let the calling procedural determine the rate
using RateFunc = std::function<arras::shading::AttributeRate(const kodachi::StringAttribute&,
                                                          const kodachi::StringAttribute&)>;

void processArbitraryData(const kodachi::GroupAttribute& arbitraryAttrs,
                          arras::shading::PrimitiveAttributeTable& primitiveAttributeTable,
                          const arras::shading::AttributeKeySet& requestedAttributes,
                          const std::vector<float>& motionSteps,
                          const arras::rdl2::Geometry* geometry,
                          const RateFunc& rateFunc);

// for tupleSize == 1
template<class ret_type, class attr_type>
std::vector<ret_type>
unpackIndexedValue(const kodachi::IntAttribute& indexAttr,
                   const attr_type& indexedValueAttr)
{
   const auto index = indexAttr.getNearestSample(0.f);
   const auto indexedValue = indexedValueAttr.getNearestSample(0.f);

   std::vector<ret_type> ret;
   ret.reserve(index.size());

   for (const int32_t i : index) {
       ret.push_back(indexedValue[i]);
   }

   return ret;
}

template <class data_type, typename scalar_type>
void
unpackIndexedValue(const kodachi::IntAttribute& indexAttr,
                   const data_type* indexedValue,
                   scalar_type* dst,
                   size_t tupleSize)
{
    const auto index = indexAttr.getNearestSample(0.f);

    for (const int32_t i : index) {
        const data_type* src = indexedValue + (i * tupleSize);
        std::copy(src, src + tupleSize, dst);
        dst += tupleSize;
    }
}

enum PrimitiveType {
    FLOAT ,
    DOUBLE,
    INT   ,
    STRING,
    UNKNOWN
};

// returns primitive data type and tuple size
// based on inputType
std::pair<PrimitiveType, size_t>
getInputTypeData(const kodachi::StringAttribute& inputType);

struct ArbitraryDataBuilderBase
{
    kodachi::StringAttribute mScope;
    kodachi::StringAttribute mInputType;
    kodachi::IntAttribute mElementSize;

    virtual ~ArbitraryDataBuilderBase() = default;

    // append values from the given data attribute at the provided times
    // able to repeat N times which allows for the modifying of scope
    virtual bool append(const kodachi::DataAttribute& inData,
                        const kodachi::array_view<float> times,
                        const size_t N = 1) = 0;
    // append values from the given arbitrary attribute group
    // at the provided times
    // always unpacks indexed values
    // able to repeat N times which allows for the modifying of scope
    virtual bool append(const kodachi::GroupAttribute& inArbitraryAttribute,
                        const kodachi::array_view<float> times,
                        const size_t N = 1) = 0;

    virtual kodachi::GroupAttribute build() = 0;
};

template <class attr_t,
          typename = typename std::enable_if<
                              std::is_base_of<kodachi::DataAttribute, attr_t>::value>::type>
struct ArbitraryDataBuilder : public ArbitraryDataBuilderBase
{
    using value_t = typename attr_t::value_type;

    kodachi::ZeroCopyDataBuilder<attr_t> mData;

    ArbitraryDataBuilder(const size_t tupleSize = 1)
    : mData(tupleSize)
    { }

    kodachi::GroupAttribute
    build() override
    {
        static const std::string kScope       ("scope");
        static const std::string kInputType   ("inputType");
        static const std::string kElementSize ("elementSize");
        static const std::string kValue       ("value");
        static const std::string kFloat       ("float");

        kodachi::GroupBuilder gb;
        gb.set(kScope, mScope);
        gb.set(kInputType, mInputType);

        // element size only needed for float types
        const auto dataType = getInputTypeData(mInputType);
        if (mInputType == kFloat) {
            gb.set(kElementSize, mElementSize);
        }

        gb.set(kValue, mData.build());
        return gb.build();
    }

    bool
    append(const kodachi::DataAttribute& inData,
           const kodachi::array_view<float> times,
           const size_t N = 1) override
    {
        const attr_t typedData = inData;
        if (typedData.isValid()) {
            const auto samples = typedData.getSamples();

            for (const float t : times) {
                const auto& sample = samples.getNearestSample(t);
                auto& data = mData.get(t);

                for (size_t i = 0; i < N; ++i) {
                    data.insert(data.end(), sample.begin(), sample.end());
                }
            }
            return true;
        }
        return false;
    }

    bool
    append(const kodachi::GroupAttribute& inArbitraryAttribute,
           const kodachi::array_view<float> times,
           const size_t N = 1) override
    {
        static const std::string kValue       ("value");
        static const std::string kIndexedValue("indexedValue");
        static const std::string kIndex       ("index");

        const kodachi::IntAttribute index =
                inArbitraryAttribute.getChildByName(kIndex);
        if (index.isValid()) {
            // indexed value
            attr_t typedValues =
                    inArbitraryAttribute.getChildByName(kIndexedValue);
            if (!typedValues.isValid()) {
                return false;
            }

            const size_t tupleSize = typedValues.getTupleSize();

            typedValues = kodachi::interpToSamples(typedValues, times, tupleSize);

            const auto samples = typedValues.getSamples();

            for (const float t : times) {
                const auto& sample = samples.getNearestSample(t);

                // unpack indexed values for convenience
                std::vector<value_t> unpackedData(
                        index.getNumberOfValues() * tupleSize);
                unpackIndexedValue(index, sample.data(),
                                   unpackedData.data(), tupleSize);

                auto& data = mData.get(t);
                for (size_t i = 0; i < N; ++i) {
                    data.insert(data.end(), unpackedData.begin(), unpackedData.end());
                }
            }
        } else {
            // direct values
            attr_t typedValues =
                    inArbitraryAttribute.getChildByName(kValue);
            if (!typedValues.isValid()) {
                return false;
            }

            typedValues = kodachi::interpToSamples(typedValues, times,
                    typedValues.getTupleSize());

            const auto samples = typedValues.getSamples();

            for (const float t : times) {
                const auto& sample = samples.getNearestSample(t);
                auto& data = mData.get(t);
                for (size_t i = 0; i < N; ++i) {
                    data.insert(data.end(), sample.begin(), sample.end());
                }
            }
        }
        return true;
    }
};

using ArbitraryIntBuilder    = ArbitraryDataBuilder<kodachi::IntAttribute>;
using ArbitraryFloatBuilder  = ArbitraryDataBuilder<kodachi::FloatAttribute>;
using ArbitraryDoubleBuilder = ArbitraryDataBuilder<kodachi::DoubleAttribute>;
using ArbitraryStringBuilder = ArbitraryDataBuilder<kodachi::StringAttribute>;

template <class attr_t,
          typename = typename std::enable_if<
                              std::is_base_of<kodachi::DataAttribute, attr_t>::value>::type>
std::unique_ptr<ArbitraryDataBuilderBase>
createArbitraryDataBuilder(const kodachi::StringAttribute& iScope,
                           const kodachi::StringAttribute& iInputType,
                           const kodachi::IntAttribute& iElementSize,
                           const size_t tupleSize);

std::unique_ptr<ArbitraryDataBuilderBase>
initArbitraryDataBuilder(const kodachi::StringAttribute& iScope,
                         const kodachi::StringAttribute& iInputType,
                         const kodachi::IntAttribute& iElementSize);

// validates the arbitrary data against the provided data set size
bool
validateArbitraryAttribute(const kodachi::GroupAttribute& arbitraryAttr,
                           const size_t pointCount,
                           const size_t vertexCount,
                           const size_t faceCount,
                           std::string& error);

} // namespace kodachi_moonray

