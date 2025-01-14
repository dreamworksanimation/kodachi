// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

namespace {

KdLogSetup("KPOPGeometrySet");

const std::string kDefaultGeoSetPath("/root/__scenebuild/geometryset/default");
const std::string kRdl2("rdl2");

class KPOPGeometrySet: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            kodachi::op_args_builder::StaticSceneCreateOpArgsBuilder sscb(true);
            sscb.createEmptyLocation(kDefaultGeoSetPath, kRdl2);

            kodachi::GroupBuilder geosetAttrs;
            geosetAttrs.set("sceneClass", kodachi::StringAttribute("GeometrySet"));
            geosetAttrs.set("name", kodachi::StringAttribute(kDefaultGeoSetPath));
            geosetAttrs.set("disableAliasing", kodachi::IntAttribute(true));
            sscb.setAttrAtLocation(kDefaultGeoSetPath, "rdl2.sceneObject", geosetAttrs.build());

            interface.execOp("StaticSceneCreate", sscb.build());
        }

        interface.stopChildTraversal();
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Creates a default rdl2::GeometrySet");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPGeometrySet)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPGeometrySet, "KPOPGeometrySet", 0, 1);
}

