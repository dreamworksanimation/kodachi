// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// kodachi
#include <kodachi/StringView.h>

// foundry
#include <internal/FnAttribute/FnAttribute.h>

// stl
#include <memory>
#include <vector>

namespace kodachi {

using namespace FnAttribute;

// Attribute handle, a pointer used to call into the Attribute suite.
using KdAttributeHandle = FnAttributeHandle;

// AttributeType values. Useful for determining the actual type of an attribute
// when you currently have an Attribute or DataAttribute.
using AttributeType = int32_t;

constexpr AttributeType kAttrTypeNull   = kFnKatAttributeTypeNull;
constexpr AttributeType kAttrTypeInt    = kFnKatAttributeTypeInt;
constexpr AttributeType kAttrTypeFloat  = kFnKatAttributeTypeFloat;
constexpr AttributeType kAttrTypeDouble = kFnKatAttributeTypeDouble;
constexpr AttributeType kAttrTypeString = kFnKatAttributeTypeString;
constexpr AttributeType kAttrTypeGroup  = kFnKatAttributeTypeGroup;
constexpr AttributeType kAttrTypeError  = kFnKatAttributeTypeError;

using Int    = IntAttribute::value_type;
using Float  = FloatAttribute::value_type;
using Double = DoubleAttribute::value_type;
using String = StringAttribute::value_type;

using IntVector    = std::vector<Int>;
using FloatVector  = std::vector<Float>;
using DoubleVector = std::vector<Double>;
using StringVector = std::vector<String>;

using IntArray    = std::unique_ptr<Int[]>;
using FloatArray  = std::unique_ptr<Float[]>;
using DoubleArray = std::unique_ptr<Double[]>;

/**
 * Forward iterator enabling FnAttribute::GroupAttribute to be used with a
 * range-based for loop.
 *
 * Instead of:
 * for (int64_t i = 0; i < groupAttr.getNumberOfChildren(); ++i) {
 *     const std::string childName = groupAttr.getChildName(i);
 *     const FnAttribute::Attribute childAttr = groupAttr.getChildByIndex(i);
 * }
 *
 * Use:
 * for (auto child : groupAttr) {
 *     const kodachi::string_view& childName = child.name;
 *     const kodachi::Attribute& childAttr = child.attribute;
 * }
 */
struct GroupAttributeChild
{
    const kodachi::string_view name;
    const kodachi::Attribute attribute;
};

class GroupAttributeConstIterator :
            public std::iterator<std::forward_iterator_tag, GroupAttributeChild>
{
public:
    GroupAttributeConstIterator(const kodachi::GroupAttribute& attr,
                                int64_t i,
                                int64_t end)
        : mAttr(attr)
        , mIdx(i)
        , mEnd(end)
    {}

    inline GroupAttributeChild operator*() const {
        // call into the suite directly so that we can construct a string_view
        Int nameSize = 0;
        const char* nameStr = kodachi::Attribute::getSuite()->
                getChildNameByIndex(mAttr.getHandle(), mIdx, &nameSize);

        return { kodachi::string_view(nameStr, nameSize),
                 mAttr.getChildByIndex(mIdx) };
    }

    inline GroupAttributeConstIterator& operator++() {
        ++mIdx;
        return *this;
    }

    inline bool operator==(const GroupAttributeConstIterator& other) const {
        return mIdx == other.mIdx;
    }

    inline bool operator!=(const GroupAttributeConstIterator& other) const {
        return !operator==(other);
    }

private:
    const kodachi::GroupAttribute mAttr;
    int64_t mIdx;
    const int64_t mEnd;
};

struct AttributeHash
{
    size_t operator()(const kodachi::Attribute& key) const
    {
        return key.getHash().uint64();
    }

    size_t hash(const kodachi::Attribute& key) const
    {
        return operator()(key);
    }

    bool equal(const kodachi::Attribute& key, const kodachi::Attribute& other) const
    {
        return key == other;
    }
};

void print(std::ostream&, const kodachi::Attribute&, unsigned indent = 0);

} // namespace kodachi

// pretty printer
inline
std::ostream& operator<<(std::ostream& o, const kodachi::Attribute& attribute) {
    kodachi::print(o, attribute);
    return o;
}

// begin() and end() functions need to be in the namespace of the object being
// iterated over for the range-based for to work. GroupAttribute is in
// FnAttribute namespace.
FNATTRIBUTE_NAMESPACE_ENTER
{

inline kodachi::GroupAttributeConstIterator
begin(const kodachi::GroupAttribute& attr)
{
    return kodachi::GroupAttributeConstIterator(attr, 0l, attr.getNumberOfChildren());
}

inline kodachi::GroupAttributeConstIterator
end(const kodachi::GroupAttribute& attr)
{
    const int64_t numChildren = attr.getNumberOfChildren();
    return kodachi::GroupAttributeConstIterator(attr, numChildren, numChildren);
}

}
FNATTRIBUTE_NAMESPACE_EXIT

