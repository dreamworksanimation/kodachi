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
KdLogSetup("AttributeSetByVolume");

const std::string kImmediateExecutionMode = "immediate";
const std::string kDeferredExecutionMode = "deferred";

class AttributeSetByVolumeOp: public kodachi::GeolibOp {
public:

    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(
                kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute kAttrSetVolumeAttr("attrSet volume");

        KdLogDebug(interface.getInputLocationPath());

        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
        if (typeAttr == kAttrSetVolumeAttr) {
            return;
        }

        const kodachi::StringAttribute attrSetVolumeLocationAttr =
                interface.getOpArg("attrSetVolumeLocation");

        if (!attrSetVolumeLocationAttr.isValid()) {
            return;
        }

        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        if (!celAttr.isValid()) {
            KdLogError("Invalid CEL");
            return;
        }

        kodachi::CookInterfaceUtils::MatchesCELInfo info;
        kodachi::CookInterfaceUtils::matchesCEL(info,
                                                interface,
                                                celAttr);
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

        const std::string attrSetLocation = attrSetVolumeLocationAttr.getValue();

        if (interface.getAttr("bound").isValid()) {
            const kodachi::StringAttribute attributeNameAttr = interface.getOpArg("attributeName");
            if (attributeNameAttr.isValid()) {
                const kodachi::String attributeName = attributeNameAttr.getValue();

                kodachi::internal::Mesh currentMesh;
                kodachi::internal::Mesh attrSetMesh;
                kodachi::internal::Mesh attrSetBoundMesh;

                if (kodachi::internal::GetTransformedBoundAsMesh(interface, currentMesh)
                        && kodachi::internal::GetTransformedMesh(interface, attrSetMesh, attrSetLocation)) {

                    bool boundIntersects = true;
                    bool intersects = false;

                    // If our attr set volume has more than 6 faces, do a bound test first
                    if (attrSetMesh.faceCount() > 6
                            && kodachi::internal::GetTransformedBoundAsMesh(interface, attrSetBoundMesh, attrSetLocation)) {
                        boundIntersects = currentMesh.doesIntersect(attrSetBoundMesh);
                    }

                    if (boundIntersects) {
                        intersects = currentMesh.doesIntersect(attrSetMesh);
                    }

                    KdLogDebug("intersects: " << intersects);
                    KdLogDebug("invert: " << invert);

                    if (intersects != invert) {
                        if (executionModeAttr == kImmediateExecutionMode) {
                            interface.setAttr("volume.metrics." + attributeName,
                                              kodachi::IntAttribute(1));
                        } else {
                            interface.setAttr("volume.metrics." + attributeName + "Deferred",
                                              kodachi::IntAttribute(1));
                            interface.stopChildTraversal();
                        }
                        return;
                    }
                }
            }
        }

        // *** primitive attr set ***
        // Points attr can be used for further setting after bounds testing for curves,
        // points, and instance arrays
        const bool attrSetPrims = kodachi::IntAttribute(
                interface.getOpArg("attrSetPrimitives")).getValue(false,
                false);
        if (attrSetPrims) {
            interface.setAttr("primitiveAttrSet.volumeAttrSet.CEL", celAttr);
            interface.setAttr("primitiveAttrSet.volumeAttrSet.invert", invertAttr);
            interface.setAttr("primitiveAttrSet.volumeAttrSet.xform",
                    kodachi::GetGlobalXFormGroup(interface, attrSetLocation));
            interface.setAttr("primitiveAttrSet.volumeAttrSet.bound",
                    interface.getAttr("bound", attrSetLocation));

            // only store geometry attrs that we need
            const kodachi::GroupAttribute attrSetVolumeGeometry =
                    interface.getAttr("geometry", attrSetLocation);
            kodachi::GroupBuilder attrSetVolumeGeometryGb;
            attrSetVolumeGeometryGb.set("poly", attrSetVolumeGeometry.getChildByName("poly"));
            attrSetVolumeGeometryGb.set("point.P", attrSetVolumeGeometry.getChildByName("point.P"));
            interface.setAttr("primitiveAttrSet.volumeAttrSet.geometry",
                    attrSetVolumeGeometryGb.build());
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(AttributeSetByVolumeOp)

// This Op creates the attr set volume location using a specified volume type. We
// should only use polymesh objects as provided by the PrimitiveCreate resources
// that ship with Katana or at least make sure that the created polymesh uses
// CCW winding order.
class AttributeSetVolumeCreateOp: public kodachi::GeolibOp {
public:
    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute sCube = "cube";
        static const kodachi::StringAttribute sCylinder = "cylinder";
        static const kodachi::StringAttribute sSphere = "sphere";

        KdLogDebug(interface.getInputLocationPath());

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

            interface.setAttr("type", kodachi::StringAttribute("attrSet volume"));
            interface.setAttr("viewer.default.drawOptions.fill", kodachi::StringAttribute("wireframe"));

            static const kodachi::FloatAttribute::value_type color[3] = { 0.7f, 0.15f, 0.15f };
            interface.setAttr("viewer.default.drawOptions.color", kodachi::FloatAttribute(color, 3, 1));
        }

        interface.stopChildTraversal();
    }
};

DEFINE_GEOLIBOP_PLUGIN(AttributeSetVolumeCreateOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(AttributeSetVolumeCreateOp, "AttributeSetVolumeCreateOp", 0, 1);
    REGISTER_PLUGIN(AttributeSetByVolumeOp, "AttributeSetByVolumeOp", 0, 2);
}

