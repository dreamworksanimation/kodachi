// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>

// scene_rdl2
#include <scene_rdl2/scene/rdl2/Types.h>

namespace kodachi_moonray {
namespace util {

enum class TruncateBehavior
{
    IGNORE, // Truncate silently
    WARN, // Print warning log statement
    THROW // throw exception
};

class InvalidAttributeError : public std::runtime_error
{
public:
    explicit InvalidAttributeError(const std::string& arg) : std::runtime_error(arg) {}
    static InvalidAttributeError fromAttr(const kodachi::Attribute& attr);
};

class AttributeTypeError : public std::runtime_error
{
public:
    explicit AttributeTypeError(const std::string& arg) : std::runtime_error(arg) {}

    template <class rdl2_type>
    static AttributeTypeError
    invalidConversion(const kodachi::Attribute &attr)
    {
        std::stringstream ss;
        ss << "Cannot convert Attribute of type '";
        kodachi::GetAttrTypeAsPrettyText(ss, attr);
        ss << "' to arras::rdl2::" << arras::rdl2::attributeTypeName<rdl2_type>();

        return  AttributeTypeError(ss.str());
    }
};

class AttributeDataError: public std::runtime_error
{
public:
    explicit AttributeDataError(const std::string& arg) : std::runtime_error(arg) {}
};

class TruncationError : public std::runtime_error
{
public:
    explicit TruncationError(const std::string& arg) : std::runtime_error(arg) {}
};

/**
 * Convert a kodachi Attribute to an rdl2 Type.
 * @param attr The attribute to be converted
 * @param time The time to sample the Attribute at (does not interpolate)
 * @param behavior
 */
template <class T>
T rdl2Convert(const kodachi::DataAttribute& attr,
              const float time = 0.f,
              const TruncateBehavior behavior = TruncateBehavior::WARN);

} // namespace util
} // namespace kodachi_moonray

