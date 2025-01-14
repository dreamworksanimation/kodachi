// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {

KdLogSetup("KPOPLayerAssign");

class KPOPLayerAssign: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isLayerAssignable\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::IntAttribute isPartAttr = interface.getAttr("rdl2.meta.isPart");
        const bool isPart = isPartAttr.getValue(false, false);

        std::string inputLocationPath = interface.getInputLocationPath();
        kodachi::StringAttribute geometryNameAttr;

        if (isPart) {
            const std::size_t pos = inputLocationPath.find_last_of('/');
            const std::string partName = inputLocationPath.substr(pos + 1);
            inputLocationPath.erase(pos);


            geometryNameAttr = kodachi::StringAttribute(inputLocationPath);

            const kodachi::StringAttribute partAttr =
                    kodachi::StringAttribute(partName);
            interface.setAttr("rdl2.layerAssign.part", partAttr, false);
        } else {
            geometryNameAttr = kodachi::StringAttribute(inputLocationPath);
        }

        interface.setAttr("rdl2.layerAssign.geometry", geometryNameAttr);

        static const kodachi::StringAttribute kDefaultLayerAttr("/root/__scenebuild/layer/default");
        interface.setAttr("rdl2.layerAssign.layer", kDefaultLayerAttr, false);

        // auto instancing
        const kodachi::IntAttribute autoInstancingEnabledAttr =
                interface.getAttr("rdl2.meta.autoInstancing.enabled");

        if (autoInstancingEnabledAttr.isValid()) {
            interface.setAttr("rdl2.meta.autoInstancing.attrs.layer", kDefaultLayerAttr);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Assigns all Geometry and Facesets to the default Layer");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPLayerAssign)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPLayerAssign, "KPOPLayerAssign", 0, 1);
}

