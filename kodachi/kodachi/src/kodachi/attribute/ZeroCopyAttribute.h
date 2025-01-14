// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// kodachi
#include <kodachi/ArrayView.h>
#include <kodachi/attribute/Attribute.h>

namespace kodachi {

/**
 * Helper class for using the ZeroCopy constructors of Attributes.
 * These take ownership of an array or vector and delete them when the
 * Attributes refcount drops to zero.
 */
template <class attribute_type>
class ZeroCopyAttribute
{
public:
    using value_type = typename attribute_type::value_type;
    using vector_type = std::vector<value_type>;

    /**
     * Single sample array constructor
     */
    static attribute_type
    create(std::unique_ptr<value_type[]> valueArray,
           int64_t valueCount,
           int64_t tupleSize = 1)
    {
        attribute_type attr(valueArray.get(), // values
                            valueCount,       // valueCount
                            tupleSize,        // tupleSize
                            valueArray.get(), // context
                            ZeroCopyAttribute::DeleteArray); // freeOwnedDataFunc

        // The attribute is now responsible for deleting the data
        valueArray.release();

        return attr;
    }

    /**
     * Multi-sample array constructor.
     * Assumes length of valueArray is nSamples * valueCount
     */
    static attribute_type
    create(array_view<float> sampleTimes,
           std::unique_ptr<value_type[]> valueArray,
           int64_t valueCount,
           int64_t tupleSize = 1)
    {
        const std::size_t numSamples = sampleTimes.size();

        // group the pointers to the start of each sample
        std::vector<const value_type*> values(numSamples);
        for (std::size_t i = 0; i < numSamples; ++i) {
            values[i] = valueArray.get() + (i * valueCount);
        }

        attribute_type attr(sampleTimes.data(), // times
                            numSamples,         // timeCount
                            values.data(),      // values
                            valueCount,         // valueCount
                            tupleSize,          // tupleSize
                            valueArray.get(),   // context
                            ZeroCopyAttribute::DeleteArray); // freeOwnedDataFunc

        valueArray.release();

        return attr;
    }

    /**
     * Single sample vector constructor
     */
    static attribute_type
    create(vector_type valueVec, int64_t tupleSize = 1)
    {
        // There isn't a way to release the data from a vector, so wrap it
        // in a container to be deleted later
        vector_type* contextVec = new vector_type(std::move(valueVec));

        return attribute_type(contextVec->data(), // values
                              contextVec->size(), // valueCount
                              tupleSize,          // tupleSize
                              contextVec,         // context
                              ZeroCopyAttribute::DeleteVector); // freeOwnedDataFunc
    }

    /**
     * Multi-sample vector constructor.
     * Assumes length of valueArray is nSamples * valueCount
     */
    static attribute_type
    create(array_view<float> sampleTimes,
           std::vector<value_type> valueVec,
           std::size_t tupleSize = 1)
    {
        vector_type* contextVec = new vector_type(std::move(valueVec));

        const std::size_t numSamples = sampleTimes.size();
        const std::size_t numValues = contextVec->size() / numSamples;

        // group the pointers to the start of each sample
        std::vector<const value_type*> values(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            values[i] = contextVec->data() + (i * numValues);
        }

        return attribute_type(sampleTimes.data(), // times
                              numSamples,         // timeCount
                              values.data(),      // values
                              numValues,          // valueCount
                              tupleSize,          // tupleSize
                              contextVec,         // context
                              ZeroCopyAttribute::DeleteVector); // freeOwnedDataFunc
    }

private:

    static void
    DeleteArray(void* arr)
    {
        std::default_delete<value_type[]>()(reinterpret_cast<value_type*>(arr));
    }

    static void
    DeleteVector(void* vec)
    {
        std::default_delete<vector_type>()(reinterpret_cast<vector_type*>(vec));
    }
};

// Specializations for StringAttribute since the constructor takes const char*
// not std::string. Also only the multi-sample constructor allows for zero-copy.
template<>
inline StringAttribute
ZeroCopyAttribute<StringAttribute>::create(kodachi::StringVector valueVec,
                                           int64_t tupleSize)
{
    vector_type* contextVec = new vector_type(std::move(valueVec));

    const size_t nValues = contextVec->size();

    std::vector<const char*> charValues(nValues);
    for (size_t i = 0; i < nValues; ++i) {
        charValues[i] = (*contextVec)[i].c_str();
    }

    const std::array<float, 1> times{0.f};
    std::array<const char**, 1> arr{charValues.data()};

    return StringAttribute(times.data(), // times
                           times.size(), // timeCount
                           arr.data(),   // values
                           nValues,      // valueCount
                           tupleSize,    // tupleSize
                           contextVec,   // context
                           ZeroCopyAttribute::DeleteVector);  // freeOwnedDataFunc
}

using ZeroCopyIntAttribute = ZeroCopyAttribute<IntAttribute>;
using ZeroCopyFloatAttribute = ZeroCopyAttribute<FloatAttribute>;
using ZeroCopyDoubleAttribute = ZeroCopyAttribute<DoubleAttribute>;
using ZeroCopyStringAttribute = ZeroCopyAttribute<StringAttribute>;

} // namespace kodachi

