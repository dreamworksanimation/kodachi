// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include <kodachi/op/Op.h>

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/XFormUtil.h>

// stl
#include <array>

namespace {
KdLogSetup("KPOPNode");

class KPOPNode : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isNode\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::GroupAttribute xformAttr = interface.getAttr("xform");

        if (!xformAttr.isValid()) {
            KdLogDebug("No 'xform' attribute");
            return;
        }

        const bool isMotionBlurEnabled = kodachi::IntAttribute(
                interface.getAttr("rdl2.meta.mbEnabled")).getValue();

        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();

        kodachi::DoubleAttribute nodeXformAttr;

        if (isMotionBlurEnabled) {
            const float shutterClose = kodachi::FloatAttribute(
                    interface.getAttr("rdl2.meta.shutterClose")).getValue();

            const std::array<float, 2> sampleTimes { shutterOpen, shutterClose };
            nodeXformAttr = kodachi::XFormUtil::CalcTransformMatrixAtTimes(
                    xformAttr, sampleTimes.data(), sampleTimes.size()).first;
        } else {
            nodeXformAttr = kodachi::XFormUtil::CalcTransformMatrixAtTime(
                    xformAttr, shutterOpen).first;
        }

        interface.setAttr("rdl2.sceneObject.attrs.node_xform", nodeXformAttr, false);

        if (interface.getAttr("rdl2.meta.autoInstancing.enabled").isValid()) {
            interface.setAttr("rdl2.sceneObject.instance.attrs.node_xform", nodeXformAttr, false);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Calculates the final xform for locations that are rdl2::Nodes");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPNode)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPNode, "KPOPNode", 0, 1);
}

