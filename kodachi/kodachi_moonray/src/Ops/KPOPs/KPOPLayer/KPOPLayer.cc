// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

namespace {

KdLogSetup("KPOPLayer");

const std::string kDefaultLayerPath("/root/__scenebuild/layer/default");
const std::string kRdl2("rdl2");

class KPOPLayer: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kLayerAttr("Layer");
        static const kodachi::StringAttribute kDefaultLayerPathAttr(kDefaultLayerPath);

        if (interface.atRoot()) {
            kodachi::op_args_builder::StaticSceneCreateOpArgsBuilder sscb(true);
            sscb.createEmptyLocation(kDefaultLayerPath, kRdl2);

            kodachi::GroupBuilder layerAttrsGb;
            layerAttrsGb.set("sceneClass", kLayerAttr);
            layerAttrsGb.set("name", kDefaultLayerPathAttr);
            layerAttrsGb.set("disableAliasing", kodachi::IntAttribute(true));
            sscb.setAttrAtLocation(kDefaultLayerPath, "rdl2.sceneObject", layerAttrsGb.build());

            interface.execOp("StaticSceneCreate", sscb.build());
        }

        interface.stopChildTraversal();
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Creates a default rdl2::Layer");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPLayer)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPLayer, "KPOPLayer", 0, 1);
}

