// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <iomanip>
#include <set>
#include <unordered_map>
#include <vector>

namespace {
KdLogSetup("KPOPArbitraryAttrWhitelist");

enum class WhitelistMode {
    AUTO,
    ENABLED,
    DISABLED
};

class KPOPArbitraryAttrWhitelist: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            // find all outputs that require primitive attributes
            const kodachi::GroupAttribute outputChannelsAttr =
                    interface.getAttr("moonrayGlobalStatements.outputChannels");

            if (outputChannelsAttr.isValid()) {
                std::set<kodachi::string_view> primitiveAttributes;

                for (const auto attrPair : outputChannelsAttr) {
                    const kodachi::GroupAttribute outputAttr =
                            attrPair.attribute;

                    const kodachi::StringAttribute resultAttr =
                            outputAttr.getChildByName("result");

                    // Moonray render output types that potentially require
                    // primitive attributes
                    static const kodachi::StringAttribute kResultPrimitiveAttributeAttr("primitive attribute");
                    static const kodachi::StringAttribute kResultMaterialAovAttr("material aov");

                    if (resultAttr == kResultPrimitiveAttributeAttr) {
                        const kodachi::StringAttribute primitiveAttributeAttr =
                                outputAttr.getChildByName("primitive_attribute");

                        if (primitiveAttributeAttr.isValid()) {
                            primitiveAttributes.emplace(primitiveAttributeAttr.getValueCStr());
                        }
                    } else if (resultAttr == kResultMaterialAovAttr) {
                        const kodachi::StringAttribute materialAovAttr =
                                outputAttr.getChildByName("material_aov");

                        if (materialAovAttr.isValid()) {
                            const kodachi::string_view materialAov =
                                    materialAovAttr.getValueCStr();

                            // The <property> of a material aov expression can
                            // specify primitive attributes in the form of:
                            // float:<attr>
                            // rgb:<attr>
                            // vec2f:<attr>
                            // vec3f:<attr>
                            //
                            // so search for the ':'
                            const std::size_t pos = materialAov.find_last_of(':');
                            if (pos != kodachi::string_view::npos) {
                                primitiveAttributes.emplace(materialAov.substr(pos + 1));
                            }
                        }
                    }
                }

                if (!primitiveAttributes.empty()) {
                    std::vector<std::string> primAttrNames(std::begin(primitiveAttributes),
                                                           std::end(primitiveAttributes));

                    const kodachi::GroupAttribute opArgsAttr(
                            "outputChannelPrimAttrs",
                            kodachi::ZeroCopyStringAttribute::create(
                                    std::move(primAttrNames)), false);

                    interface.replaceChildTraversalOp("", opArgsAttr);
                }
            }

            return;
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and hasattr("geometry.arbitrary")})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // Special case for instances
        // We can't determine much about an instance without cooking the source.
        // Moonray allows instances to have their own primitive scope
        // arbitrary attributes so keep those
        static const kodachi::StringAttribute kInstanceAttr("instance");
        const kodachi::StringAttribute typeAttr =
                interface.getAttr("rdl2.meta.kodachiType");

        if (typeAttr == kInstanceAttr) {
            const kodachi::GroupAttribute arbitraryAttrs =
                    interface.getAttr("geometry.arbitrary");

            kodachi::GroupBuilder arbAttrGb;
            arbAttrGb.update(arbitraryAttrs);

            for (const auto arbAttrPair : arbitraryAttrs) {
                static const kodachi::StringAttribute kPrimitiveScopeAttr("primitive");

                const kodachi::GroupAttribute arbAttr(arbAttrPair.attribute);
                const kodachi::StringAttribute scopeAttr =
                        arbAttr.getChildByName("scope");

                if (scopeAttr != kPrimitiveScopeAttr) {
                    arbAttrGb.del(arbAttrPair.name);
                }
            }

            interface.setAttr("geometry.arbitrary", arbAttrGb.build());

            return;
        }

        const kodachi::GroupAttribute arbitraryAttrsAttr =
                interface.getAttr("moonrayStatements.arbitraryAttrs");

        // This attr is only used by this op, so we can now remove it
        interface.deleteAttr("moonrayStatements.arbitraryAttrs");

        WhitelistMode mode = WhitelistMode::AUTO;
        {
            const kodachi::IntAttribute modeAttr =
                    arbitraryAttrsAttr.getChildByName("whitelistMode");

            if (modeAttr.isValid()) {
                mode = static_cast<WhitelistMode>(modeAttr.getValue());
            }
        }

        if (mode == WhitelistMode::DISABLED) {
            // keep all arbitrary attributes
            return;
        } else if (mode == WhitelistMode::AUTO) {
            // prefetch all children
            const auto potentialChildrenSamples =
                    interface.getPotentialChildren().getSamples();
            if (potentialChildrenSamples.isValid()) {
                for (const kodachi::string_view child : potentialChildrenSamples.front()) {
                    interface.prefetch(child);
                }
            }
        }

        const kodachi::GroupAttribute arbitraryAttrs =
                interface.getAttr("geometry.arbitrary");

        // Delete all of the arbitrary attributes,
        // we'll add back the whitelisted ones
        interface.deleteAttr("geometry.arbitrary");

        kodachi::GroupBuilder arbAttrGb;

        // UVs are an arbitrary attribute by convention, but shouldn't be
        // involved in whitelisting. If the user wants them removed, they can
        // use an AttributeSet
        {
            const kodachi::GroupAttribute stAttr =
                    arbitraryAttrs.getChildByName("st");

            if (stAttr.isValid()) {
                arbAttrGb.set("st", stAttr);
            }
        }

        // Currently the same for acceleration
        // TODO: Look into moving accel attr to geometry.point
        {
            const kodachi::GroupAttribute accelAttr =
                    arbitraryAttrs.getChildByName("accel");

            if (accelAttr.isValid()) {
                arbAttrGb.set("accel", accelAttr);
            }
        }

        // Not disabled, so always check for the whitelist attr
        {
            // For now, whitelist is a list of attr names delimited by '|'
            // TODO: Look into regex-like syntax
            const kodachi::StringAttribute whitelistAttr =
                    arbitraryAttrsAttr.getChildByName("whitelist");

            if (whitelistAttr.isValid()) {
                kodachi::string_view whitelist(whitelistAttr.getValueCStr());
                while(!whitelist.empty()) {
                    const std::size_t pos = whitelist.find('|');
                    kodachi::string_view attrName;
                    if (pos != kodachi::string_view::npos) {
                        // get the attrname without the delimiter
                        attrName = whitelist.substr(0, pos);
                        whitelist.remove_prefix(pos + 1);
                    } else {
                        // no more delimiters
                        attrName = whitelist;
                        whitelist.remove_prefix(whitelist.size());
                    }

                    const auto arbAttr = arbitraryAttrs.getChildByName(attrName);
                    if (arbAttr.isValid()) {
                        arbAttrGb.set(attrName, arbAttr);
                    }
                }
            }
        }

        if (mode == WhitelistMode::AUTO) {
            // check for any arbitrary attrs required by the output channels
            {
                const auto outputChannelPrimAttrs =
                        kodachi::StringAttribute(
                                interface.getOpArg("outputChannelPrimAttrs")).getSamples();

                if (outputChannelPrimAttrs.isValid()) {
                    for (const kodachi::string_view primAttrName : outputChannelPrimAttrs.front()) {
                        const auto arbAttr = arbitraryAttrs.getChildByName(primAttrName);
                        if (arbAttr.isValid()) {
                            arbAttrGb.set(primAttrName, arbAttr);
                        }
                    }
                }
            }

            // find all AttributeMap material nodes in the geometry's material
            // and each facesets material. Whitelist their
            // 'primitive_attribute_name' attribute
            std::vector<kodachi::GroupAttribute> materialAttrs;
            {
                const kodachi::GroupAttribute materialAttr =
                        interface.getAttr("material");

                if (materialAttr.isValid()) {
                    materialAttrs.emplace_back(materialAttr);
                }
            }

            const auto potentialChildrenSamples =
                    interface.getPotentialChildren().getSamples();
            if (potentialChildrenSamples.isValid()) {
                for (const kodachi::string_view child : potentialChildrenSamples.front()) {
                    const kodachi::IntAttribute isPartAttr =
                            interface.getAttr("rdl2.meta.isPart", child);

                    if (isPartAttr.isValid()) {
                        materialAttrs.emplace_back(interface.getAttr("material", child));
                    }
                }
            }

            for (const auto materialAttr : materialAttrs) {
                const kodachi::GroupAttribute nodesAttr =
                        materialAttr.getChildByName("nodes");

                for (const auto node : nodesAttr) {
                    static const kodachi::StringAttribute kAttributeMapAttr("AttributeMap");

                    static const kodachi::StringAttribute kHairColumnMapType("HairColumnMap"); // scatter_tag
                    static const kodachi::StringAttribute kRandomMapType("RandomMap"); // random_color

                    const kodachi::GroupAttribute nodeAttr(node.attribute);
                    const kodachi::StringAttribute typeAttr =
                            nodeAttr.getChildByName("type");

                    if (typeAttr == kAttributeMapAttr) {
                        const kodachi::StringAttribute primitiveAttributeNameAttr =
                                nodeAttr.getChildByName("parameters.primitive_attribute_name");

                        // Initialize with the default value
                        kodachi::string_view primAttrName("Cd", 2);
                        if (primitiveAttributeNameAttr.isValid()) {
                            primAttrName = primitiveAttributeNameAttr.getValueCStr();
                        }

                        const auto arbAttr = arbitraryAttrs.getChildByName(primAttrName);
                        if (arbAttr.isValid()) {
                            arbAttrGb.set(primAttrName, arbAttr);
                        }
                    } else {
                        const auto& primAttrNameMap = getPrimAttrNameMap();
                        const auto iter = primAttrNameMap.find(typeAttr);

                        if (typeAttr == kHairColumnMapType) {
                            arbAttrGb.set("requiresScatterTag", kodachi::IntAttribute(1));
                        }
                        else if (typeAttr == kRandomMapType) {
                            arbAttrGb.set("requiresRandomColor", kodachi::IntAttribute(1));
                        }

                        if (iter != primAttrNameMap.end()) {
                            for (const auto& primAttrName : iter->second) {
                                const auto arbAttr = arbitraryAttrs.getChildByName(primAttrName);
                                if (arbAttr.isValid()) {
                                    arbAttrGb.set(primAttrName, arbAttr);
                                }
                            }
                        }
                    }
                }
            }

            /**
             * Moonray's InstanceGeometry supports primitive attributes where
             * There is once value per instance. Instead of looking at the materials
             * of the reference geometries, we'll whitelist and attributes that
             * have a valid number of values.
             */
            static const kodachi::StringAttribute kInstanceArrayAttr("instance array");
            if (interface.getAttr("rdl2.meta.kodachiType") == kInstanceArrayAttr) {
                for (const auto arbAttrPair : arbitraryAttrs) {
                    static const kodachi::StringAttribute kPointAttr("point");

                    const kodachi::GroupAttribute arbAttr = arbAttrPair.attribute;
                    const kodachi::StringAttribute scopeAttr = arbAttr.getChildByName("scope");
                    if (scopeAttr == kPointAttr) {
                        arbAttrGb.set(arbAttrPair.name, arbAttr);
                    }
                }
            }
        }

        if (arbAttrGb.isValid()) {
            arbAttrGb.setGroupInherit(false);
            interface.setAttr("geometry.arbitrary", arbAttrGb.build(), false);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Removes arbitrary attributes that are not in the whitelist");

        std::stringstream ss;
        ss << "When 'whitelistMode' is AUTO, the location's material nodes are used to "
           << "determine which arbitrary attributes to whitelist.\n\n"
           << "For AttributeMaps, the 'primitive_attribute_name' attribute is used.\n\n"
           << "For 'instance array' locations, their arbitrary attributes of scope 'point' are whitelisted.\n\n"
           << "The other Shaders and the attributes they check for are:\n";

        for (const auto& iterPair : getPrimAttrNameMap()) {
            ss << iterPair.first.getValueCStr() << ":[ ";
            const auto& attrNames = iterPair.second;
            for (auto iter = attrNames.cbegin(); iter != attrNames.cend(); ++iter) {
                if (iter != attrNames.cbegin()) {
                    ss << ", ";
                }
                ss << *iter;
            }
            ss << " ]\n\n";
        }
        builder.setHelp(ss.str());
        builder.describeInputAttr(InputAttrDescription(AttrTypeDescription::kTypeStringAttribute,
                                                       "moonrayStatements.arbitraryAttrs.whitelist"));
        builder.describeInputAttr(InputAttrDescription(AttrTypeDescription::kTypeGroupAttribute,
                                                       "geometry.arbitrary"));
        builder.describeOutputAttr(OutputAttrDescription(AttrTypeDescription::kTypeGroupAttribute,
                                                         "geometry.arbitrary"));

        return builder.build();
    }

protected:
    using PrimAttrNameMap = std::unordered_map<kodachi::StringAttribute, std::vector<std::string>, kodachi::AttributeHash>;

    /*
     * Map of Moonray shaders to their required and optional primitive attributes.
     * Found by looking at the entries added to rdl2::Shader::mRequiredAttributes
     * and rdl2::Shader::mOptionalAttributes for each dso.
     */
    static const PrimAttrNameMap& getPrimAttrNameMap()
    {
        static const std::unordered_map<kodachi::StringAttribute,
            std::vector<std::string>, kodachi::AttributeHash> kPrimitiveAttributesMap
        {
            { "DirectionalMap",             { "ref_P", "ref_N", "ref_P_transform" } },
            { "GradientMap",                { "ref_P", "ref_P_transform" } },
            { "HairColumnMap",              { "scatter_tag" } },
            { "RandomMap",                  { "random_color" } },
            { "ImageMap",                   { "surface_st" } },
            { "NoiseMap",                   { "ref_P", "ref_P_transform" } },
            { "NoiseWorleyMap",             { "ref_P", "ref_P_transform" } },
            { "OceanMap",                   { "ref_P", "ref_P_transform" } },
            { "OpenVdbMap",                 { "ref_P", "ref_P_transform" } },
            { "ProjectCameraMap",           { "ref_P", "ref_N", "ref_P_transform" } },
            { "ProjectCameraMap_v2",        { "ref_P", "ref_N", "ref_P_transform" } },
            { "ProjectCylindricalMap",      { "ref_P", "ref_N", "ref_P_transform" } },
            { "ProjectPlanarMap",           { "ref_P", "ref_P_transform"  } },
            { "ProjectPlanarNormalMap",     { "ref_P", "ref_P_transform" } },
            { "ProjectSphericalMap",        { "ref_P", "ref_P_transform"  } },
            { "ProjectTriplanarMap",        { "ref_P", "ref_N", "ref_P_transform" } },
            { "ProjectTriplanarNormalMap",  { "ref_P", "ref_N", "ref_P_transform" } },
            { "ProjectTriplanarUdimMap",    { "ref_P", "ref_N", "ref_P_transform" } },
            { "RampMap",                    { "ref_P", "ref_P_transform"  } },
            { "UVTransformMap",             { "ref_P", "ref_P_transform" } },
            { "GlitterFlakeMaterial",       { "ref_P", "ref_N" } },
            { "GlitterFlakeMaterial_v2",    { "ref_P", "ref_N" } },
            { "AmorphousVolume",            { "amorphous_meta_data" } },
        };

        return kPrimitiveAttributesMap;
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPArbitraryAttrWhitelist)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPArbitraryAttrWhitelist, "KPOPArbitraryAttrWhitelist", 0, 1);
}

