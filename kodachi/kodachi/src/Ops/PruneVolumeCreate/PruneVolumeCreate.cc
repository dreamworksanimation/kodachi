// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Katana
#include <kodachi/StringView.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/GeometryUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

// OpenEXR
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathLine.h>

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>

#include <FnGeolib/op/FnGeolibOp.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>

// kodachi
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/Op.h>

namespace {
KdLogSetup("PruneVolumeCreate");

class PruneVolumeCreateOp: public kodachi::GeolibOp {

public:

    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        const kodachi::StringAttribute pruneVolumeLocationAttr = interface.getOpArg("pruneVolumeLocation");

        if (!pruneVolumeLocationAttr.isValid()) {
            return;
        }

        static const std::string sCube = "cube";
        static const std::string sCylinder = "cylinder";
        static const std::string sSphere = "sphere";

        KdLogDebug(interface.getInputLocationPath());

        const kodachi::StringAttribute volumeTypeAttr = interface.getOpArg("volumeType");
        if (volumeTypeAttr.isValid()) {
            kodachi::string_view volumeName = volumeTypeAttr.getValueCStr();

            if (volumeName == sCylinder) {
                volumeName = "poly_cylinder";
            } else if (volumeName == sSphere) {
                volumeName = "poly_sphere";
            }

            const std::string volumePath = kodachi::concat(
                    getenv("KODACHI_ROOT"), "/",
                           "UI4/Resources/Geometry/PrimitiveCreate/",
                           volumeName, ".attrs");

            kodachi::GroupBuilder gb;
            gb.set("fileName", kodachi::StringAttribute(volumePath));
            interface.execOp("ApplyAttrFile", gb.build());

            interface.setAttr("type", kodachi::StringAttribute("prune volume"));
            interface.setAttr("viewer.default.drawOptions.fill", kodachi::StringAttribute("wireframe"));

            static const kodachi::FloatAttribute::value_type color[3] = { 0.7f, 0.15f, 0.15f };
            interface.setAttr("viewer.default.drawOptions.color", kodachi::FloatAttribute(color, 3, 1));
        }

        interface.stopChildTraversal();
    }
};

DEFINE_GEOLIBOP_PLUGIN(PruneVolumeCreateOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(PruneVolumeCreateOp, "PruneVolumeCreateOp", 0, 2);
}

