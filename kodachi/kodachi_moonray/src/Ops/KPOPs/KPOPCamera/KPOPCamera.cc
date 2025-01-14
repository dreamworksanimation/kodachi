// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/InterpolatingGroupBuilder.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/logging/KodachiLogging.h>

#include <unordered_map>

namespace {

KdLogSetup("KPOPCamera");

constexpr double kDegToRad = M_PI / 180.0;

enum Projection {PERSPECTIVE=0, ORTHO, DOME, BAKE, SPHERICAL, NUM_PROJECTIONS};

static const char* const projectionNames[NUM_PROJECTIONS][2] = {
    { "perspective"  , "PerspectiveCamera"  },
    { "orthographic" , "OrthographicCamera" },
    { "domeMaster3D" , "DomeMaster3DCamera"    },
    { "bake"         , "BakeCamera"    },
    { "spherical"    , "SphericalCamera"    }
};

double getDouble(const kodachi::GroupAttribute& group, const char* name, double dflt)
{
    return kodachi::DoubleAttribute(group.getChildByName(name)).getValue(dflt, false);
}

// kodachi uses FOV, Moonray cameras use Focal length
inline double
fovToFocal(const double angleOfView, const double filmWidth)
{
    return filmWidth / (2.0 * std::tan((kDegToRad * angleOfView) * 0.5));
}

// Convert Katana camera geometry to Moonray camera settings, add zoom-out
// and film back offset to convert from aperture to viewport of image format
// This works for PerpectiveCamera and OrthographicCamera
void cookCamera(kodachi::OpCookInterface& interface,
                kodachi::InterpolatingGroupBuilder& attrsGb,
                bool ortho)
{
    const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");

    const double left = getDouble(geometryAttr, "left", -1);
    const double bottom = getDouble(geometryAttr, "bottom", -1);
    const double right = getDouble(geometryAttr, "right", +1);
    const double top = getDouble(geometryAttr, "top", +1);

    // compute width and center in camera units
    double scale;
    double filmWidth;
    if (ortho) {
        filmWidth = getDouble(geometryAttr, "orthographicWidth", 30);
        scale = filmWidth / (right - left);
    } else {
        scale = 24.0 / 2; // mm per screen unit
        // ani()["a_film_width_aperture"] could be used instead of 24.0
        filmWidth = scale * (right - left);
    }
    double filmOffsetX = scale * (left + right) / 2;
    double filmOffsetY = scale * (top + bottom) / 2;

    // KPOPSceneVariables sets aperture_window to the format's viewport so that exr
    // files have displayWindow set to the format's viewport. Convert the camera from
    // the format's aperture to the viewport as aperture. If aperture_window was
    // preset (by usd_render for instance) then don't do this adjustment and assume
    // camera is correct.
    if (not interface.getAttr("moonrayGlobalStatements.aperture_window", "/root").isValid()) {

        // Recover format's viewport
        int vpx, vpy, vpw, vph;
        kodachi::IntAttribute screenWindowOffsetAttr(
            interface.getAttr("moonrayGlobalStatements.screen_window_offset", "/root"));
        if (screenWindowOffsetAttr.isValid()) {
            auto&& a(screenWindowOffsetAttr.getNearestSample(0.f));
            vpx = a[0];
            vpy = a[1];
        } else {
            vpx = vpy = 0;
        }
        kodachi::IntAttribute xyResAttr(interface.getAttr("renderSettings.xyRes", "/root"));
        auto&& xyRes(xyResAttr.getNearestSample(0.f));
        vpw = xyRes[0];
        vph = xyRes[1];

        // Recover format's aperture
        int apx, apy, apw, aph;
        kodachi::IntAttribute formatApertureAttr(
            interface.getAttr("moonrayGlobalStatements.format_aperture_window", "/root"));
        if (formatApertureAttr.isValid()) {
            auto&& a(formatApertureAttr.getNearestSample(0.f));
            apx = a[0];
            apy = a[1];
            apw = a[2]-a[0];
            aph = a[3]-a[1];
        } else { // guess that aperture is centered with lower-left corner at 0,0
            apx = 0;
            apy = 0;
            apw = vpw + 2 * vpx;
            aph = vph + 2 * vpy;
        }

        const double s = filmWidth / apw; // mm per pixel
        filmWidth = s * vpw;
        filmOffsetX += s * (vpx - apx + (vpw - apw) * 0.5);
        filmOffsetY += s * (vpy - apy + (vph - aph) * 0.5);
    }

    const kodachi::DoubleAttribute pixelAspectRatioAttr =
        interface.getAttr("moonrayGlobalStatements.pixel_aspect_ratio", "/root");
    float pixelAspectRatio = pixelAspectRatioAttr.getValue(1.0, false);

    attrsGb
        .set("horizontal_film_offset", kodachi::FloatAttribute(filmOffsetX))
        .set("vertical_film_offset", kodachi::FloatAttribute(filmOffsetY))
        .set("film_width_aperture", kodachi::FloatAttribute(filmWidth))
        .set("pixel_aspect_ratio", kodachi::FloatAttribute(pixelAspectRatio));

    const kodachi::DoubleAttribute coiAttr = geometryAttr.getChildByName("centerOfInterest");
    if (coiAttr.isValid())
        attrsGb.set("dof_focus_distance", kodachi::FloatAttribute(coiAttr.getValue()));

    // convert Katana angle of view to focal length (focal length is blurrable)
    const kodachi::DoubleAttribute fovAttr = geometryAttr.getChildByName("fov");
    if (!ortho && fovAttr.isValid()) {
        kodachi::FloatAttribute focalAttr;
        const auto fovSamples = fovAttr.getSamples();
        if (fovSamples.size() == 1) {
            focalAttr = kodachi::FloatAttribute(fovToFocal(fovSamples[0][0], 2*scale));
        } else {
            const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
            const float shutterClose = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterClose")).getValue();
            double fovShutterOpen, fovShutterClose;
            fovAttr.fillInterpSample(&fovShutterOpen, 1, shutterOpen);
            fovAttr.fillInterpSample(&fovShutterClose, 1, shutterClose);

            std::array<float, 2> sampleTimes { shutterOpen, shutterClose };

            const float focalShutterOpen = fovToFocal(fovShutterOpen, 2*scale);
            const float focalShutterClose = fovToFocal(fovShutterClose, 2*scale);

            std::array<const float*, 2> values{&focalShutterOpen, &focalShutterClose};
            focalAttr = kodachi::FloatAttribute(sampleTimes.data(), sampleTimes.size(),
                                                values.data(), 1, 1);
        }
        attrsGb.setWithoutInterpolation("focal", focalAttr, false);
    }
}

// Converter for DomeMaster3DCamera
void cookDome(kodachi::OpCookInterface& interface,
              kodachi::InterpolatingGroupBuilder& attrsGb)
{
    const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");

    // Compute size of aperture in Katana units where default is 2x2
    const double left = getDouble(geometryAttr, "left", -1);
    const double bottom = getDouble(geometryAttr, "bottom", -1);
    const double right = getDouble(geometryAttr, "right", +1);
    const double top = getDouble(geometryAttr, "top", +1);
    double width = right-left;
    double height = top-bottom;

    // Scale from aperture to viewport. Assumes aperture is centered
    if (not interface.getAttr("moonrayGlobalStatements.aperture_window", "/root").isValid()) {
        kodachi::IntAttribute formatApertureAttr(
            interface.getAttr("moonrayGlobalStatements.format_aperture_window", "/root"));
        if (formatApertureAttr.isValid()) {
            auto&& a(formatApertureAttr.getNearestSample(0.f));
            int apw = a[2]-a[0];
            int aph = a[3]-a[1];
            kodachi::IntAttribute xyResAttr(interface.getAttr("renderSettings.xyRes", "/root"));
            auto&& xyRes(xyResAttr.getNearestSample(0.f));
            int vpw = xyRes[0];
            int vph = xyRes[1];
            width = (width * vpw) / apw;
            height = (height * vph) / aph;
        }
    }

    const double fov = getDouble(geometryAttr, "fov", 60); // animation not supported
    attrsGb.set("FOV_horizontal_angle", kodachi::FloatAttribute(width * fov / 2));
    attrsGb.set("FOV_vertical_angle", kodachi::FloatAttribute(height * fov / 2));
}

class KPOPCamera: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root//*{@type=="rdl2" and @rdl2.meta.kodachiType=="camera"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren)
            interface.stopChildTraversal();
        if (!celInfo.matches)
            return;

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");

        // SceneClass
        Projection projection = PERSPECTIVE;
        const kodachi::StringAttribute projectionAttr =
                geometryAttr.getChildByName("projection");
        if (projectionAttr.isValid()) {
            const std::string& s(projectionAttr.getValue("", false));
            for (unsigned i = 0; ; ++i) {
                if (i >= NUM_PROJECTIONS) {
                    kodachi::ReportNonCriticalError(interface, "Unknown projection: " + s);
                    break;
                }
                if (s == projectionNames[i][0]) {
                    projection = (Projection)i;
                    break;
                }
            }
        }
        {
            const char* sceneClass = projectionNames[projection][1];
            interface.setAttr("rdl2.sceneObject.sceneClass", kodachi::StringAttribute(sceneClass));
            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_", sceneClass);
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName));
        }

        // camera settings
        double near = getDouble(geometryAttr, "near", 1);
        double far = getDouble(geometryAttr, "far", 10000);

        const bool isMbEnabled = kodachi::IntAttribute(
                interface.getAttr("rdl2.meta.mbEnabled")).getValue();
        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
        const float shutterClose = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterClose")).getValue();

        kodachi::InterpolatingGroupBuilder attrsGb(shutterOpen, shutterClose);
        attrsGb
            .setGroupInherit(false)
            .update(interface.getAttr("rdl2.sceneObject.attrs"))
            .set("near", kodachi::FloatAttribute(near))
            .set("far", kodachi::FloatAttribute(far));

        const kodachi::GroupAttribute cameraStatementsAttr =
            kodachi::GroupAttribute(interface.getAttr("moonrayCameraStatements"));
        if (cameraStatementsAttr.isValid()) {
            attrsGb.update(cameraStatementsAttr);
        }

        // projection stuff
        switch (projection) {
        case PERSPECTIVE:
            cookCamera(interface, attrsGb, false);
            break;
        case ORTHO:
            cookCamera(interface, attrsGb, true);
            break;
        case DOME:
            cookDome(interface, attrsGb);
            break;
        default:
            break;
        }

        interface.setAttr("rdl2.sceneObject.attrs", attrsGb.build(), false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Sets attributes in rdl2::Cameras");

        return builder.build();
    }
};

///////////////////////////////////////////////////////////////////////////////////////////

DEFINE_KODACHIOP_PLUGIN(KPOPCamera)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPCamera, "KPOPCamera", 0, 1);
}

