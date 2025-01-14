// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/XFormUtil.h>
#include <kodachi_moonray/light_util/LightUtil.h>

namespace {

// Duplicate the source geometry for the MeshLight so it can both render and also
// be used for the light. The copy is added as a child of the light and the copy is
// used for the light. If Moonray is fixed to allow a geometry to be used for both
// then this op can be removed.
// Also translate the "map" setting into an ImageMap for the "map_shader" setting
class MoonrayMeshLightSourceCopyOp: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        kodachi::StringAttribute geometryAttr(interface.getOpArg("geometry"));
        if (not geometryAttr.isValid()) {

            // Find mesh light
            static const kodachi::StringAttribute kLight("light");
            if (interface.getAttr("type") != kLight)
                return;
            const kodachi::GroupAttribute material = interface.getAttr("material");
            if (not material.isValid()) return;
            if (kodachi_moonray::light_util::getShaderName(material) != "MeshLight")
                return;

            kodachi::GroupAttribute params(
                kodachi_moonray::light_util::getShaderParams(material));

            geometryAttr = params.getChildByName("geometry");
            const kodachi::string_view geometry(geometryAttr.getValueCStr("", false));
            if (not interface.doesLocationExist(geometry)) {
                kodachi::ReportNonCriticalError(interface, std::string(geometry) + " does not exist");
                // return;
            }

            // Start the copy:
            static const std::string childName("copiedGeometry");
            interface.createChild(childName, FnPlatform::StringView(),
                                  kodachi::GroupAttribute("geometry", geometryAttr, false));

            // Modify shader to use the copy:
            std::string paramsPath(
                "material." + kodachi_moonray::light_util::getShaderParamsPath(material));

            interface.setAttr(
                paramsPath + ".geometry",
                kodachi::StringAttribute(interface.getOutputLocationPath() + '/' + childName));

            // See if the texture is set:
            kodachi::StringAttribute textureAttr(params.getChildByName("texture"));
            if (textureAttr.isValid() && *textureAttr.getValueCStr()) {

                // translate the map attributes to ImageMap ones as much as possible:
                kodachi::GroupBuilder gb;
                gb.set("texture", textureAttr);

                kodachi::FloatAttribute a;
                a = params.getChildByName("saturation");
                if (a.isValid()) {
                    gb.set("saturation_enabled", kodachi::IntAttribute(true));
                    gb.set("saturation", a);
                }
                a = params.getChildByName("contrast");
                if (a.isValid()) {
                    gb.set("contrast_enabled", kodachi::IntAttribute(true));
                    gb.set("contrast", a);
                }
                a = params.getChildByName("gamma");
                if (a.isValid()) {
                    gb.set("gamma_enabled", kodachi::IntAttribute(true));
                    gb.set("gamma_adjust", a);
                }
                a = params.getChildByName("gain");
                if (a.isValid()) {
                    gb.set("gain_offset_enabled", kodachi::IntAttribute(true));
                    gb.set("gain", a);
                }
                a = params.getChildByName("offset");
                if (a.isValid()) {
                    gb.set("gain_offset_enabled", kodachi::IntAttribute(true)); // same as gain
                    gb.set("offset_adjust", a);
                }
                a = params.getChildByName("temperature");
                if (a.isValid()) {
                    gb.set("TME_control_enabled", kodachi::IntAttribute(true));
                    gb.set("TME", a);
                }

                a = params.getChildByName("texture_rotation");
                if (a.isValid()) {
                    gb.set("rotation_angle", a);
                    // light textures can only rotate around origin:
                    float center[2] = {0,0};
                    gb.set("rotation_center", kodachi::FloatAttribute(center, 2, 2));
                }
                a = params.getChildByName("texture_translation");
                if (a.isValid()) {
                    gb.set("offset", a);
                }
                // scale is reps/coverage
                float scale[2] = {1, 1};
                float time = FnGeolibServices::GetCurrentTime(interface);
                a = params.getChildByName("texture_reps_u");
                if (a.isValid()) {
                    a.fillInterpSample(&scale[0], 1, time);
                }
                a = params.getChildByName("texture_reps_v");
                if (a.isValid()) {
                    a.fillInterpSample(&scale[1], 1, time);
                }
                a = params.getChildByName("texture_coverage");
                if (a.isValid()) {
                    float coverage[2];
                    a.fillInterpSample(coverage, 2, time);
                    scale[0] /= coverage[0];
                    scale[1] /= coverage[1];
                }
                if (scale[0] != 1 || scale[1] != 1) {
                    gb.set("scale", kodachi::FloatAttribute(scale, 2, 2));
                }

                kodachi::GroupAttribute attrs(gb.build());

                // generate name for ImageMap that will be shared by any identical ones
                kodachi::StringAttribute mapName(attrs.getHash().str() + "_ImageMap");

                // make a child ImageMap
                kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
                asb.setCEL("//*");
                asb.setAttr("type", kodachi::StringAttribute("rdl2"));
                asb.setAttr("rdl2.sceneObject.sceneClass", kodachi::StringAttribute("ImageMap"));
                asb.setAttr("rdl2.sceneObject.name", mapName);
                asb.setAttr("rdl2.sceneObject.attrs", attrs);
                asb.setAttr("rdl2.sceneObject.disableAliasing", kodachi::IntAttribute(true));
                interface.createChild("mapShader", "AttributeSet", asb.build());

                // make the mesh light shader use it and ignore the texture setting
                interface.setAttr(paramsPath + ".map_shader", mapName);
                interface.deleteAttr(paramsPath + ".texture");
            }

        } else {

            // Do not run on the copyLocationToChild children
            interface.stopChildTraversal(); // does not work
            if (not interface.getInputLocationPath().empty()) return; // works

            // We are on the child, copy all the attributes from the source
            std::string geometry(geometryAttr.getValue("", false));
            const kodachi::GroupAttribute a(interface.getAttr("", geometry));
            for (auto&& x : a) {
                if (x.name != "xform" && x.name != "visible" && x.name != "deferredPrune")
                    interface.setAttr(x.name, x.attribute);
            }

            // modifications to the attributes
            interface.setAttr("xform.origin", kodachi::DoubleAttribute(1));
            kodachi::DoubleAttribute matrixAttr(
                kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                    kodachi::GetGlobalXFormGroup(interface, geometry)).first);
            interface.setAttr("xform.matrix", matrixAttr);
            interface.setAttr("disableLayerAssign", kodachi::IntAttribute(true));
            interface.setAttr("moonrayStatements.sceneBuild.autoInstancing", kodachi::IntAttribute(0));
            // disable the whitelist for arbitrary attrs so primitive attributes can
            // pass through without material assignment
            // TODO: we can look at the map shader network and add a whitelist instead
            // of allowing all attributes
            interface.setAttr("moonrayStatements.arbitraryAttrs.whitelistMode",
                    kodachi::IntAttribute(2));

            // copy all the children of the source
            const kodachi::StringAttribute children(interface.getPotentialChildren(geometry));
            for (const char* child : children.getNearestSample(0))
                interface.copyLocationToChild(child, geometry + '/' + child);

        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Copies the source mesh for MeshLights to a child of the light.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(MoonrayMeshLightSourceCopyOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayMeshLightSourceCopyOp, "MoonrayMeshLightSourceCopy", 0, 1);
}

