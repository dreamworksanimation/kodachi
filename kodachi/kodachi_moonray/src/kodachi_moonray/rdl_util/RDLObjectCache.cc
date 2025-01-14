// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi_moonray/rdl_util/RDLObjectCache.h>

// third-party includes
#include <json/reader.h>
#include <json/value.h>
#include <tbb/spin_mutex.h>

// system includes
#include <algorithm>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <mutex>

namespace
{

using namespace kodachi_moonray;

// Hidden parameters arent truly hidden.  They're just the parameters we want
// to skip over when conditioning katana data into moonray data.
const std::string kHiddenParamPrefix("__");


bool
endswith(const std::string& fullString, const std::string& search)
{
    return fullString.size() >= search.size() && 
        fullString.compare(fullString.size() - search.size(), search.size(),
            search) == 0;
}

int
getValueType(const std::string& type)
{   
    if (type == "Bool" || type == "BoolVector") {
        return kFnRendererObjectValueTypeBoolean;
    } else if (type == "Int" || type == "Long" || type == "IntVector" ||
            type == "LongVector") {
        return kFnRendererObjectValueTypeInt;
    } else if (type == "Float" || type == "Double" || type == "FloatVector" ||
            type == "DoubleVector") {
        return kFnRendererObjectValueTypeFloat;
    } else if (type == "String" || type == "StringVector") {
        return kFnRendererObjectValueTypeString;
    } else if (type == "Rgb" || type == "RgbVector") {
        return kFnRendererObjectValueTypeColor3;
    } else if (type == "Rgba" || type == "RgbaVector") {
        return kFnRendererObjectValueTypeColor4;
    } else if (type == "Vec2f" || type == "Vec2d" || type == "Vec2fVector" ||
            type == "Vec2dVector") {
        return kFnRendererObjectValueTypeVector2;
    } else if (type == "Vec3f" || type == "Vec3d" || type == "Vec3fVector" ||
            type == "Vec3dVector") {
        return kFnRendererObjectValueTypeVector3;
    } else if (type == "Vec4f" || type == "Vec4d" || type == "Vec4fVector" ||
            type == "Vec4dVector") {
        return kFnRendererObjectValueTypeVector4;
    } else if (type == "Mat4f" || type == "Mat4d" || type == "Mat4fVector" ||
            type == "Mat4dVector") {
        return kFnRendererObjectValueTypeMatrix;
    } else if (type == "SceneObject*" || type == "SceneObjectVector") {
        return kFnRendererObjectValueTypeLocation;
    }

    return kFnRendererObjectValueTypeUnknown;
}

void
getComponents(const Json::Value& value, std::vector<std::string>& components)
{
    for (unsigned int i = 0; i < value.size(); ++i) {
            components.push_back(value[i].asString());
    }
}

void
getComponents(const Json::Value& value, std::vector<int>& components)
{
    for (unsigned int i = 0; i < value.size(); ++i) {
        components.push_back(value[i].asInt());
    }
}

void
getComponents(const Json::Value& value, std::vector<float>& components)
{
    for (unsigned int i = 0; i < value.size(); ++i) {
        if (value[i].isArray()) {
            // Recursively call this function if we have an array of arrays.
            // This will essentially flatten the value down which is what
            // Katana expects.
            getComponents(value[i], components);
        } else {
            components.push_back(value[i].asFloat());
        }
    }
}

FnAttribute::Attribute
getDefaultVector(const Json::Value& value)
{
    // Not sure if this is possible ...
    if (value.size() == 0) {
        return FnAttribute::Attribute();
    }

    if (value[0].isIntegral()) {
        std::vector<int> items;
        getComponents(value, items);
        return FnAttribute::IntAttribute(items.data(), items.size(), 1);
    } else if (value[0].isString()) {
        std::vector<std::string> items;
        getComponents(value, items);
        return FnAttribute::StringAttribute(items);
    } else {
        std::vector<float> items;
        getComponents(value, items);
        return FnAttribute::FloatAttribute(items.data(), items.size(), 1);
    }
}

FnAttribute::Attribute
getDefaultValue(const Json::Value& value)
{
    // Check if we have a null value.
    if (value.isNull()) {
        return FnAttribute::Attribute();
    }

    if (value.isArray()) {
        return getDefaultVector(value);
    } else if (value.isIntegral() || value.isBool()) {
        return FnAttribute::IntAttribute(value.asInt());
    } else if (value.isDouble()) {
        return FnAttribute::FloatAttribute(value.asFloat());
    } else {
        return FnAttribute::StringAttribute(value.asString());
    }
}

std::map<std::string, std::string>
getGroupingInfo(const Json::Value& sceneClass)
{
    std::map<std::string, std::string> groups;

    Json::Value groupingValue = sceneClass.get("grouping", Json::Value());
    if (!groupingValue.isNull()) {
        Json::Value groupValue = groupingValue.get("groups", Json::Value());
        if (!groupValue.isNull()) {
            const std::vector<std::string>& groupNames = 
                groupValue.getMemberNames();
            for (const std::string& groupName : groupNames) {
                Json::Value attributes = groupValue[groupName];
                for (const Json::Value& attrName : attributes) {
                    groups[attrName.asString()] = groupName;
                }
            }
        }
    }

    return groups;
}

void
getParameter(const std::string& name, 
             const Json::Value& attribute,
             RenderObject& renderObject,
             const std::map<std::string, std::string>& groups)
{
    Json::Value type = attribute.get("attrType", Json::Value());
    if (!type) {
        std::cerr << "Unable to find key 'type' for attribute "
                  << name << ". Skipping.\n";
        return;
    }

    // Order
    Json::Value order = attribute.get("order", Json::Value());
    if (!order) {
        std::cerr << "Unable to find index value for attribute " << name
                  << std::endl;
        return;
    }

    Param param;
    param.mName = name;
    std::string typeStr = type.asString();
    param.mValueType = getValueType(typeStr);

    // Default Value
    if (param.mValueType == kFnRendererObjectValueTypeLocation) {
        // Default value for SceneObject attributes is currently read in as a bool
        // Ignore this and use empty string instead
        param.mDefaultValue = FnAttribute::StringAttribute("");
    } else {
        const Json::Value defaultValue = attribute.get("default", Json::Value());
        if (!defaultValue.isNull()) {
            param.mDefaultValue = getDefaultValue(defaultValue);
        }
    }

    // Group
    auto i = groups.find(name);
    if (i != groups.end()) {
        param.mGroup = i->second;
    }

    // BindType
    if (param.mValueType == kFnRendererObjectValueTypeLocation) {
        Json::Value interface = attribute.get("interface", Json::Value());
        if (!interface.isNull()) {
            std::string iface = interface.asString();
            std::transform(iface.begin(), iface.end(), iface.begin(), 
                ::tolower);

            if (iface == RDLObjectCache::sMap ||
                    iface == RDLObjectCache::sDisplacement || 
                    iface == RDLObjectCache::sLight ||
                    iface == RDLObjectCache::sLightFilter ||
                    iface == RDLObjectCache::sVolume ||
                    iface == RDLObjectCache::sDwaBaseLayerable ||
                    iface == RDLObjectCache::sMaterial ||
                    iface == RDLObjectCache::sRootShader) {
                param.mBindType = iface;
                param.mValueType = kFnRendererObjectValueTypeShader;
                param.mWidget = "null";
            }
        }

        // either no interface was specified or it is of a non-material type
        // (camera, geometry, node, etc.) Use scenegraphLocation widget
        if (param.mWidget.empty()) {
            param.mWidget = "scenegraphLocation";
        }
    }

    // If we don't have a specific interface, but we are listed as 'bindable',
    // then this attribute can be bound to a map.
    if (param.mBindType.empty()) {
        const Json::Value bindable = attribute.get("bindable", Json::Value());
        if (!bindable.isNull() && bindable.asBool()) {
            param.mBindType = RDLObjectCache::sMap;
        }
    }

    // Enumerations
    Json::Value enumAttr = attribute.get("enum", Json::Value());
    if (!enumAttr.isNull()) {
        const std::vector<std::string>& enumNames = enumAttr.getMemberNames();
        param.mOptions.clear();
        param.mOptions.reserve(enumNames.size());

        const FnAttribute::IntAttribute::value_type defaultIdx =
                FnAttribute::IntAttribute(param.mDefaultValue).getValue();

        for (const std::string& name : enumNames) {
            if (enumAttr.get(name, -1) == defaultIdx) {
                param.mDefaultValue = FnAttribute::StringAttribute(name);
            }

            param.mOptions.push_back(name);
        }

        param.mWidget = "popup";
        param.mValueType = kFnRendererObjectValueTypeString;
    }

    // Widget
    Json::Value filename = attribute.get("filename", Json::Value());
    if (!filename.isNull() && filename.asBool()) {
        param.mWidget = "assetIdInput";
    } else if (endswith(typeStr, "Vector") || endswith(typeStr, "Indexable")) {
        // This works acceptably for StringVector. Other types untested.
        // Requres extra hints to be set by MoonrayRenderInfo.cc
        // SceneObjectVector may want to use scenegraphLocationArray but that is broken in
        // Katana currently.
        param.mWidget = "sortableArray";
        param.mDefaultValue = FnAttribute::StringAttribute("");
    }

    // Metadata
    Json::Value metadata = attribute.get("metadata", Json::Value());
    if (!metadata.isNull()) {
        Json::Value structure_type = 
            metadata.get("structure_type", Json::Value());

        if (!structure_type.isNull() && 
                structure_type.asString() == "ramp_color") {
            // Katana is very strict about the structure and naming of ramp 
            // widget parameters.
            Json::Value structure_path = metadata.get("structure_path", 
                Json::Value());
            Json::Value structure_name = metadata.get("structure_name",
                Json::Value());
            if (!structure_path.isNull() && !structure_name.isNull()) {
                std::string prefix = structure_name.asString();
                std::string path = structure_path.asString();

                // Get and set the defaults
                const Json::Value defaultValue =
                        attribute.get("default", Json::Value());
                if (!defaultValue.isNull()) {
                    param.mDefaultValue = getDefaultValue(defaultValue);
                }

                if (path == "positions") {
                    param.mName = prefix + "_Knots";
                    param.mWidget = "null";

                } else if (path == "values") {
                    param.mName = prefix + "_Colors";
                    param.mWidget = "null";

                } else if (path == "interpolation_types") {
                    // Since katana doesnt yet support per-knot interpolations,
                    // we'll let the moonray interpolations attribute display
                    // normally.
                }
            }
        }

        // Help
        Json::Value help = metadata.get("comment", Json::Value());
        if (!help.isNull()) {
            param.mHelp = help.asString();
        }

        // Label
        Json::Value label = metadata.get("label", Json::Value());
        if (!label.isNull()) {
            param.mWidgetDisplayName = label.asString();
        }
    }

    // Aliases
    Json::Value aliases = attribute.get("aliases", Json::Value());
    if (!aliases.isNull()) {
        param.mAliases.reserve(aliases.size());
        for (auto alias : aliases) {
            auto aliasStr = alias.asString();
            param.mAliases.push_back(aliasStr);

            // Create any whitespace/underscore combos
            if (aliasStr.find(' ')) {
                std::replace(aliasStr.begin(), aliasStr.end(), ' ', '_');
                if (std::find(param.mAliases.begin(),
                              param.mAliases.end(),
                              aliasStr) == param.mAliases.end()) {
                    param.mAliases.push_back(aliasStr);
                }
            }
        }
    }

    renderObject.mParams[order.asInt()] = std::move(param);
}


void
checkForRampAttributes(RenderObject& renderObject)
{
    auto i = renderObject.mParams.begin();
    while (i != renderObject.mParams.end()) {
        // Check for 'null' widgets on non-shader types. This is indicative of a ramp.
        if (i->mValueType != kFnRendererObjectValueTypeShader &&
                i->mWidget == "null") {
            // Ramps are composed of three separate attributes in Moonray, and 
            // these also correspond to the expected args in Katana. However, 
            // Katana also requires an initial parameter to start the ramp off.
            // That initial parameter is the ramp name with an int value
            // corresponding to the number of knots in the ramp
            // TODO: Moonray doesn't currently have any float ramps, but it
            // may in the future.

            // Make it static and initialize the ramp starter once.  It will
            // be initialized on the first encounter of either ramp_Colors,
            // ramp_Interpolation, or ramp_Knots
            static Param rampStarter;
            static Param rampInterpolation;
            if (rampStarter.mWidget.empty()) {
                rampStarter.mWidget = "colorRamp";
                rampStarter.mGroup = i->mGroup;
                rampStarter.mValueType = kFnRendererObjectValueTypeColor3;
                rampStarter.mDefaultValue = FnAttribute::IntAttribute(1);

                // The naming convention for ramp attributes is <ramp_name>,
                // <ramp_name>_Knots, <ramp_name>_Colors, <ramp_name>_Interpolation.
                // We will use the name of the current parameter to figure out
                // that ramp name.
                size_t pos = i->mName.rfind('_');
                const std::string rampName = i->mName.substr(0, pos);
                rampStarter.mName = rampName;

                i = renderObject.mParams.insert(i, std::move(rampStarter));
                ++i;

                // Also initialize the RampWidget's global interpolation.
                // This is the last step to making sure the RampWidget works.
                rampInterpolation.mName = rampName + "_Interpolation";
                rampInterpolation.mValueType = kFnRendererObjectValueTypeString;
                rampInterpolation.mOptions = std::vector<std::string>{"linear"};
                rampInterpolation.mDefaultValue =
                    FnAttribute::StringAttribute("linear");
                rampInterpolation.mWidget = "null";

                i = renderObject.mParams.insert(i, std::move(rampInterpolation));
                ++i;
            }

            // Find the first non-ramp entry and restart our loop.
            while (i != renderObject.mParams.end() && i->mWidget == "null") {
                ++i;
            }
        } else {
            ++i;
        }
    }
}

RenderObject*
getRenderObject(const Json::Value& root, std::string& katanaObjectType)
{
    // Get object type
    Json::Value type = root.get("type", Json::Value());
    if (type.isNull()) {
        std::cerr << "No 'type' key for object." << std::endl;
        return nullptr;
    }

    // Translate to the Katana type
    std::string typeStr = type.asString();
    std::transform(typeStr.begin(), typeStr.end(), typeStr.begin(), ::tolower);
    if (typeStr == RDLObjectCache::sMaterial ||
            typeStr == RDLObjectCache::sDwaBaseLayerable ||
            typeStr == RDLObjectCache::sVolume ||
            typeStr == RDLObjectCache::sDisplacement || 
            typeStr == RDLObjectCache::sLight ||
            typeStr == RDLObjectCache::sLightFilter ||
            typeStr == RDLObjectCache::sMap) {
        katanaObjectType = kFnRendererObjectTypeShader;
    } else if (typeStr == RDLObjectCache::sRenderOutput) {
        katanaObjectType = kFnRendererObjectTypeOutputChannel;
    } else {
        // This isn't an object we cache
        return nullptr;
    }

    std::unique_ptr<RenderObject> renderObject(new RenderObject(std::move(typeStr)));

    // Get the list of parameters
    Json::Value attributes = root.get("attributes", Json::Value());
    
    const std::vector<std::string>& attrNames = attributes.getMemberNames();

    // Set the size of our parameters
    renderObject->mParams.resize(attrNames.size());

    // Get the grouping information. This may not exist if there are no
    // groups in the render object.
    std::map<std::string, std::string> groups = getGroupingInfo(root);

    for (const std::string& attrName : attrNames) {
        Json::Value attribute = attributes.get(attrName, Json::Value());
        getParameter(attrName, attribute, *renderObject, groups);
    }

    // Do a quick check to see if any of the parameters correspond to a
    // ramp. If so, we need to add one extra parameter to make our structure
    // match the one expected by Katana. We do this last so we don't mess
    // up ordering.
    checkForRampAttributes(*renderObject);

    return renderObject.release();
}

void parseJson(const std::string& jsonData, 
    std::map<std::string, RDLObjectCache::RDLObjectMap>& renderObjects)
{
    // Parse the file
    Json::Value root;
    Json::Reader reader;
    if (!reader.parse(jsonData, root)) {
        std::cerr << "Invalid json data.";
        return;
    }

    // Find the 'scene_classes' entry. This should be an array of all
    // scene classes contained in this json file.
    Json::Value sceneClasses = root.get("scene_classes", Json::Value());
    if (sceneClasses.isNull()) {
        std::cerr << "Error while reading jsonData: No scene "
                  << "classes found." << std::endl;
        return;
    }

    const std::vector<std::string>& classNames = sceneClasses.getMemberNames();
    for (const std::string& name : classNames) {
        Json::Value sceneClass = sceneClasses[name];
        std::string katanaType;
        RenderObject* renderObject = getRenderObject(sceneClass, katanaType);

        if (renderObject) {
            renderObjects[katanaType][name].reset(renderObject);
        }
    }
}

// Our cache singleton and the mutexes protecting it. We use a combination
// of a spin_mutex and a standard one so, when the cache is valid (the most
// common case), access time are fast.
tbb::spin_mutex sCacheMutex;
std::mutex sCacheLoadMutex;
std::shared_ptr<RDLObjectCache> sCache;
}

namespace kodachi_moonray {

// Our object types
const std::string RDLObjectCache::sDisplacement = "displacement";
const std::string RDLObjectCache::sDwaBaseLayerable = "dwabaselayerable";
const std::string RDLObjectCache::sLight = "light";
const std::string RDLObjectCache::sLightFilter = "lightfilter";
const std::string RDLObjectCache::sMap = "map";
const std::string RDLObjectCache::sMaterial = "material";
const std::string RDLObjectCache::sRenderOutput = "renderoutput";
const std::string RDLObjectCache::sRootShader = "rootshader";
const std::string RDLObjectCache::sVolume = "volume";

std::shared_ptr<RDLObjectCache>
RDLObjectCache::get()
{
    // We use a spin mutex to protect the shared_ptr since there isn't an
    // atomic shared_ptr yet. This protects against the case where flush and
    // get are called concurrently.
    {
        tbb::spin_mutex::scoped_lock spinLock(sCacheMutex);
        if (sCache) return sCache;
    }

    // We don't have a cache yet so we'll have to build one. This is a
    // potentially slow process, so we don't want to use a spin mutex here.
    std::lock_guard<std::mutex> loadLock(sCacheLoadMutex);

    // Check that another thread didn't already cache the shaders.
    if (sCache) return sCache;

    sCache.reset(new RDLObjectCache);
    return sCache;
}

void
RDLObjectCache::flush()
{
    // Get the load lock so we don't reset the cache while it's being created.
    std::lock_guard<std::mutex> loadLock(sCacheLoadMutex);

    // Get the spin lock to block against calls to get.
    tbb::spin_mutex::scoped_lock spinLock(sCacheMutex);

    // Throw away the old cache.
    sCache.reset();
}

RDLObjectCache::RDLObjectCache()
{
    FILE* process = ::popen("rdl2_json_exporter", "r");
    if (process == nullptr) {
        std::cerr << "Unable to run command 'rdl2_json_exporter'." << std::endl;
        return;
    }
    
    // Get stdout from the process
    std::array<char, 128> buffer;
    std::string jsonData;
    while (!feof(process)) {
        if (fgets(buffer.data(), 128, process) != nullptr) {
            jsonData += buffer.data();
        }
    }

    ::pclose(process);

    parseJson(jsonData, mRenderObjects);

    const std::string filePath = getenv("KODACHI_RDL_PATH");
    if (!filePath.empty()) {

        // Sanitize filePath
        // Normalize the path by removing any redundant elements
        std::filesystem::path normalizedPath = std::filesystem::path(filePath).lexically_normal();

        // Ensure the path is absolute
        if (!std::filesystem::path(normalizedPath).is_absolute()) {
            std::cerr << "KODACH_RDL_PATH must be absolute.\n";
            return;
        }

        // Check for path traversal sequences
        if (normalizedPath.string().find("..") != std::string::npos) {
            std::cerr << "KODACH_RDL_PATH must be absolute.\n";
            return;
        }

        FILE* katanaRdlFile = fopen((filePath + "/scene_classes.json").c_str(), "r");
        if (katanaRdlFile) {
            std::string katanaRdlJson;
            fseek(katanaRdlFile, 0, SEEK_END);
            katanaRdlJson.resize(ftell(katanaRdlFile));
            rewind(katanaRdlFile);
            fread(&katanaRdlJson[0], 1, katanaRdlJson.size(), katanaRdlFile);
            fclose(katanaRdlFile);

            parseJson(katanaRdlJson, mRenderObjects);
        }
    }
}

const RDLObjectCache::RDLObjectMap&
RDLObjectCache::getRenderObjects(const std::string& katanaType) const
{
    // Empty map to return when no objects match the given type.
    static RDLObjectMap sEmpty;

    auto i = mRenderObjects.find(katanaType);
    if (i == mRenderObjects.end()) {
        return sEmpty;
    }

    return i->second;
}

RDLObjectCache::ObjectType
RDLObjectCache::typeNameToTypeEnum(const std::string& typestr)
{
    if (typestr == RDLObjectCache::sDisplacement) {
        return ObjectType::DISPLACEMENT | ObjectType::ROOTSHADER;
    }
    else if (typestr == RDLObjectCache::sLight) {
        return ObjectType::LIGHT;
    }
    else if (typestr == RDLObjectCache::sLightFilter) {
        return ObjectType::LIGHTFILTER;
    }
    else if (typestr == RDLObjectCache::sMap) {
        return ObjectType::MAP;
    }
    else if (typestr == RDLObjectCache::sRenderOutput) {
        return ObjectType::RENDER_OUTPUT;
    }
    else if (typestr == RDLObjectCache::sRootShader) {
        return ObjectType::ROOTSHADER;
    }
    else if (typestr == RDLObjectCache::sVolume) {
        return (ObjectType::VOLUME | ObjectType::ROOTSHADER);
    }
    else if (typestr == RDLObjectCache::sMaterial) {
        return (ObjectType::MATERIAL | ObjectType::ROOTSHADER);
    }
    else if (typestr == RDLObjectCache::sDwaBaseLayerable) {
        return (ObjectType::DWA_BASE_LAYERABLE | ObjectType::MATERIAL | ObjectType::ROOTSHADER);
    }

    return ObjectType::UNKNOWN;
}

const std::string&
RDLObjectCache::typeEnumToTypeName(ObjectType type)
{
    if (type == ObjectType::DISPLACEMENT) {
        return RDLObjectCache::sDisplacement;
    }
    else if (type == ObjectType::LIGHT) {
        return RDLObjectCache::sLight;
    }
    else if (type == ObjectType::LIGHTFILTER) {
        return RDLObjectCache::sLightFilter;
    }
    else if (type == ObjectType::MAP) {
        return RDLObjectCache::sMap;
    }
    else if (type == ObjectType::RENDER_OUTPUT) {
        return RDLObjectCache::sRenderOutput;
    }
    else if (type == ObjectType::ROOTSHADER) {
        return RDLObjectCache::sRootShader;
    }
    else if (type == ObjectType::VOLUME) {
        return RDLObjectCache::sVolume;
    }
    else if (type == ObjectType::MATERIAL) {
        return RDLObjectCache::sMaterial;
    }
    else if (type == ObjectType::DWA_BASE_LAYERABLE) {
        return RDLObjectCache::sDwaBaseLayerable;
    }

    static const std::string sEmpty;
    return sEmpty;
}

} // namespace kodachi_moonray

