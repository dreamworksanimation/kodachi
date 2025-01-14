// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/PathUtil.h>

#include <sstream>
#include <map>
#include <unordered_set>

namespace {

const std::array<float, 3> kRed { 1.0f, 0.0f, 0.0f };
const std::array<float, 3> kGreen { 0.0f, 1.0f, 0.0f };
const std::array<float, 3> kBlue { 0.0f, 0.0f, 1.0f };

const std::string kDwaBaseMaterialType { "DwaBaseMaterial" };

const std::string kRgbMatte { "rgbmatte" };
const std::unordered_set<std::string> kMatteTypes {
    kRgbMatte
};

kodachi::GroupAttribute
createMaterialNode(const std::string& materialType,
                   const std::string& label,
                   const kodachi::FloatAttribute& color)
{
    kodachi::GroupBuilder gb;
    kodachi::GroupBuilder pgb;

    // build parameters
    pgb.set("label", kodachi::StringAttribute(label));
    pgb.set("show_specular", kodachi::IntAttribute(0));
    pgb.set("show_diffuse", kodachi::IntAttribute(0));
    pgb.set("show_transmission", kodachi::IntAttribute(0));
    pgb.set("show_emission", kodachi::IntAttribute(1));
    pgb.set("emission", color);

    gb.set("name", kodachi::StringAttribute("__moonrayMatteMaterial"));
    gb.set("srcName", kodachi::StringAttribute("__moonrayMatteMaterial"));
    gb.set("type", kodachi::StringAttribute(materialType));
    gb.set("parameters", pgb.build());

    return gb.build();
}


class MoonrayMatteMaterialOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.getInputLocationPath() == "/root") {
            const kodachi::GroupAttribute renderPassMattesAttr =
                    interface.getAttr("renderPass.mattes");

            if (!renderPassMattesAttr.isValid()) {
                // without mattes there is no reason for this to run
                interface.stopChildTraversal();
                return;
            }
        }

        static const kodachi::StringAttribute kCELMatchAttr("/root/world/geo//*");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // the local mattes attribute takes priority
        // if not set the mattes attribute will be taken
        // from the op args and propagated to child locations
        const kodachi::GroupAttribute localMattesAttr =
                interface.getAttr("mattes");

        const kodachi::GroupAttribute inheritedMattesAttr =
                interface.getOpArg("mattes");

        const kodachi::GroupAttribute mattesAttr = localMattesAttr.isValid()
                                                         ? localMattesAttr
                                                         : inheritedMattesAttr;

        if (!mattesAttr.isValid()) {
            return;
        }

        // propagate the mattes attribute to children locations
        if (localMattesAttr.isValid()) {
            interface.replaceChildTraversalOp("", kodachi::GroupAttribute("mattes", mattesAttr, false));
        }

        const kodachi::GroupAttribute materialAttr =
                interface.getAttr("material");

        // without a material it's not a valid location
        // type to apply the matte to
        if (!materialAttr.isValid()) {
            return;
        }

        const kodachi::GroupAttribute nodesAttr =
                interface.getAttr("material.nodes");

        // create matte materials nodes
        for (const auto& child : mattesAttr) {
            const kodachi::GroupAttribute matteAttr = child.attribute;
            const kodachi::string_view matteName = child.name;

            const kodachi::StringAttribute matteTypeAttr =
                    matteAttr.getChildByName("matteType");

            if (matteTypeAttr.isValid()) {
                // rgb matte types
                const bool isMatte = kMatteTypes.find(matteTypeAttr.getValue()) != kMatteTypes.end();
                if (isMatte && matteTypeAttr == kRgbMatte) {
                    kodachi::GroupBuilder ngb;
                    ngb.update(nodesAttr);

                    const kodachi::StringAttribute redChannelAttr =
                            matteAttr.getChildByName("channels.red.label");

                    const kodachi::StringAttribute greenChannelAttr =
                            matteAttr.getChildByName("channels.green.label");

                    const kodachi::StringAttribute blueChannelAttr =
                            matteAttr.getChildByName("channels.blue.label");

                    // TODO: The following will clash if multiple channels
                    // are defined for the same location or multiple mattes
                    // contribute to the same channel on the same location.
                    // At the moment, the last will win in both cases. Look
                    // into handling this better
                    if (redChannelAttr.isValid()) {
                        const kodachi::GroupAttribute redMaterial =
                                createMaterialNode(kDwaBaseMaterialType,
                                                   redChannelAttr.getValue(),
                                                   kodachi::FloatAttribute(kRed.data(), kRed.size(), 1));
                        ngb.set("__moonrayMatteMaterial", redMaterial);
                    }

                    if (greenChannelAttr.isValid()) {
                        const kodachi::GroupAttribute greenMaterial =
                                createMaterialNode(kDwaBaseMaterialType,
                                                   greenChannelAttr.getValue(),
                                                   kodachi::FloatAttribute(kGreen.data(), kGreen.size(), 1));
                        ngb.set("__moonrayMatteMaterial", greenMaterial);
                    }

                    if (blueChannelAttr.isValid()) {
                        const kodachi::GroupAttribute blueMaterial =
                                createMaterialNode(kDwaBaseMaterialType,
                                                   blueChannelAttr.getValue(),
                                                   kodachi::FloatAttribute(kBlue.data(), kBlue.size(), 1));

                        ngb.set("__moonrayMatteMaterial", blueMaterial);
                    }

                    const kodachi::GroupAttribute matteNodesAttr = ngb.build();
                    if (matteNodesAttr.isValid()) {
                        kodachi::GroupBuilder mgb;
                        mgb.setGroupInherit(false);
                        mgb.update(materialAttr);

                        mgb.set("terminals.moonrayMaterial", kodachi::StringAttribute("__moonrayMatteMaterial"));
                        mgb.set("nodes", matteNodesAttr);
                        interface.setAttr("material", mgb.build());
                    }
                }
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Turns a matte into a DwaBaseMaterial "
                           "and overrides the moonrayMaterial terminal");
        builder.setHelp("Finds the locations whose 'mattes' and 'material' "
                        "attributes have been set.  It will then create a DwaBaseMaterial"
                        "and set the necessary attributes to add it to the location's "
                        "material network");
        builder.setNumInputs(0);

        return builder.build();
    }
};


DEFINE_GEOLIBOP_PLUGIN(MoonrayMatteMaterialOp)

} // anonymous namespace


void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayMatteMaterialOp, "MoonrayMatteMaterial", 0, 1);
}

