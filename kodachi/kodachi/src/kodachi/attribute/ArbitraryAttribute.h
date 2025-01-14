// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/attribute/ZeroCopyAttribute.h>

#include <unordered_map>

namespace kodachi {

/**
 * Convenience wrapper around arbitrary attributes. Provides easy access
 * to scope, type, values or index & indexedValue
 */
struct ArbitraryAttr {
    ArbitraryAttr(const kodachi::GroupAttribute& attr)
    : mAttrGroup(attr)
    , mScope(UNKNOWN)
    , mValueType(kodachi::kAttrTypeNull)
    , mIsIndexed(false)
    , mIsValid(true)
    {
        const auto scopeAttr =
                kodachi::StringAttribute(attr.getChildByName("scope"));
        if (scopeAttr == "primitive") {
            mScope = CONSTANT;
        } else if (scopeAttr == "face") {
            mScope = UNIFORM;
        } else if (scopeAttr == "point") {
            mScope = POINT;
        } else if (scopeAttr == "vertex") {
            mScope = VERTEX;
        } else {
            // assume we must have a scope
            mIsValid = false;
        }

        kodachi::DataAttribute values(attr.getChildByName("value"));
        mValueName = "value";
        if (!values.isValid()) {
            const kodachi::IntAttribute indexAttr = attr.getChildByName("index");
            if (indexAttr.isValid()) {
                mValueName = "indexedValue";
                mIsIndexed = true;
                values = attr.getChildByName("indexedValue");
            }
        }

        if (values.isValid()) {
            mValueType = values.getType();
        } else {
            // must have valid values
            mIsValid = false;
            return;
        }

        kodachi::IntAttribute elementSize(attr.getChildByName("elementSize"));
        mTupleSize = values.getTupleSize();

        // input type
        mInputType = attr.getChildByName("inputType");
        _setInputType(elementSize);
        if (!mInputType.isValid()) {
            mIsValid = false;
        }
    }

    kodachi::AttributeType
    getValueType() const { return mValueType; }

    const kodachi::StringAttribute&
    getInputType() const { return mInputType; }

    int64_t
    getTupleSize() const { return mTupleSize; }

    bool
    isIndexed() const { return mIsIndexed; }

    const kodachi::IntAttribute
    getIndex() const {
        return mAttrGroup.getChildByName("index");;
    }

    const std::string& getValueName() const { return mValueName; }

    template<typename T = kodachi::DataAttribute,
    typename = typename std::enable_if<
    std::is_base_of<kodachi::DataAttribute, T>::value>::type>
    const T getValues() const {
        return T(mAttrGroup.getChildByName(mValueName));
    }

    bool
    isValid() const { return mIsValid; }

    // matching rdl attr rates
    enum SCOPE {
        UNKNOWN  = -1,
        CONSTANT = 0, // primitive
        UNIFORM  = 1, // face / per curve
        POINT    = 2, // per point / cv
        VERTEX   = 3  // per vertex / cv
    };

    SCOPE mScope;
    kodachi::AttributeType mValueType;
    kodachi::StringAttribute mInputType;
    int64_t mTupleSize;     // look at element size first,
                            // then look at value's tuple size
    std::string mValueName; // value or indexedValue
    bool        mIsIndexed;
    bool        mIsValid;   // user is responsible for checking the validity of
                            // the arbitrary attribute
private:
    kodachi::GroupAttribute mAttrGroup;

    // derive input type from attribute data type and element size
    void
    _setInputType(const kodachi::IntAttribute& elementSize)
    {
        static const kodachi::StringAttribute kIntAttr("int");
        static const kodachi::StringAttribute kFloatAttr("float");
        static const kodachi::StringAttribute kDoubleAttr("double");
        static const kodachi::StringAttribute kStringAttr("string");
        static const kodachi::StringAttribute kVector2Attr("vector2");
        static const kodachi::StringAttribute kVector3Attr("vector3");
        static const kodachi::StringAttribute kMatrix16Attr("matrix16");

        // if input type was invalid, first derive the basic data type
        if (!mInputType.isValid()) {
            switch (mValueType) {
            case kodachi::kAttrTypeInt:    mInputType = kIntAttr; break;
            case kodachi::kAttrTypeFloat:  mInputType = kFloatAttr; break;
            case kodachi::kAttrTypeDouble: mInputType = kDoubleAttr; break;
            case kodachi::kAttrTypeString: mInputType = kStringAttr; break;;
            }
        }

        // basic float data types may depend on element size as the
        // actual tuple size
        if (mInputType == kFloatAttr) {
            if (elementSize.isValid()) {
                mTupleSize = elementSize.getValue();
            }

            if (mTupleSize > 1) {
                switch (mTupleSize) {
                case 2 : mInputType = kVector2Attr; break;
                case 3 : mInputType = kVector3Attr; break;
                case 16: mInputType = kMatrix16Attr; break;
                }
            }
        }
    }

}; // struct ArbitraryAttr

} // namespace kodachi

