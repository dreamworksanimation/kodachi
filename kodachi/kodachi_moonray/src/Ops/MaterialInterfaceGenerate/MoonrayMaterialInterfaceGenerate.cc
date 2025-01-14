// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {

class MoonrayMaterialInterfaceGenerateOp : public kodachi::GeolibOp
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const auto& rootNodeName =
                interface.getAttr("material.terminals.moonrayMaterial");
        if (rootNodeName.isValid()) {
            const std::string nodeName =
                    kodachi::StringAttribute(rootNodeName).getValue();
            const std::string nodeType =
                    kodachi::StringAttribute(interface.getAttr("material.nodes."+nodeName+".type")).getValue();

            kodachi::GroupAttribute materialDaps =
                    kodachi::ThreadSafeCookDaps(interface, "material");

            const kodachi::GroupAttribute materialParamAttrs =
                    materialDaps.getChildByName("__meta.material.c.nodes.c."+nodeName+".c.parameters.c");

            kodachi::GroupBuilder gb;
            for (int i = 0; i < materialParamAttrs.getNumberOfChildren(); ++i) {
                const auto materialParamName = materialParamAttrs.getChildName(i);
                gb.set(materialParamName+".src", kodachi::StringAttribute(nodeName+"."+materialParamName));
            }

            interface.setAttr("material.interface", gb.build());
        }
        interface.deleteAttr("material.__applyNodeDefaults");
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(MoonrayMaterialInterfaceGenerateOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayMaterialInterfaceGenerateOp, "MoonrayMaterialInterfaceGenerate", 0, 1);
}

