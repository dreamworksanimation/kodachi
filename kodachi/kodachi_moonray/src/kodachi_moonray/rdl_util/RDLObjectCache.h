// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

// katana includes
#include <FnAttribute/FnAttribute.h>
#include <FnRendererInfo/suite/RendererObjectDefinitions.h>

// system includes
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace kodachi_moonray {

class RDLObjectCache;
class RenderObject;
class Param;

// Cache for RDL2 Render Objects.
class RDLObjectCache
{
public:
    // Method for retrieving, and creating if necessary, the cache of RDL2
    // render objects in our path.
    static std::shared_ptr<RDLObjectCache> get();

    // Deletes the current cache instance
    static void flush();

    typedef std::map<std::string, std::unique_ptr<RenderObject> > RDLObjectMap;

    // Returns all of the render objects of a given type. Type here is
    // defined as one of the kFnRendererObjectValueType's defined by Katana.
    const RDLObjectMap&
    getRenderObjects(const std::string& katanaType) const;

    // object types
    static const std::string sDisplacement;
    static const std::string sDwaBaseLayerable;
    static const std::string sLight;
    static const std::string sLightFilter;
    static const std::string sMap;
    static const std::string sMaterial;
    static const std::string sRenderOutput;
    static const std::string sRootShader;
    static const std::string sVolume;

    enum class ObjectType : std::uint32_t
    {
        UNKNOWN            = 0u,
        DISPLACEMENT       = 1u << 0,
        LIGHT              = 1u << 1,
        MAP                = 1u << 2,
        RENDER_OUTPUT      = 1u << 3,
        ROOTSHADER           = 1u << 4,
        VOLUME             = 1u << 5,
        MATERIAL           = 1u << 6,
        DWA_BASE_LAYERABLE = 1u << 7,
        LIGHTFILTER        = 1u << 8,
    };

    static ObjectType typeNameToTypeEnum(const std::string& typestr);
    static const std::string& typeEnumToTypeName(ObjectType type);

private:
    RDLObjectCache();

    std::map<std::string, RDLObjectMap> mRenderObjects;
};

inline RDLObjectCache::ObjectType
operator&(RDLObjectCache::ObjectType l, RDLObjectCache::ObjectType r)
{
    return static_cast<RDLObjectCache::ObjectType>(static_cast<std::uint32_t>(l) & static_cast<std::uint32_t>(r));
}

inline RDLObjectCache::ObjectType
operator|(RDLObjectCache::ObjectType l, RDLObjectCache::ObjectType r)
{
    return static_cast<RDLObjectCache::ObjectType>(static_cast<std::uint32_t>(l) | static_cast<std::uint32_t>(r));
}

// Class representing a shader, render output, etc.
class RenderObject
{
public:
    RenderObject() = delete;

    explicit
    RenderObject(const std::string& type)
        : mTypeName(type)
        , mType(RDLObjectCache::typeNameToTypeEnum(mTypeName))
    {
    }

    explicit
    RenderObject(std::string&& type)
        : mTypeName(type)
        , mType(RDLObjectCache::typeNameToTypeEnum(mTypeName))
    {
    }

    bool isA(RDLObjectCache::ObjectType type) const
    {
        return (mType & type) == type;
    }

    bool isA(const std::string& type) const
    {
        return isA(RDLObjectCache::typeNameToTypeEnum(type));
    }

    // appends the list of tags for the specified object type to the
    // provided string vector
    void fillShaderOutputTags(std::vector<std::string>& tags) const
    {
        tags.push_back(mTypeName);

        if (isA(RDLObjectCache::ObjectType::ROOTSHADER)) {
            tags.emplace_back(RDLObjectCache::sRootShader);
        }

        if (isA(RDLObjectCache::ObjectType::DWA_BASE_LAYERABLE)) {
            tags.push_back(RDLObjectCache::sMaterial);
        }
    }

    // The declared RDL2 interface for the object. This is more specific
    // than the Katana type.
    const std::string mTypeName;
    const RDLObjectCache::ObjectType mType;

    // The attributes on the object.
    std::vector<Param> mParams;
};

// Structure holding the data for a single parameter on a RenderObject.
struct Param
{
    // The name of the attribute
    std::string mName;

    // The display name of the attribute
    std::string mWidgetDisplayName;

    // If this value is non-empty, indicates the parameter is bindable and
    // the object type it can be binded to.
    std::string mBindType;

    // The default value for the parameter.
    FnAttribute::Attribute mDefaultValue;

    // The group name for the attribute if it is grouped. This is the
    // equivalent of "page" in Katana.
    std::string mGroup;

    // Help for the parameter. This is displayed if the user clicks the '?'
    // icon next to the parameter.
    std::string mHelp;

    // The type of widget this parameter should use.
    std::string mWidget;

    std::vector<std::string> mOptions;

    // The value type (int, string, float, etc).
    int mValueType = kFnRendererObjectValueTypeUnknown;

    std::vector<std::string> mAliases;
};

} // namespace kodachi_moonray

