// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <array>

namespace {

class KPOPRendererProcedural : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root//*{@type=="rdl2" and @rdl2.meta.kodachiType=="renderer procedural"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::GroupAttribute rendererProceduralAttr =
                interface.getAttr("rendererProcedural");

        const kodachi::StringAttribute proceduralAttr =
                rendererProceduralAttr.getChildByName("procedural");

        if (!proceduralAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface,
                    "Missing 'rendererProcedural.procedural' attribute");
            return;
        }

        interface.setAttr("rdl2.sceneObject.sceneClass", proceduralAttr, false);

        const std::string objectName = kodachi::concat(
                interface.getInputLocationPath(), "_", proceduralAttr.getValueCStr());
        const kodachi::StringAttribute objectNameAttr(objectName);

        interface.setAttr("rdl2.sceneObject.name", objectNameAttr);

        const kodachi::GroupAttribute proceduralArgsAttr =
                rendererProceduralAttr.getChildByName("args");

        kodachi::GroupBuilder attrsGb;
        attrsGb.setGroupInherit(false).update(proceduralArgsAttr);

        interface.setAttr("rdl2.sceneObject.attrs", attrsGb.build(), false);

        // Assume all renderer procedurals are geometry
        interface.setAttr("rdl2.meta.isNode", kodachi::IntAttribute(true));
        interface.setAttr("rdl2.meta.isLayerAssignable", kodachi::IntAttribute(true));

        // Only try to assign a material if one exists. We don't want to
        // apply a default material.
        if (interface.getAttr("material").isValid()) {
            interface.setAttr("rdl2.meta.isMaterialAssignable", kodachi::IntAttribute(true));
        }

        interface.setAttr("rdl2.meta.isGeometry", kodachi::IntAttribute(true));

        // auto instancing
        {
            const kodachi::IntAttribute autoInstancingEnabledAttr =
                    interface.getAttr("rdl2.meta.autoInstancing.enabled");

            if (autoInstancingEnabledAttr.getValue(true, false)) {
                kodachi::GroupBuilder autoInstancingAttrsGb;
                autoInstancingAttrsGb
                    .setGroupInherit(false)
                    .update(interface.getAttr("rdl2.meta.autoInstancing.attrs"))
                    .update(rendererProceduralAttr);

                interface.setAttr("rdl2.meta.autoInstancing.attrs", autoInstancingAttrsGb.build());
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Converts attributes from a 'renderer procedural' location to rdl2 format.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPRendererProcedural)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPRendererProcedural, "KPOPRendererProcedural", 0, 1);
}

