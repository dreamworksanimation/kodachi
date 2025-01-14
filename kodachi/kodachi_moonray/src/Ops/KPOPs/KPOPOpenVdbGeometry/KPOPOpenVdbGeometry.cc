// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {

KdLogSetup("KPOPOpenVdbGeometry");

class KPOPOpenVdbGeometry : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and @rdl2.meta.kodachiType=="volume"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // SceneClass and SceneObject name
        {
            static const kodachi::StringAttribute kOpenVdbGeometryAttr("OpenVdbGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kOpenVdbGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_OpenVdbGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        }

        const kodachi::GroupAttribute vdbAttr = interface.getAttr("geometry.vdb");
        if (!vdbAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface,
                    "Missing 'geometry.vdb' attributes");
            return;
        }

        const kodachi::StringAttribute modelAttr = vdbAttr.getChildByName("model");
        if (!modelAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface,
                    "Missing 'geometry.vdb.model' attribute");
            return;
        }

        const kodachi::IntAttribute interpolationAttr = vdbAttr.getChildByName("interpolation");
        if (!interpolationAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface,
                    "Missing 'geometry.vdb.interpolation' attribute");
            return;
        }

        kodachi::GroupBuilder attrsGb;
        attrsGb.setGroupInherit(false)
               .update(interface.getAttr("rdl2.sceneObject.attrs"))
               .set("model", modelAttr)
               .set("interpolation", interpolationAttr);

        interface.setAttr("rdl2.sceneObject.attrs", attrsGb.build(), false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Sets attributes on locations that represent an OpenVdbGeometry.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPOpenVdbGeometry)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPOpenVdbGeometry, "KPOPOpenVdbGeometry", 0, 1);
}

