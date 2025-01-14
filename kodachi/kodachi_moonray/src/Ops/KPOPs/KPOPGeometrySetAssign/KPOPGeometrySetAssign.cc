// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {

KdLogSetup("KPOPGeometrySetAssign");

class KPOPGeometrySetAssign: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and hasattr("rdl2.meta.isGeometry")})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        interface.setAttr("rdl2.geoSetAssign.geometry",
                kodachi::StringAttribute(interface.getInputLocationPath()), false);

        kodachi::Attribute geometrySetAttr =
                interface.getAttr("rdl2.geoSetAssign.geometrySet");

        if (!geometrySetAttr.isValid()) {
            static const kodachi::StringAttribute kDefaultGeometrySetAttr(
                    "/root/__scenebuild/geometryset/default");

            geometrySetAttr = kDefaultGeometrySetAttr;
            interface.setAttr("rdl2.geoSetAssign.geometrySet",
                              kDefaultGeometrySetAttr, false);
        }

        // auto instancing
        const kodachi::IntAttribute autoInstancingEnabledAttr =
                interface.getAttr("rdl2.meta.autoInstancing.enabled");

        if (autoInstancingEnabledAttr.isValid()) {
            interface.setAttr("rdl2.meta.autoInstancing.attrs.geometrySet",
                              geometrySetAttr, false);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Assigns all Geometry to a GeometrySet.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPGeometrySetAssign)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPGeometrySetAssign, "KPOPGeometrySetAssign", 0, 1);
}

