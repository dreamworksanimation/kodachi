// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/InterpolatingGroupBuilder.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi_moonray/motion_blur_util/MotionBlurUtil.h>

// Imath
#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathMatrixAlgo.h>

#include <sstream>
#include <memory>
#include <unordered_set>

namespace {

using namespace kodachi_moonray;

KdLogSetup("KPOPCurveGeometry");

enum class CurveWidthRate : kodachi::Int
{
    PER_VERTEX = 0,
    PER_CURVE = 1,
    CONSTANT = 2
};

class KPOPCurveGeometry : public kodachi::Op
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

            // scatter_tag and random_color
            {
                const kodachi::GroupAttribute outputChannelsAttr =
                        interface.getAttr("moonrayGlobalStatements.outputChannels");

                for (const auto attrPair : outputChannelsAttr) {
                    const kodachi::GroupAttribute outputAttr =
                            attrPair.attribute;

                    const kodachi::StringAttribute resultAttr =
                            outputAttr.getChildByName("result");

                    static const kodachi::StringAttribute kResultPrimitiveAttributeAttr("primitive attribute");
                    if (resultAttr == kResultPrimitiveAttributeAttr) {
                        static const kodachi::StringAttribute kScatterTagAttr("scatter_tag");
                        static const kodachi::StringAttribute kRandomColorAttr("random_color");

                        const kodachi::StringAttribute primitiveAttributeAttr =
                                outputAttr.getChildByName("primitive_attribute");

                        if (primitiveAttributeAttr == kScatterTagAttr) {
                            opArgsBuilder.set("requiresScatterTag", kodachi::IntAttribute(1));
                        } else if (primitiveAttributeAttr == kRandomColorAttr) {
                            opArgsBuilder.set("requiresRandomColor", kodachi::IntAttribute(1));
                        }
                    }
                }
            }

            // use Rdl geometry procedurals
            {
                const kodachi::IntAttribute useRdlGeometryAttr =
                        interface.getAttr("moonrayGlobalStatements.use_rdl_geometry");

                if (useRdlGeometryAttr.getValue(false, false)) {
                    opArgsBuilder.set("useRdlCurves", kodachi::IntAttribute(true));
                }
            }

            if (opArgsBuilder.isValid()) {
                opArgsBuilder.update(interface.getOpArg(""));
                interface.replaceChildTraversalOp("", opArgsBuilder.build());
            }
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and @rdl2.meta.kodachiType=="curves"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const bool useRdlCurveGeometry = kodachi::IntAttribute(
                interface.getOpArg("useRdlCurves")).getValue(false, false);

        // SceneClass and SceneObject name
        if (useRdlCurveGeometry) {
            static const kodachi::StringAttribute kRdlCurveGeometryAttr("RdlCurveGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kRdlCurveGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_RdlCurveGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        } else {
            static const kodachi::StringAttribute kKodachiCurveGeometryAttr("KodachiCurveGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kKodachiCurveGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_KodachiCurveGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        }

        const bool isMotionBlurEnabled = kodachi::IntAttribute(
                interface.getAttr("rdl2.meta.mbEnabled")).getValue();

        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
        const float shutterClose = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterClose")).getValue();

        kodachi::InterpolatingGroupBuilder curveAttrsGb(shutterOpen, shutterClose);
        curveAttrsGb.setGroupInherit(false);

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
            interface.deleteSelf();
            return;
        }

        const kodachi::IntAttribute numVerticesAttr =
                geometryAttr.getChildByName("numVertices");

        const kodachi::FloatAttribute pointListAttr =
                geometryAttr.getChildByName("point.P");

        if (numVerticesAttr.getNumberOfValues() == 0) {
            kodachi::ReportWarning(interface, "'geometry.numVertices' attribute missing or empty");
            return;
        }

        if (pointListAttr.getNumberOfValues() == 0) {
            kodachi::ReportWarning(interface, "'geometry.point.P' attribute missing or empty");
            return;
        }

        // curves_vertex_count
        {
            curveAttrsGb.set("curves_vertex_count", numVerticesAttr);
        }

        // If using KodachiCurveGeometry, we want to leave large geometry
        // data attributes unmodified to avoid memory spikes. Mainly
        // vertex_list, velocity_list, and radius_list

        // vertex_list, velocity_list, acceleration_list
        if (useRdlCurveGeometry) {
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

                curveAttrsGb.update(motionBlurAttrs.getChildByName("attrs"));
            }
        } else {
            curveAttrsGb.setWithoutInterpolation("point",
                    geometryAttr.getChildByName("point"), false);

            const kodachi::GroupAttribute accelerationAttr =
                    geometryAttr.getChildByName("arbitrary.accel");

            if (accelerationAttr.isValid()) {
                curveAttrsGb.setWithoutInterpolation("acceleration", accelerationAttr);

                // Prevent acceleration from being added as a PrimitiveAttribute
                interface.deleteAttr("geometry.arbitrary.accel");
            }
        }

        // radius_list
        {
            // Moonray now supports scaling of curve thickness
            // if invert_world_scale is true, scale the CV widths with the inverse of the scaling factor
            // to offset this effect if desired
            const kodachi::IntAttribute invertScale = kodachi::GetGlobalAttr(
                    interface, "curveOperations.invertWorldScale");

            const kodachi::DoubleAttribute matrixAttr = interface.getAttr("xform.matrix");

            const bool invertWorldScale = invertScale.getValue(false, false)
                                            && (matrixAttr.getNumberOfValues() > 0);

            // Since kodachi uses width and Moonray uses radius, we need to
            // scale by a half
            float scaleFactor = 0.5f;
            if (invertWorldScale) {
                scaleFactor *= getInverseScaleFactor(matrixAttr);
            }

            const kodachi::FloatAttribute constantWidthAttr =
                    geometryAttr.getChildByName("constantWidth");
            const kodachi::FloatAttribute vertexWidthsAttr =
                    geometryAttr.getChildByName("point.width");

            if (useRdlCurveGeometry) {
                kodachi::FloatAttribute radiusListAttr;

                if (constantWidthAttr.isValid()) {
                    const float constWidth = constantWidthAttr.getValue();
                    kodachi::FloatVector radiusList(pointListAttr.getNumberOfValues() / 3, constWidth * scaleFactor);
                    radiusListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(radiusList));
                } else if (vertexWidthsAttr.isValid()) {
                    const auto vertexWidths = vertexWidthsAttr.getNearestSample(0.f);
                    kodachi::FloatVector radiusList;
                    radiusList.reserve(vertexWidths.size());
                    for (const auto width : vertexWidths) {
                        radiusList.push_back(width * scaleFactor);
                    }
                    radiusListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(radiusList));
                } else {
                    KdLogDebug("Error getting width attributes for curve. Using default radius of 1.0");
                    kodachi::FloatVector radiusList(pointListAttr.getNumberOfValues() / 3, 1.0f);
                    radiusListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(radiusList));
                }

                curveAttrsGb.set("radius_list", radiusListAttr);
            } else {
                curveAttrsGb.setWithoutInterpolation("width",
                        kodachi::GroupAttribute("constantWidth", constantWidthAttr,
                                                "vertexWidth", vertexWidthsAttr,
                                                "scaleFactor", kodachi::FloatAttribute(scaleFactor),
                                                false));
            }
        }

        // curve_type
        {
            const kodachi::IntAttribute basisAttr = geometryAttr.getChildByName("basis");
            if (!basisAttr.isValid()) {
                // basisCurves read from usd does not set 'basis' when curve type is linear
                KdLogDebug("Missing 'basis', assuming linear.")
                curveAttrsGb.set("curve_type", kodachi::IntAttribute(0));
            } else {
                curveAttrsGb.set("curve_type", basisAttr);
            }
        }

        // uv_list
        {
            const kodachi::FloatAttribute uvCoordinatesAttr(
                                geometryAttr.getChildByName("arbitrary.st.value"));
            if (uvCoordinatesAttr.isValid()) {
                curveAttrsGb.set("uv_list", uvCoordinatesAttr);

                // Prevent UVs from being added as a PrimitiveAttribute
                interface.deleteAttr("geometry.arbitrary.st");
            }
        }

        const kodachi::GroupAttribute curveAttrs = curveAttrsGb.build();

        // auto instancing attrs
        {
            const kodachi::IntAttribute autoInstancingEnabledAttr =
                    interface.getAttr("rdl2.meta.autoInstancing.enabled");

            if (autoInstancingEnabledAttr.isValid()) {
                kodachi::GroupBuilder autoInstancingAttrsGb;
                autoInstancingAttrsGb
                    .setGroupInherit(false)
                    .update(interface.getAttr("rdl2.meta.autoInstancing.attrs"))
                    .update(curveAttrs);

                interface.setAttr("rdl2.meta.autoInstancing.attrs",
                                  autoInstancingAttrsGb.build(), false);
            }
        }

        // SceneObject Attrs
        {
            kodachi::GroupBuilder sceneObjectAttrsGb;
            sceneObjectAttrsGb
                .setGroupInherit(false)
                .update(interface.getAttr("rdl2.sceneObject.attrs"));

            if (useRdlCurveGeometry) {
                sceneObjectAttrsGb.update(curveAttrs);
            } else {
                interface.setAttr("rdl2.sceneObject.kodachiGeometry", curveAttrs, false);
            }

            interface.setAttr("rdl2.sceneObject.attrs", sceneObjectAttrsGb.build(), false);
        }

        // scatter_tag and random_color
        {
            const kodachi::IntAttribute requiresScatterTagAttr =
                    interface.getOpArg("requiresScatterTag");

            const kodachi::IntAttribute requiresRandomColorAttr =
                    interface.getOpArg("requiresRandomColor");

            if (requiresScatterTagAttr.getValue(false, false)) {
                interface.setAttr("geometry.arbitrary.requiresScatterTag",
                        requiresScatterTagAttr);
            }

            if (requiresRandomColorAttr.getValue(false, false)) {
                interface.setAttr("geometry.arbitrary.requiresRandomColor",
                        requiresRandomColorAttr);
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Sets attributes specific to the CurveGeometry Procedural");

        return builder.build();
    }

private:
    inline static float
    getInverseScaleFactor(const kodachi::DoubleAttribute& matrixAttr)
    {
        float scaleFactor = 1.0f;
        const auto matrixSamples = matrixAttr.getSamples();

        Imath::M44d xform;
        setXformMatrix(xform, matrixSamples.front().data());

        Imath::V3d scale;
        Imath::extractScaling(xform, scale);
        // apply the inverse
        // currently only supporting uniform scaling so take the average of the x,y,z scales
        scaleFactor = 1.0f / ((static_cast<float>(scale.x) +
                static_cast<float>(scale.y) +
                static_cast<float>(scale.z))  / 3.0f);
        return scaleFactor;
    }

    inline static void
    setXformMatrix(Imath::M44d& mat, const double* arr)
    {
        if (arr) {
            mat[0][0] = arr[0];
            mat[0][1] = arr[1];
            mat[0][2] = arr[2];
            mat[0][3] = arr[3];
            mat[1][0] = arr[4];
            mat[1][1] = arr[5];
            mat[1][2] = arr[6];
            mat[1][3] = arr[7];
            mat[2][0] = arr[8];
            mat[2][1] = arr[9];
            mat[2][2] = arr[10];
            mat[2][3] = arr[11];
            mat[3][0] = arr[12];
            mat[3][1] = arr[13];
            mat[3][2] = arr[14];
            mat[3][3] = arr[15];
        }
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPCurveGeometry)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPCurveGeometry, "KPOPCurveGeometry", 0, 1);
}

