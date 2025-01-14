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

// kodachi
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/Op.h>

namespace {
KdLogSetup("PruneByVolume");

const std::string kImmediateExecutionMode = "immediate";
const std::string kDeferredExecutionMode = "deferred";
const std::string kCreateVolumeMode = "create volume";
const std::string kUseExistingMode = "use existing";

class PruneByVolumeOp: public kodachi::GeolibOp {
public:

    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        const kodachi::StringAttribute modeAttr = interface.getOpArg("mode");
        if (!modeAttr.isValid()) {
            return;
        }
        bool createVolume = (modeAttr.getValueCStr() == kCreateVolumeMode);

        static const kodachi::StringAttribute kPruneVolumeAttr("prune volume");

        KdLogDebug(interface.getInputLocationPath());

        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
        if (typeAttr == kPruneVolumeAttr) {
            return;
        }

        const kodachi::StringAttribute pruneVolumeLocationAttr = createVolume ?
                        interface.getOpArg("pruneVolumeLocation") : interface.getOpArg("pruneVolumePaths");
        if (!pruneVolumeLocationAttr.isValid()) {
            return;
        }

        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        if (!celAttr.isValid()) {
            KdLogError("Invalid CEL");
            return;
        }

        kodachi::CookInterfaceUtils::MatchesCELInfo info;
        kodachi::CookInterfaceUtils::matchesCEL(info, interface, celAttr);

        if (!info.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!info.matches) {
            return;
        }

        const kodachi::StringAttribute executionModeAttr = interface.getOpArg("executionMode");
        if (!executionModeAttr.isValid()) {
            return;
        }

        const kodachi::IntAttribute invertAttr = interface.getOpArg("invert");
        const bool invert = invertAttr.getValue(false, false);

        const auto nearestSample = pruneVolumeLocationAttr.getNearestSample(0);
        std::string pruneLocation = createVolume ? pruneVolumeLocationAttr.getValue() : "";

        if (interface.getAttr("bound").isValid()) {
            kodachi::internal::Mesh currentMesh;
            kodachi::internal::Mesh pruneMesh;
            kodachi::internal::Mesh pruneBoundMesh;

            int numVol = createVolume ? 1 : nearestSample.size();
            bool intersectsAny = false;

            // loop through prune volumes
            for (int i = 0; i < numVol; ++i) {
                if (!createVolume) {
                    pruneLocation = nearestSample[i];
                }
                if (pruneLocation.empty())
                    continue;

                pruneMesh.points.clear();
                pruneMesh.faceIndices = kodachi::IntAttribute::array_type();
                pruneMesh.verts = kodachi::IntAttribute::array_type();
                pruneBoundMesh.points.clear();
                pruneBoundMesh.faceIndices = kodachi::IntAttribute::array_type();
                pruneBoundMesh.verts = kodachi::IntAttribute::array_type();

                if (kodachi::internal::GetTransformedBoundAsMesh(interface, currentMesh)
                        && kodachi::internal::GetTransformedMesh(interface, pruneMesh, pruneLocation)) {

                    bool boundIntersects = true;
                    bool intersects = false;

                    // If our prune volume has more than 6 faces, do a bound test first
                    if (pruneMesh.faceCount() > 6 &&
                            kodachi::internal::GetTransformedBoundAsMesh(interface, pruneBoundMesh, pruneLocation)) {
                        boundIntersects = currentMesh.doesIntersect(pruneBoundMesh);
                    }

                    if (boundIntersects) {
                        intersects = currentMesh.doesIntersect(pruneMesh);
                    }

                    KdLogDebug("intersects: " << intersects);
                    KdLogDebug("invert: " << invert);

                    intersectsAny |= intersects;
                }
            }

            if (intersectsAny != invert) {

                if (executionModeAttr == kImmediateExecutionMode) {
                    KdLogDebug("deleting self.");
                    interface.deleteSelf();
                } else /* (executionMode == kDeferredExecutionMode) */{
                    interface.setAttr("deferredPrune", kodachi::IntAttribute(1));
                    interface.stopChildTraversal();
                }
                // do not set primitive pruning since we are pruned here already
                return;
            }
        }

        // *** primitive pruning ***
        // Points attr can be used for further pruning after bounds testing for curves,
        // points, and instance arrays
        bool prunePrims = kodachi::IntAttribute(
                interface.getOpArg("prune_primitives")).getValue(false, false);
        if (prunePrims) {
            interface.setAttr("primitivePrune.volumePrune.CEL", celAttr);
            interface.setAttr("primitivePrune.volumePrune.invert", invertAttr);
            interface.setAttr("primitivePrune.volumePrune.xform",
                    kodachi::GetGlobalXFormGroup(interface, pruneLocation));
            interface.setAttr("primitivePrune.volumePrune.bound",
                    interface.getAttr("bound", pruneLocation));

            // only store geometry attrs that we need
            const kodachi::GroupAttribute pruneVolumeGeometry =
                    interface.getAttr("geometry", pruneLocation);
            kodachi::GroupBuilder pruneVolumeGeometryGb;
            pruneVolumeGeometryGb.set("poly", pruneVolumeGeometry.getChildByName("poly"));
            pruneVolumeGeometryGb.set("point.P", pruneVolumeGeometry.getChildByName("point.P"));
            interface.setAttr("primitivePrune.volumePrune.geometry",
                    pruneVolumeGeometryGb.build());
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(PruneByVolumeOp)

// This Op creates the prune volume location using a specified volume type. We
// should only use polymesh objects as provided by the PrimitiveCreate resources
// that ship with Katana or at least make sure that the created polymesh uses
// CCW winding order.
class PruneVolumeSingleCreateOp: public kodachi::GeolibOp {
public:
    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(
                kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute sCube = "cube";
        static const kodachi::StringAttribute sCylinder = "cylinder";
        static const kodachi::StringAttribute sSphere = "sphere";

        const kodachi::StringAttribute volumeTypeAttr = interface.getOpArg("volumeType");
        if (volumeTypeAttr.isValid()) {
            kodachi::string_view volumeName = volumeTypeAttr.getValueCStr();

            if (volumeTypeAttr == sCylinder) {
                volumeName = "poly_cylinder";
            } else if (volumeTypeAttr == sSphere) {
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
            interface.setAttr("viewer.default.drawOptions.fill",kodachi::StringAttribute("wireframe"));

            static const kodachi::FloatAttribute::value_type color[3] = { 0.7f, 0.15f, 0.15f };
            interface.setAttr("viewer.default.drawOptions.color", kodachi::FloatAttribute(color, 3, 1));
        }

        interface.stopChildTraversal();
    }
};

DEFINE_GEOLIBOP_PLUGIN(PruneVolumeSingleCreateOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(PruneVolumeSingleCreateOp, "PruneVolumeSingleCreateOp", 0, 1);
    REGISTER_PLUGIN(PruneByVolumeOp, "PruneByVolumeOp", 0, 2);
}

