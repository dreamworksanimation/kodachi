// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <mutex>
#include <map>
#include <vector>
#include <kodachi/attribute/Attribute.h>

namespace kodachi
{
    /*
     * NOTE: ZeroCopyDataBuilder is based on Katana Plugin API's DataBuilder.
     */
    template<typename AttributeType>
    class ZeroCopyDataBuilder {
    public:
        /**
         * Creates an empty ZeroCopyDataBuilder with a given tuple size
         */
        ZeroCopyDataBuilder(std::int64_t tupleSize = 1) : mTupleSize(tupleSize) {}

        /**
         * Reserves timeSampleDataCount entries in the given timesample data
         */
        void reserve(std::int64_t timeSampleDataCount, float timeSample);

        /**
         *Gets the current size tuple size of the builder
         */
        std::int64_t getTupleSize() const;

        /**
         * Get the current time samples in the builder
         */
        std::vector<float> getTimeSamples() const;

        /**
         * Get a vector at the given data sample
         */
        std::vector<typename AttributeType::value_type>& get(const float timeSample);

        /**
         * Moves timeSampleData into the builder at the given time sample
         */
        void set(std::vector<typename AttributeType::value_type>&& timeSampleData, const float timeSample);

        /**
         * Copies timeSampleData into the builder at the given time sample
         */
        void set(const std::vector<typename AttributeType::value_type>& timeSampleData, const float timeSample);

        /**
         * Removes the time sample data from the builder
         */
        void del(const float timeSample);

        /**
         * NOTE: Don't use this; for every push_back this is going to do a look up in
         * the STL map; instead, use ZeroCopyDataBuilder<T>::get() and directly work on
         * the returned reference.
         *
         * Push back a single data element onto a given timesample
         */
        void push_back(typename AttributeType::value_type data, float timeSample);

        /**
         * Create an attribute from the current data in the builder
         */
        AttributeType build();

    private:
        template <typename TimeSampleDataType = std::vector<typename AttributeType::value_type>>
        using ContextData = std::vector<TimeSampleDataType>;

        template <typename TimeSampleDataType = std::vector<typename AttributeType::value_type>>
        static void
        DeleteContextData(void* data)
        {
            std::default_delete<ContextData<TimeSampleDataType>>()(
                    reinterpret_cast<ContextData<TimeSampleDataType>*>(data));
        }

        std::int64_t mTupleSize = 1;

        using TimeToDataMap = std::map<float, std::vector<typename AttributeType::value_type>>;
        TimeToDataMap mData;
    };

    template<typename AttributeType>
    inline void
    ZeroCopyDataBuilder<AttributeType>::reserve(std::int64_t timeSampleDataCount, float timeSample)
    {
        mData[timeSample].reserve(timeSampleDataCount);
    }

    template<typename AttributeType>
    inline std::int64_t
    ZeroCopyDataBuilder<AttributeType>::getTupleSize() const
    {
        return mTupleSize;
    }

    template<typename AttributeType>
    inline std::vector<float>
    ZeroCopyDataBuilder<AttributeType>::getTimeSamples() const
    {
        std::vector<float> timeSample;
        for (const auto& entry : mData) {
            timeSample.push_back(entry.first);
        }
        return timeSample;
    }

    template<typename AttributeType>
    inline std::vector<typename AttributeType::value_type>&
    ZeroCopyDataBuilder<AttributeType>::get(const float timeSample)
    {
        return mData[timeSample];
    }

    template<typename AttributeType>
    inline void
    ZeroCopyDataBuilder<AttributeType>::set(std::vector<typename AttributeType::value_type>&& timeSampleData, const float timeSample)
    {
        mData[timeSample] = std::move(timeSampleData);
    }

    template<typename AttributeType>
    inline void
    ZeroCopyDataBuilder<AttributeType>::set(const std::vector<typename AttributeType::value_type>& timeSampleData, const float timeSample)
    {
        mData[timeSample] = timeSampleData;
    }

    template<typename AttributeType>
    inline void
    ZeroCopyDataBuilder<AttributeType>::del(const float timeSample)
    {
        mData.erase(timeSample);
    }

    template<class AttributeType>
    inline void
    ZeroCopyDataBuilder<AttributeType>::push_back(typename AttributeType::value_type data, float timeSample)
    {
        mData[timeSample].push_back(data);
    }

    template<typename AttributeType>
    inline AttributeType
    ZeroCopyDataBuilder<AttributeType>::build()
    {
        // Otherwise handle regular attributes
        if ( mData.size() == 0 ) {
            // Return an attribute representing an empty array of the given
            // tuple size
            return AttributeType(0x0, 0, mTupleSize);
        }

        const std::vector<float> timeSamples  = getTimeSamples();
        const std::size_t timeSampleCount     = timeSamples.size();
        const std::size_t valuesPerTimeSample = mData.begin()->second.size();

        // Move sample values out of mData
        ContextData<>* contextData = new ContextData<>();
        contextData->reserve(timeSampleCount);

        std::vector<const typename AttributeType::value_type*> values(timeSampleCount);

        for (std::size_t idx = 0; idx < timeSampleCount; ++idx) {
            contextData->emplace_back(
                    std::move(mData[timeSamples[idx]]));

            values[idx] = contextData->back().data();
        }

        return AttributeType(
                timeSamples.data(),  // time samples
                timeSampleCount,     // number of time samples
                values.data(),       // values per time sample
                valuesPerTimeSample, // number of values per time sample
                mTupleSize,
                contextData,         // context
                ZeroCopyDataBuilder::DeleteContextData); // freeOwnedDataFunc
    }

    using ZeroCopyIntBuilder    = ZeroCopyDataBuilder<kodachi::IntAttribute>;
    using ZeroCopyFloatBuilder  = ZeroCopyDataBuilder<kodachi::FloatAttribute>;
    using ZeroCopyDoubleBuilder = ZeroCopyDataBuilder<kodachi::DoubleAttribute>;

    //------------------------------------------------------------
    // StringBuilder
    //------------------------------------------------------------

    // StringBuilder is typically not recommended for use.
    // As strings are not generally multisampled, a builder is not appropriate.
    // (or efficient) If you'd like the convenience of array initialization,
    // consider using the std::vector<std::string> constructor directly.

    using ZeroCopyStringBuilder = ZeroCopyDataBuilder<kodachi::StringAttribute>;

    template<>
    kodachi::StringAttribute ZeroCopyDataBuilder<kodachi::StringAttribute>::build();

} // namespace kodachi

