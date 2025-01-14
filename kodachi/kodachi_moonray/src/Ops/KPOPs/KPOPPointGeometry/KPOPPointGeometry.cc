// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/InterpolatingGroupBuilder.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi_moonray/motion_blur_util/MotionBlurUtil.h>

#include <array>

namespace {

using namespace kodachi_moonray;

KdLogSetup("KPOPPointGeometry");

class KPOPPointGeometry : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const std::string kRootLocation("/root");

        if (interface.getInputLocationPath() == kRootLocation) {
            kodachi::GroupBuilder opArgsBuilder;

            // use Rdl geometry procedurals
            {
                const kodachi::IntAttribute useRdlGeometryAttr =
                        interface.getAttr("moonrayGlobalStatements.use_rdl_geometry");
                if (useRdlGeometryAttr.getValue(false, false)) {
                    opArgsBuilder.set("useRdlPoints", kodachi::IntAttribute(true));
                }
            }

            if (opArgsBuilder.isValid()) {
                opArgsBuilder.update(interface.getOpArg(""));
                interface.replaceChildTraversalOp("", opArgsBuilder.build());
            }
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and @rdl2.meta.kodachiType=="pointcloud"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const bool useRdlPointGeometry = kodachi::IntAttribute(
                interface.getOpArg("useRdlPoints")).getValue(false, false);


        // SceneClass and SceneObject name
        if (useRdlPointGeometry) {
            static const kodachi::StringAttribute kPointGeometryAttr("RdlPointGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kPointGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_RdlPointGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        } else {
            static const kodachi::StringAttribute kKodachiPointGeometryAttr("KodachiPointGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kKodachiPointGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_KodachiPointGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        }

        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
        const float shutterClose = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterClose")).getValue();

        kodachi::InterpolatingGroupBuilder pointAttrsGb(shutterOpen, shutterClose);
        pointAttrsGb.setGroupInherit(false);

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
            interface.deleteSelf();
            return;
        }

        const kodachi::FloatAttribute pointListAttr =
                geometryAttr.getChildByName("point.P");

        if (!pointListAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "missing 'point.P' attribute");
            return;
        }

        // If using KodachiPointGeometry, we want to leave large geometry
        // data attributes unmodified to avoid memory spikes.  Mainly
        // vertex_list, velocity_list, and radius_list

        // vertex_list, velocity_list, acceleration_list
        if (useRdlPointGeometry) {
            // Since motion_blur_type can be "BEST", we will resolve the actual
            // type here
            kodachi::StringAttribute motionBlurTypeAttr;

            kodachi::GroupAttribute motionBlurAttrs;

            if (!kodachi::IntAttribute(
                    interface.getAttr("rdl2.meta.mbEnabled")).getValue()) {
                motionBlurAttrs =
                        motion_blur_util::createStaticMotionBlurAttributes(pointListAttr);
            } else {
                const kodachi::Attribute initialMotionBlurTypeAttr =
                        interface.getAttr("moonrayStatements.motion_blur_type");

                const kodachi::FloatAttribute velocityAttr =
                        geometryAttr.getChildByName("point.v");

                const kodachi::GroupAttribute accelerationAttr =
                        geometryAttr.getChildByName("arbitrary.accel");

                if (accelerationAttr.isValid()) {
                    // Prevent acceleration from being added as a PrimitiveAttribute
                    interface.deleteAttr("geometry.arbitrary.accel");
                }

                const float fps = kodachi::FloatAttribute(
                        interface.getAttr("rdl2.meta.fps")).getValue();

                motionBlurAttrs = motion_blur_util::createMotionBlurAttributes(
                        initialMotionBlurTypeAttr,
                        pointListAttr,
                        velocityAttr,
                        accelerationAttr,
                        shutterOpen,
                        shutterClose,
                        fps);
            }

            const kodachi::StringAttribute errorMessageAttr =
                    motionBlurAttrs.getChildByName("errorMessage");

            if (errorMessageAttr.isValid()) {
                interface.setAttr("errorMessage", errorMessageAttr);
                return;
            } else {
                const kodachi::StringAttribute warningMessageAttr =
                        motionBlurAttrs.getChildByName("warningMessage");

                if (warningMessageAttr.isValid()) {
                    interface.setAttr("warningMessage", warningMessageAttr);
                }

                motionBlurTypeAttr = motionBlurAttrs.getChildByName("motionBlurType");

                pointAttrsGb.set("motion_blur_type", motionBlurTypeAttr);
                pointAttrsGb.update(motionBlurAttrs.getChildByName("attrs"));
            }
        } else {
            pointAttrsGb.setWithoutInterpolation("point",
                    geometryAttr.getChildByName("point"), false);

            const kodachi::GroupAttribute accelerationAttr =
                    geometryAttr.getChildByName("arbitrary.accel");

            if (accelerationAttr.isValid()) {
                pointAttrsGb.setWithoutInterpolation("acceleration", accelerationAttr);

                // prevent acceleration from being added as a PrimitiveAttribute
                interface.deleteAttr("geometry.arbitrary.accel");
            }
        }

        // radius
        {
            const kodachi::FloatAttribute widthsAttr =
                    geometryAttr.getChildByName("point.width");

            const kodachi::FloatAttribute constWidthAttr =
                    geometryAttr.getChildByName("constantWidth");

            if (useRdlPointGeometry) {
                // Use constant width if it is set AND size of widths (radius) array
                // does not match the size of positions array.
                const int64_t positionsCount = pointListAttr.getNumberOfTuples();
                const int64_t widthsCount = widthsAttr.getNumberOfValues();

                kodachi::FloatAttribute radiusListAttr;
                if (constWidthAttr.isValid() && positionsCount != widthsCount) {
                    std::vector<float> radiusList(positionsCount, constWidthAttr.getValue() * 0.5f);
                    radiusListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(radiusList));
                } else if (widthsAttr.isValid()) {
                    const auto widths = widthsAttr.getNearestSample(0.f);

                    std::vector<float> radiusList;
                    radiusList.reserve(widths.size());

                    for (const auto width : widths) {
                        radiusList.emplace_back(width * 0.5f);
                    }

                    radiusListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(radiusList));
                } else {
                    KdLogDebug("Error getting width attributes for point. Using default radius of 1.0");
                    kodachi::FloatVector radiusList(pointListAttr.getNumberOfValues() / 3, 1.0f);
                    radiusListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(radiusList));
                }

                pointAttrsGb.set("radius_list", radiusListAttr);
            } else {
                pointAttrsGb.setWithoutInterpolation("width",
                        kodachi::GroupAttribute("constantWidth", constWidthAttr,
                                                "vertexWidth", widthsAttr,
                                                "scaleFactor", kodachi::FloatAttribute(0.5),
                                                false));
            }
        }

        // SceneObject Attrs
        {
            kodachi::GroupBuilder sceneObjectAttrsGb;
            sceneObjectAttrsGb
                .setGroupInherit(false)
                .update(interface.getAttr("rdl2.sceneObject.attrs"));

            const kodachi::GroupAttribute pointAttrs = pointAttrsGb.build();
            if (useRdlPointGeometry) {
                sceneObjectAttrsGb.update(pointAttrs);
            } else {
                interface.setAttr("rdl2.sceneObject.kodachiGeometry", pointAttrs, false);
            }

            interface.setAttr("rdl2.sceneObject.attrs", sceneObjectAttrsGb.build(), false);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Sets attributes on locations that represent a RdlPointGeometry.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPPointGeometry)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPPointGeometry, "KPOPPointGeometry", 0, 1);
}

