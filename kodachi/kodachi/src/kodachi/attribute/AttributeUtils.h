// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/ArrayView.h>

// foundry
#include <internal/FnAttribute/FnAttributeUtils.h>

// stl
#include <memory>
#include <map>

namespace kodachi {

using namespace FnAttribute;

/**
 * Interpolates a multi-sampled attribute at a specified sample time.
 * Returns an attribute of the same type with one time sample.
 */
template <class attribute_type>
inline attribute_type
_interpolateAttrFP(const attribute_type& attr,
                   const float sampleTime,
                   const int64_t numValues,
                   const int64_t tupleSize)
{
    using value_type     = typename attribute_type::value_type;
    using zero_copy_attr = ZeroCopyAttribute<attribute_type>;

    if (numValues == 1) {
        // no reason to user zero-copy when there is only 1 value
        value_type interpolatedData{};
        attr.fillInterpSample(&interpolatedData, numValues, sampleTime);
        return attribute_type(interpolatedData);
    }

    std::unique_ptr<value_type[]> interpolatedData(new value_type[numValues]);

    attr.fillInterpSample(interpolatedData.get(), numValues, sampleTime);

    return zero_copy_attr::create(std::move(interpolatedData), numValues, tupleSize);
}

inline DataAttribute
interpolateAttr(const DataAttribute& attr,
                const float sampleTime,
                const int64_t tupleSize)
{
    const int64_t numValues = attr.getNumberOfValues();

    if (attr.getNumberOfTimeSamples() <= 1 || numValues == 0) {
        // not enough data to do any interpolation
        return attr;
    }

    switch (attr.getType()) {
    case kAttrTypeInt:
    {
        // can't interpolate Int values, so return an attribute containing the
        // nearest sample
        const IntAttribute::array_type sample =
                IntAttribute(attr).getNearestSample(sampleTime);

        return IntAttribute(sample.data(), numValues, tupleSize);
    }
    case kAttrTypeFloat:
        return _interpolateAttrFP(FloatAttribute(attr), sampleTime, numValues, tupleSize);
    case kAttrTypeDouble:
        return _interpolateAttrFP(DoubleAttribute(attr), sampleTime, numValues, tupleSize);
    case kAttrTypeString:
    {
        const StringAttribute::array_type sample =
                StringAttribute(attr).getNearestSample(sampleTime);

        return StringAttribute(const_cast<const char**>(sample.data()), numValues, tupleSize);
    }
    }

    return {};
}

inline DataAttribute
interpolateAttr(const DataAttribute& attr, const float sampleTime)
{
    return interpolateAttr(attr, sampleTime, attr.getTupleSize());
}

template <class attribute_type>
inline attribute_type
_interpToSamplesFP(const attribute_type& attr,
                   const array_view<float> sampleTimes,
                   const int64_t numValues,
                   const int64_t tupleSize)
{
    using value_type = typename attribute_type::value_type;
    using array_type = typename std::unique_ptr<value_type[]>;
    using zero_copy_attr = ZeroCopyAttribute<attribute_type>;

    array_type dataArray(new value_type[numValues * sampleTimes.size()]);
    for (size_t t = 0; t < sampleTimes.size(); ++t) {
        attr.fillInterpSample(dataArray.get() + (t*numValues),
                numValues, sampleTimes[t]);
    }
    return zero_copy_attr::create(sampleTimes, std::move(dataArray),
                                  numValues, tupleSize);
}

inline DataAttribute
interpToSamples(const DataAttribute& attr,
                const array_view<float> sampleTimes,
                const int64_t tupleSize)
{
    const int64_t numValues = attr.getNumberOfValues();

    if (attr.getNumberOfTimeSamples() <= 1 || numValues == 0) {
        // not enough data to do any interpolation
        return attr;
    }

    // does not support int / string
    switch (attr.getType()) {
    case kAttrTypeFloat:
        return _interpToSamplesFP(FloatAttribute(attr), sampleTimes, numValues, tupleSize);
    case kAttrTypeDouble:
        return _interpToSamplesFP(DoubleAttribute(attr), sampleTimes, numValues, tupleSize);
    }

    return {};
}

/**
 * Helpers for unpacking indexed values
 */
template <class attribute_type>
attribute_type
_unpackIndexedValue(const IntAttribute& indexAttr,
                    const attribute_type& indexedValueAttr,
                    const int64_t tupleSize)
{
    using value_type = typename attribute_type::value_type;

    const auto index = indexAttr.getNearestSample(0.f);
    const auto indexedValue = indexedValueAttr.getNearestSample(0.f);

    std::vector<value_type> ret;
    ret.reserve(index.size() * tupleSize);

    for (const Int i : index) {
        const auto iter = std::begin(indexedValue) + (i * tupleSize);
        ret.insert(ret.end(), iter, iter + tupleSize);
    }

    return ZeroCopyAttribute<attribute_type>::create(std::move(ret), tupleSize);
}

/**
 * Allow for tupleSize override
 */
inline DataAttribute
unpackIndexedValue(const IntAttribute& indexAttr,
                   const DataAttribute& indexedValueAttr,
                   const int64_t tupleSize)
{
    switch (indexedValueAttr.getType()) {
    case kAttrTypeInt:
        return _unpackIndexedValue(indexAttr,
                                   IntAttribute(indexedValueAttr),
                                   tupleSize);
    case kAttrTypeFloat:
        return _unpackIndexedValue(indexAttr,
                                   FloatAttribute(indexedValueAttr),
                                   tupleSize);
    case kAttrTypeDouble:
        return _unpackIndexedValue(indexAttr,
                                   DoubleAttribute(indexedValueAttr),
                                   tupleSize);
    case kAttrTypeString:
        return _unpackIndexedValue(indexAttr,
                                   StringAttribute(indexedValueAttr),
                                   tupleSize);
    }

    return {};
}

inline DataAttribute
unpackIndexedValue(const IntAttribute& indexAttr,
                   const DataAttribute& indexedValueAttr)
{
    return unpackIndexedValue(indexAttr,
                              indexedValueAttr,
                              indexedValueAttr.getTupleSize());
}

/*
 * Indexed attributes iterators that can be modified
 * Convenience class for indexed attributes
 * Allow for iteration through the index and easy access to the values
 * the indices points to. Also allows for appending values.
 * TODO: currently has no mechanism for modifying values
 */
template <typename attribute_type,
          typename = typename std::enable_if<
                     std::is_base_of<DataAttribute, attribute_type>::value>::type>
class IndexedValueAttribute
{
public:
    using value_type = typename attribute_type::value_type;
    using value_vec  = std::vector<value_type>;
    using index_type = typename IntAttribute::value_type;
    using index_vec  = std::vector<index_type>;
    using vec_view   = array_view<value_type>;

    class IndexedValueIterator {
    public:
        explicit IndexedValueIterator(const index_vec::const_iterator& indexIt,
                                      const value_vec& data,
                                      const int64_t tupleSize)
        : _indexIt(indexIt)
        , _dataPtr(data)
        , _tupleSize(tupleSize)
        { }

        // *** data access ***
        // convenience for single tuples
        const value_type& operator*() const {
            return _dataPtr[(*_indexIt) * _tupleSize];
        }

        // get the value at the given tuple index
        // clamps invalid tuple indices within tuple size
        const value_type& get(int64_t tupleIndex = 0) const {
            if (tupleIndex < 0) {
                tupleIndex = 0;
            } else if (tupleIndex >= _tupleSize) {
                tupleIndex = (_tupleSize - 1);
            }

            return _dataPtr[((*_indexIt) * _tupleSize) + tupleIndex];
        }

        typename value_vec::const_iterator dataBegin() const {
            return _dataPtr.begin() + ((*_indexIt) * _tupleSize);
        }

        typename value_vec::const_iterator dataEnd() const {
            return _dataPtr.begin() + ((*_indexIt) * _tupleSize) + _tupleSize;
        }

        const index_type& index() const {
            return (*_indexIt);
        }

        // *** operators ***
        IndexedValueIterator operator++() { _indexIt++; return *this; }
        IndexedValueIterator operator--() { _indexIt--; return *this; }

        bool operator==(const IndexedValueIterator& rhs) {
            return (&_dataPtr == &rhs._dataPtr) && (_indexIt == rhs._indexIt);
        }
        bool operator!=(const IndexedValueIterator& rhs) {
            return (&_dataPtr != &rhs._dataPtr) || (_indexIt != rhs._indexIt);
        }

    private:
        index_vec::const_iterator _indexIt;
        const value_vec& _dataPtr;
        int64_t _tupleSize;
    };

    using iterator = IndexedValueIterator;

    IndexedValueAttribute(int64_t tupleSize)
    : mTupleSize(tupleSize)
    { }

    IndexedValueAttribute(const IntAttribute::array_type& index,
                          const typename attribute_type::array_type& values,
                          int64_t tupleSize)
    : mIndex(index.begin(), index.end())
    , mData(values.begin(), values.end())
    , mTupleSize(tupleSize)
    {
        auto it = begin();
        for (; it != end(); ++it) {
            mDataMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(it.dataBegin(), it.dataEnd()),
                             std::forward_as_tuple(it.index()));
        }
    }

    int64_t getNumberOfTuples() {
        return mTupleSize;
    }

    iterator begin() {
        return iterator(mIndex.begin(), mData, mTupleSize);
    }

    iterator end() {
        return iterator(mIndex.end(), mData, mTupleSize);
    }

    iterator operator[](int64_t idx) const {
        return iterator(mIndex.begin() + idx, mData, mTupleSize);
    }

    // appends a value, if the value exists, simply
    // appends the index, otherwise, create the value and
    // point to it too
    void append(const value_vec& values) {
        assert(values.size() == mTupleSize);

        const auto it = mDataMap.find(values);
        if (it == mDataMap.end()) {
            const size_t last = (mData.size() / mTupleSize);
            mData.insert(mData.end(), values.begin(), values.end());
            mIndex.push_back(last);
            mDataMap.emplace(std::piecewise_construct,
                             std::forward_as_tuple(values.begin(), values.end()),
                             std::forward_as_tuple(last));

        } else {
            mIndex.push_back(it->second);
        }
    }

    // convenience for single tuples
    void append(const value_type& value) {
        assert(mTupleSize == 1);

        value_vec val { value };
        const auto it = mDataMap.find(val);
        if (it == mDataMap.end()) {
            const size_t last = mData.size();
            mData.push_back(value);
            mIndex.push_back(last);
            mDataMap.emplace(std::piecewise_construct,
                              std::forward_as_tuple(val.begin(), val.end()),
                              std::forward_as_tuple(last));

        } else {
            mIndex.push_back(it->second);
        }
    }

     enum BuildMode
     {
         /**
          * Specifies that the builder's contents are cleared following a
          * call to build(). This is the default.
          */
         BuildAndFlush = 0,

         /**
          * Specifies that the builder's contents are retained following a
          * call to build().
          */
         BuildAndRetain = 1
     };

    // updates an existing group attribute with the updated
    // values and indexed values
    void build(GroupBuilder& gb, BuildMode mode = BuildAndFlush) {
        static const std::string kIndex("index");
        static const std::string kIndexedValue("indexedValue");

        gb.del(kIndex);
        gb.del(kIndexedValue);

        gb.set(kIndex, _buildIndex(mode));
        gb.set(kIndexedValue, _buildIndexedValue(mode));
    }

    // returns a group attribute with the updated
    // values and indexed values
    GroupAttribute build(GroupAttribute& inAttr, BuildMode mode = BuildAndFlush) {

        kodachi::GroupBuilder gb;
        gb.deepUpdate(inAttr);

        static const std::string kIndex("index");
        static const std::string kIndexedValue("indexedValue");

        gb.del(kIndex);
        gb.del(kIndexedValue);

        gb.set(kIndex, _buildIndex(mode));
        gb.set(kIndexedValue, _buildIndexedValue(mode));

        return gb.build();
    }

private:
    IntAttribute _buildIndex(BuildMode mode) {
        if (mode == BuildAndFlush) {
            // invalidates the data
            return ZeroCopyAttribute<IntAttribute>::create(std::move(mIndex), 1);
        }

        // BuildAndRetain
        // keeping the data, make a copy
        static const std::string kIndex("index");
        std::vector<index_type> index(mIndex.begin(), mIndex.end());

        return ZeroCopyAttribute<IntAttribute>::create(std::move(index), 1);
    }

    attribute_type _buildIndexedValue(BuildMode mode) {
        if (mode == BuildAndFlush) {
            // invalidates the data
            return ZeroCopyAttribute<attribute_type>::create(std::move(mData), mTupleSize);
        }

        // BuildAndRetain
        // keeping the data, make a copy
        static const std::string kIndexedValue("indexedValue");
        std::vector<value_type> indexedValue(mData.begin(), mData.end());

        return ZeroCopyAttribute<attribute_type>::create(std::move(indexedValue), mTupleSize);
    }

    index_vec mIndex;
    value_vec mData;

    const int64_t mTupleSize;

    std::map<value_vec, index_type> mDataMap;
};

} // namespace kodachi

