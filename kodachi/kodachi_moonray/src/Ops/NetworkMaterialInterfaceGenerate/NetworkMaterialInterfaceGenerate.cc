// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <pystring/pystring.h>

#include <unordered_set>

namespace {

class NetworkMaterialInterfaceGenerateOp : public kodachi::GeolibOp
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const std::string kPattern { "pattern" };
        static const std::string kWhitelist { "whitelist" };

        const kodachi::StringAttribute locationAttr =
                interface.getOpArg("location");

        const kodachi::GroupAttribute nodesOpArg =
                interface.getOpArg("nodes");

        const kodachi::GroupAttribute paramsOpArg =
                interface.getOpArg("parameters");

        if (locationAttr.isValid()) {
            kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
            kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, locationAttr);

            if (!celInfo.canMatchChildren) {
                // if there is no matching children just stop traversing
                interface.stopChildTraversal();
            }

            if (locationAttr == interface.getInputLocationPath()) {

                std::string nodePattern;
                std::unordered_set<std::string> nodeWhitelist;

                if (nodesOpArg.isValid()) {

                    const kodachi::StringAttribute modeAttr =
                            nodesOpArg.getChildByName("mode");

                    if (modeAttr.isValid()) {
                        const kodachi::StringAttribute nodeWhitelistAttr =
                                nodesOpArg.getChildByName(kWhitelist);

                        const kodachi::StringAttribute nodePatternAttr =
                                nodesOpArg.getChildByName(kPattern);

                        if (modeAttr == kPattern
                                && nodePatternAttr.isValid()) {
                            nodePattern = nodePatternAttr.getValue();

                        } else if (modeAttr == kWhitelist
                                && nodeWhitelistAttr.isValid()) {
                            std::vector<std::string> whitelist;
                            whitelist.reserve(nodeWhitelistAttr.getNumberOfValues());

                            pystring::split(nodeWhitelistAttr.getValue(), whitelist);
                            nodeWhitelist.insert(whitelist.begin(), whitelist.end());
                        }
                    }
                }

                std::unordered_set<std::string> paramsWhitelist;

                if (paramsOpArg.isValid()) {
                    const kodachi::StringAttribute paramsWhitelistAttr =
                            paramsOpArg.getChildByName(kWhitelist);

                    std::vector<std::string> whitelist;
                    whitelist.reserve(paramsWhitelistAttr.getNumberOfValues());

                    pystring::split(paramsWhitelistAttr.getValue(), whitelist);
                    paramsWhitelist.insert(whitelist.begin(), whitelist.end());
                }

                const kodachi::GroupAttribute materialNodesAttr =
                        interface.getAttr("material.nodes");

                if (materialNodesAttr.isValid()) {

                    const kodachi::GroupAttribute materialDaps =
                            kodachi::ThreadSafeCookDaps(interface, "material");

                    kodachi::GroupBuilder gb;
                    gb.setGroupInherit(false);

                    for (const auto& materialNode : materialNodesAttr) {

                        if (!nodePattern.empty()
                                && pystring::find(materialNode.name.data(), nodePattern) == -1) {
                            continue;
                        }

                        if (!nodeWhitelist.empty()
                                && nodeWhitelist.count(materialNode.name.data()) == 0) {
                            continue;
                        }

                        const auto dap = kodachi::concat("__meta.material.c.nodes.c.",
                                                         materialNode.name,
                                                         ".c.parameters.c");

                        const kodachi::GroupAttribute materialParamAttrs =
                                materialDaps.getChildByName(dap);

                        if (materialParamAttrs.isValid()) {

                            for (const auto& child : materialParamAttrs) {

                                if (!paramsWhitelist.empty()
                                        && paramsWhitelist.count(child.name.data()) == 0) {
                                    continue;
                                }

                                const auto param = kodachi::concat(materialNode.name, "_", child.name);
                                const auto src = kodachi::concat(materialNode.name, ".", child.name);

                                kodachi::GroupBuilder mpgb;
                                mpgb.set("src", kodachi::StringAttribute(src), false);
                                mpgb.set("hints", kodachi::GroupAttribute("page",
                                                                          kodachi::StringAttribute(materialNode.name.data()),
                                                                          "label",
                                                                          kodachi::StringAttribute(child.name.data()),
                                                                          false));

                                gb.set(param, mpgb.build(), false);

                            }
                        }
                    }

                    interface.setAttr("material.interface", gb.build());
                    interface.deleteAttr("material.__applyNodeDefaults");
                }

                interface.stopChildTraversal();
            }
        }
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(NetworkMaterialInterfaceGenerateOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(NetworkMaterialInterfaceGenerateOp, "NetworkMaterialInterfaceGenerate", 0, 1);
}

