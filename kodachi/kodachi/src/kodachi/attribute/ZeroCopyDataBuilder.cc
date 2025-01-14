// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "ZeroCopyDataBuilder.h"

namespace
{

// Builds and holds a 2D array of C-Strings (const char***)
class ManagedCStringArray {
public:
    ManagedCStringArray(std::vector<std::vector<std::string>> const& contextData)
        : mRowCount(contextData.size())
    {
        if (mRowCount == 0) {
            return;
        }

        mColumnCount = contextData.begin()->size();
        if (mColumnCount == 0) {
            return;
        }

        mData = new const char**[mRowCount];
        for (std::size_t row = 0; row < mRowCount; ++row) {
            mData[row] = new const char*[mColumnCount];

            for (std::size_t col = 0; col < mColumnCount; ++col) {
                mData[row][col] = contextData[row][col].c_str();
            }
        }
    }

    ~ManagedCStringArray()
    {
        if (mData == nullptr) {
            return;
        }

        for (std::size_t row = 0; row < mRowCount; ++row) {
            delete [] mData[row];
            mData[row] = nullptr;
        }

        delete [] mData;
        mData = nullptr;
    }

    const char*** data() const { return mData; }

private:
    std::size_t mRowCount    = 0;
    std::size_t mColumnCount = 0;

    const char*** mData = nullptr;
};

} // namespace anonymous

namespace kodachi
{
    template<>
    kodachi::StringAttribute
    ZeroCopyDataBuilder<kodachi::StringAttribute>::build()
    {
        if ( mData.size() == 0 ) {
            // Return an attribute representing an empty array of the given
            // tuple size
            return kodachi::StringAttribute((const char**) 0x0, 0, mTupleSize);
        }

        const std::vector<float> timeSamples       = getTimeSamples();
        const std::size_t timeSampleCount          = timeSamples.size();
        const std::size_t valuesPerTimeSampleCount = mData.begin()->second.size();

        ContextData<std::vector<std::string>>* contextData =
                new ContextData<std::vector<std::string>>();
        contextData->reserve(timeSampleCount);

        for (std::size_t idx = 0; idx < timeSampleCount; ++idx) {
            contextData->emplace_back(
                    std::move(mData[timeSamples[idx]]));
        }

        ManagedCStringArray values(*contextData);
        if (values.data() == nullptr) {
            return { };
        }

        return StringAttribute(timeSamples.data(),
                               (std::int64_t) timeSamples.size(),
                               (const char***) values.data(),
                               (std::int64_t) valuesPerTimeSampleCount,
                               mTupleSize,
                               contextData,
                               ZeroCopyDataBuilder::DeleteContextData);
    }

} // namespace kodachi

