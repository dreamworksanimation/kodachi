// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <unordered_map>

namespace {

KdLogSetup("KPOPPrimitiveAttributes");

// Types supported by rdl2::UserData class
enum class UserDataType
{
    BOOL,
    INT,
    FLOAT,
    STRING,
    RGB,
    VEC2F,
    VEC3F,
    MAT4F,
    UNSUPPORTED
};

// The expected tuple size for UserData types
int64_t
getTupleSize(UserDataType udt)
{
    switch (udt) {
    case UserDataType::BOOL:
    case UserDataType::INT:
    case UserDataType::FLOAT:
    case UserDataType::STRING:
        return 1;
    case UserDataType::VEC2F:
        return 2;
    case UserDataType::RGB:
    case UserDataType::VEC3F:
        return 3;
    case UserDataType::MAT4F:
        return 16;
    }

    return 0;
}

/**
 * rdl2::UserData can technically store 1 of every primitive attribute type,
 * but we create a new UserData for each attribute. A primitive attribute is
 * stored as a name/vector pair. These helpers return the AttributeNames for
 * setting the specified UserDataType.
 */
const char*
getKeyAttrName(UserDataType udt)
{
    switch (udt) {
    case UserDataType::BOOL:   return "bool_key";
    case UserDataType::INT:    return "int_key";
    case UserDataType::FLOAT:  return "float_key";
    case UserDataType::STRING: return "string_key";
    case UserDataType::RGB:    return "color_key";
    case UserDataType::VEC2F:  return "vec2f_key";
    case UserDataType::VEC3F:  return "vec3f_key";
    case UserDataType::MAT4F:  return "mat4f_key";
    }

    return "";
}

kodachi::StringAttribute
getUserDataTypeName(UserDataType udt)
{
    static const kodachi::StringAttribute kBoolAttr("bool");
    static const kodachi::StringAttribute kIntAttr("int");
    static const kodachi::StringAttribute kStringAttr("string");
    static const kodachi::StringAttribute kFloatAttr("float");
    static const kodachi::StringAttribute kColorAttr("color");
    static const kodachi::StringAttribute kVec2fAttr("vec2f");
    static const kodachi::StringAttribute kVec3fAttr("vec3f");
    static const kodachi::StringAttribute kMat4fAttr("mat4f");

    switch (udt) {
    case UserDataType::BOOL:   return kBoolAttr;
    case UserDataType::INT:    return kIntAttr;
    case UserDataType::FLOAT:  return kFloatAttr;
    case UserDataType::STRING: return kStringAttr;
    case UserDataType::RGB:    return kColorAttr;
    case UserDataType::VEC2F:  return kVec2fAttr;
    case UserDataType::VEC3F:  return kVec3fAttr;
    case UserDataType::MAT4F:  return kMat4fAttr;
    }

    return "";
}

const char*
getValuesAttrName(UserDataType udt)
{
    switch (udt) {
    case UserDataType::BOOL:   return "bool_values";
    case UserDataType::INT:    return "int_values";
    case UserDataType::FLOAT:  return "float_values";
    case UserDataType::STRING: return "string_values";
    case UserDataType::RGB:    return "color_values";
    case UserDataType::VEC2F:  return "vec2f_values";
    case UserDataType::VEC3F:  return "vec3f_values";
    case UserDataType::MAT4F:  return "mat4f_values";
    }

    return "";
}

// Maps arbitrary output type to UserData type
UserDataType
getUserDataType(const kodachi::StringAttribute& dataTypeAttr)
{
    static const std::unordered_map<kodachi::StringAttribute, UserDataType,
                                    kodachi::AttributeHash> kUserDataMap
    {
        { "float"    , UserDataType::FLOAT },
        { "double"   , UserDataType::FLOAT },
        { "int"      , UserDataType::INT },
        { "long"     , UserDataType::INT },
        { "string"   , UserDataType::STRING },
        { "color3"   , UserDataType::RGB },
        { "color4"   , UserDataType::UNSUPPORTED },
        { "normal2"  , UserDataType::VEC2F },
        { "normal3"  , UserDataType::VEC3F },
        { "vector2"  , UserDataType::VEC2F },
        { "vector3"  , UserDataType::VEC3F },
        { "vector4"  , UserDataType::UNSUPPORTED },
        { "point2"   , UserDataType::VEC2F },
        { "point3"   , UserDataType::VEC3F },
        { "point4"   , UserDataType::UNSUPPORTED },
        { "matrix9"  , UserDataType::UNSUPPORTED },
        { "matrix16" , UserDataType::MAT4F },
        { "uint"     , UserDataType::INT },
        { "unsigned" , UserDataType::INT },
        { "ulong"    , UserDataType::INT },
        { "bool"     , UserDataType::BOOL },
        { "byte"     , UserDataType::UNSUPPORTED },
    };

    const auto iter = kUserDataMap.find(dataTypeAttr);

    if (iter != kUserDataMap.end()) {
        return iter->second;
    }

    return UserDataType::UNSUPPORTED;
}

// Builds the AttributeSet attrs for creating a rdl2 child location that
// represents a UserData instance
kodachi::GroupAttribute
buildUserDataAttrs(const std::string& userDataPath, const kodachi::GroupAttribute& userDataAttrs)
{
    static const kodachi::StringAttribute kAttributeSetCELAttr("//*");
    static const kodachi::StringAttribute kRdl2Attr("rdl2");
    static const kodachi::StringAttribute kUserDataAttr("UserData");

    static const std::string kType("type");
    static const std::string kSceneObjectSceneClass("rdl2.sceneObject.sceneClass");
    static const std::string kSceneObjectName("rdl2.sceneObject.name");
    static const std::string kSceneObjectAttrs("rdl2.sceneObject.attrs");
    static const std::string kSceneObjectDisableAliasing("rdl2.sceneObject.disableAliasing");

    kodachi::op_args_builder::AttributeSetOpArgsBuilder asBuilder;
    asBuilder.setCEL(kAttributeSetCELAttr);
    asBuilder.setAttr(kType, kRdl2Attr);
    asBuilder.setAttr(kSceneObjectSceneClass, kUserDataAttr);
    asBuilder.setAttr(kSceneObjectName, kodachi::StringAttribute(userDataPath));
    asBuilder.setAttr(kSceneObjectAttrs, userDataAttrs);
    asBuilder.setAttr(kSceneObjectDisableAliasing, kodachi::IntAttribute(true));

    return asBuilder.build();
}

class KPOPPrimitiveAttributes: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            const kodachi::IntAttribute primAttrCachingAttr =
                    interface.getAttr("moonrayGlobalStatements.primitiveAttributeCaching");

            if (primAttrCachingAttr.isValid()) {
                kodachi::GroupBuilder opArgsGb;
                opArgsGb.update(interface.getOpArg(""));
                opArgsGb.set("isCachingEnabled", primAttrCachingAttr);
                interface.replaceChildTraversalOp("", opArgsGb.build());
            }

            return;
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isGeometry\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::GroupAttribute arbitraryAttrs = interface.getAttr("geometry.arbitrary");

        if (!arbitraryAttrs.isValid() || arbitraryAttrs.getNumberOfChildren() == 0) {
            return;
        }

        const bool isKodachiGeometry = interface.getAttr("rdl2.sceneObject.kodachiGeometry").isValid();

        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
        const float shutterClose = kodachi::FloatAttribute(
                    interface.getAttr("rdl2.meta.shutterClose")).getValue();

        const std::string inputLocationPath = interface.getInputLocationPath();


        static const kodachi::StringAttribute kPrimitiveScopeAttr("primitive");

        // pair of <UserData name, isPrimitiveScope>
        std::vector<std::pair<std::string, bool>> primAttrPaths;
        kodachi::GroupBuilder kodachiGeometryArbAttrsGb;

        for (const auto entry : arbitraryAttrs) {
            // TODO: Use the ArbitraryAttribute helper class
            const kodachi::GroupAttribute arbAttr(entry.attribute);

            const kodachi::IntAttribute indexAttr = arbAttr.getChildByName("index");
            const bool isIndexedValue = indexAttr.isValid();

            kodachi::DataAttribute valueAttr;
            if (isIndexedValue) {
                valueAttr = arbAttr.getChildByName("indexedValue");
            } else {
                valueAttr = arbAttr.getChildByName("value");
            }

            if (!valueAttr.isValid()) {
                KdLogWarn("Arbitrary attribute '" << entry.name << "' has no value");
                continue;
            }

            UserDataType userDataType = UserDataType::UNSUPPORTED;
            {
                static const kodachi::StringAttribute kIntAttr("int");
                static const kodachi::StringAttribute kFloatAttr("float");
                static const kodachi::StringAttribute kDoubleAttr("double");
                static const kodachi::StringAttribute kStringAttr("string");
                static const kodachi::StringAttribute kVector2Attr("vector2");
                static const kodachi::StringAttribute kVector3Attr("vector3");
                static const kodachi::StringAttribute kMatrix16Attr("matrix16");

                kodachi::StringAttribute inputTypeAttr =
                        arbAttr.getChildByName("inputType");

                if (!inputTypeAttr.isValid()) {
                    switch (valueAttr.getType()) {
                    case kodachi::kAttrTypeInt:    inputTypeAttr = kIntAttr; break;
                    case kodachi::kAttrTypeFloat:  inputTypeAttr = kFloatAttr; break;
                    case kodachi::kAttrTypeDouble: inputTypeAttr = kDoubleAttr; break;
                    case kodachi::kAttrTypeString: inputTypeAttr = kStringAttr; break;
                    default:
                        KdLogWarn("Arbitrary attribute '" << entry.name << "' missing 'inputType'");
                        continue;
                    }
                }

                if (inputTypeAttr == kFloatAttr) {
                    int elementSize = valueAttr.getTupleSize();
                    {
                        const kodachi::IntAttribute elementSizeAttr =
                                arbAttr.getChildByName("elementSize");

                        if (elementSizeAttr.isValid()) {
                            elementSize = elementSizeAttr.getValue();
                        }
                    }

                    if (elementSize > 1) {
                        switch (elementSize) {
                        case 2: inputTypeAttr = kVector2Attr; break;
                        case 3: inputTypeAttr = kVector3Attr; break;
                        case 16: inputTypeAttr = kMatrix16Attr; break;
                        default:
                            KdLogWarn("Unsupported elementSize for attribute '"
                                      << entry.name << "'");
                            break;
                        }
                    }
                }

                userDataType = getUserDataType(inputTypeAttr);
            }

            if (userDataType == UserDataType::UNSUPPORTED) {
                KdLogWarn("Arbitrary attribute '" << entry.name
                          << "' unsupported 'inputType' type");
                continue;
            }

            const int64_t tupleSize = getTupleSize(userDataType);

            if (isKodachiGeometry) {
                // handle interpolation in the geometry procedural to avoid
                // interpolating and unpacking identical arbitrary attribute
                // multiple times
                kodachi::GroupBuilder arbAttrGb;
                arbAttrGb
                    .setGroupInherit(false)
                    .set("type", getUserDataTypeName(userDataType))
                    .set("scope", arbAttr.getChildByName("scope"))
                    .set("interpolationType", arbAttr.getChildByName("interpolationType"));

                if (isIndexedValue) {
                    arbAttrGb.set("index", indexAttr).set("indexedValue", valueAttr);
                } else {
                    arbAttrGb.set("value", valueAttr);
                }

                kodachiGeometryArbAttrsGb.set(entry.name, arbAttrGb.build());
            } else {
                const kodachi::IntAttribute cachingEnabledAttr =
                        interface.getOpArg("isCachingEnabled");

                const bool isCachingEnabled = cachingEnabledAttr.getValue(true, false);

                // RdlGeometry uses the UserData SceneObject, which only supports
                // a single time sample
                if (isIndexedValue) {
                    valueAttr = kodachi::interpolateAttr(valueAttr, 0, tupleSize);
                    valueAttr = kodachi::unpackIndexedValue(indexAttr, valueAttr);
                } else {
                    valueAttr = kodachi::interpolateAttr(valueAttr, 0, tupleSize);
                }

                const kodachi::GroupAttribute userDataAttrs = kodachi::GroupAttribute(
                        getKeyAttrName(userDataType), kodachi::StringAttribute(entry.name.data()),
                        getValuesAttrName(userDataType), valueAttr,
                        false);

                const std::string childName = kodachi::concat("__", entry.name);
                std::string userDataPath;
                if (isCachingEnabled) {
                    userDataPath = userDataAttrs.getHash().str() + "__UserData";
                } else {
                    userDataPath = kodachi::concat(inputLocationPath, "/", childName);
                }

                const kodachi::GroupAttribute attributeSetAttrs =
                        buildUserDataAttrs(userDataPath, userDataAttrs);

                const kodachi::StringAttribute scopeAttr = arbAttr.getChildByName("scope");

                KdLogDebug("Creating primitive attribute child: " << childName);
                interface.createChild(childName, "AttributeSet", attributeSetAttrs);

                // Keep track of if the attr is primitive scope for
                // auto-instancing purposes
                primAttrPaths.emplace_back(std::move(userDataPath), (scopeAttr == kPrimitiveScopeAttr));
            }
        }

        const kodachi::IntAttribute autoInstancingEnabledAttr =
                interface.getAttr("rdl2.meta.autoInstancing.enabled");

        const bool autoInstancingEnabled = autoInstancingEnabledAttr.isValid();

        if (!primAttrPaths.empty()) {
            kodachi::StringVector allPrimAttrPaths;
            {
                if (autoInstancingEnabled) {
                    for (const auto& entry : primAttrPaths) {
                        allPrimAttrPaths.emplace_back(entry.first);
                    }
                } else {
                    for (auto& entry : primAttrPaths) {
                        allPrimAttrPaths.emplace_back(std::move(entry.first));
                    }
                }
            }
            // RdlGeometry
            interface.setAttr("rdl2.sceneObject.attrs.primitive_attributes",
                    kodachi::ZeroCopyStringAttribute::create(std::move(allPrimAttrPaths)), false);

            // auto instancing
            if (autoInstancingEnabled) {
                // Instances can have their own primitive scope attrs
                // only use the non-primitive scope attrs for generating the ID
                // hash. Keep track of the others for auto-instancing.

                kodachi::StringVector primitiveScopeAttrs;
                kodachi::StringVector otherScopeAttrs;

                for (auto& entry : primAttrPaths) {
                    if (entry.second) {
                        primitiveScopeAttrs.emplace_back(std::move(entry.first));
                    } else {
                        otherScopeAttrs.emplace_back(std::move(entry.first));
                    }
                }

                // GroupGeometry has a primitive_attributes attr, so the
                // MoonrayRenderState will handle linking this up as normal
                if (!primitiveScopeAttrs.empty()) {
                    interface.setAttr("rdl2.sceneObject.instance.attrs.primitive_attributes",
                            kodachi::ZeroCopyStringAttribute::create(std::move(primitiveScopeAttrs)), false);
                }

                if (!otherScopeAttrs.empty()) {
                    interface.setAttr("rdl2.meta.autoInstancing.attrs.primitive_attributes",
                            kodachi::ZeroCopyStringAttribute::create(std::move(otherScopeAttrs)), false);
                }
            }
        } else {
            // KodachiGeometry
            const kodachi::GroupAttribute kodachiGeometryArbAttr =
                    kodachiGeometryArbAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain);

            if (kodachiGeometryArbAttr.isValid()) {
                interface.setAttr("rdl2.sceneObject.kodachiGeometry.arbitrary",
                                  kodachiGeometryArbAttr, false);

                // auto instancing
                const kodachi::IntAttribute autoInstancingEnabledAttr =
                        interface.getAttr("rdl2.meta.autoInstancing.enabled");

                if (autoInstancingEnabledAttr.isValid()) {
                    // Instances can have their own primitive scope arbitrary
                    // attributes, so store them as instanceAttrs and don't
                    // use them for calculating the instanceID hash
                    kodachi::GroupBuilder primAttrsGb;
                    for (const auto arbAttrPair : kodachiGeometryArbAttr) {
                        const kodachi::GroupAttribute arbAttr(arbAttrPair.attribute);
                        const kodachi::StringAttribute scopeAttr =
                                arbAttr.getChildByName("scope");
                        if (scopeAttr == kPrimitiveScopeAttr) {
                            primAttrsGb.set(arbAttrPair.name, arbAttrPair.attribute);
                            kodachiGeometryArbAttrsGb.del(arbAttrPair.name);
                        }
                    }

                    if (primAttrsGb.isValid()) {
                        interface.setAttr("rdl2.sceneObject.instance.arbitrary", primAttrsGb.build());
                    }

                    interface.setAttr("rdl2.meta.autoInstancing.attrs.primitive_attributes",
                            kodachiGeometryArbAttrsGb.build(), false);
                }
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Converts 'geometry.arbitrary' attributes to rdl2::UserData locations "
                           "for RdlGeometry, or added to ");
        builder.setHelp("All validation and whitelisting is expected to have happened before this Op is executed");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPPrimitiveAttributes)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPPrimitiveAttributes, "KPOPPrimitiveAttributes", 0, 1);
}

