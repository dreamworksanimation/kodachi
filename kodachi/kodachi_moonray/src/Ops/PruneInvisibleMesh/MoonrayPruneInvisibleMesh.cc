// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/logging/KodachiLogging.h>

namespace {

KdLogSetup("MoonrayPruneInvisibleMeshOp");

class MoonrayPruneInvisibleMeshOp : public kodachi::GeolibOp
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world/geo//*{hasattr(\"geometry\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        static const kodachi::StringAttribute kPolymesh("polymesh");
        static const kodachi::StringAttribute kSubdmesh("subdmesh");
        const kodachi::StringAttribute typeAttr = interface.getAttr("type");

        const bool isMesh = typeAttr == kPolymesh || typeAttr == kSubdmesh;

        const kodachi::IntAttribute visibleAttr =
                interface.getAttr("visible");
        // Visible unless stated otherwise
        const bool isGeoVisible = visibleAttr.getValue(true, false);
        bool visibleChildFound = false;

        if (isMesh) {
            // This location should delete itself if it is marked as invisible.
            // However, it is only safe to do so if its children are all
            // invisible.  If one child is visible, then this location is
            // technically also visible.  It is safe to delete invisible
            // children though.
            const auto potentialChildrenSamples =
                    interface.getPotentialChildren().getSamples();

            if (potentialChildrenSamples.isValid()) {
                for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                    interface.prefetch(childName);
                }

                for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                    const kodachi::IntAttribute childVisibleAttr =
                            interface.getAttr("visible", childName);

                    if (childVisibleAttr.isValid()) {
                        const bool isChildVisible = childVisibleAttr.getValue();

                        if (isChildVisible) {
                            // explicit visible child found.  Remember this so
                            // we don't delete the parent mesh.
                            visibleChildFound = true;
                        } else {
                            // Child faceset is explicitly invisible. Delete it.
                            interface.deleteChild(childName);
                        }
                    } else {
                        // The faceset doesnt have the visible attr.  Delete it
                        // if the parent isn't visible
                        if (!isGeoVisible) {
                            interface.deleteChild(childName);
                        }
                    }
                }
            }
        }

        // Only delete this invisible geo if there aren't any children that are
        // explicitly marked as visible.
        if (!isGeoVisible && !visibleChildFound) {
            interface.deleteSelf();
        }
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(MoonrayPruneInvisibleMeshOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayPruneInvisibleMeshOp, "MoonrayPruneInvisibleMesh", 0, 1);
}

