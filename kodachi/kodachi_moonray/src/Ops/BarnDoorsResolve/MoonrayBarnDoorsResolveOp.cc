// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi_moonray/light_util/LightUtil.h>

#include <algorithm>

namespace {

KdLogSetup("MoonrayBarnDoorResolverOps");

class MoonrayBarnDoorsResolveOp : public kodachi::Op
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute kLight("light");
        static const kodachi::StringAttribute kPolymesh("polymesh");

        // Check for spot light and find its parameters group attribute
        if (interface.getAttr("type") != kLight) {
            return;
        }
        interface.stopChildTraversal();

        bool isValidLight;
        const kodachi::GroupAttribute lightMaterialAttr = interface.getAttr("material");
        if (lightMaterialAttr.isValid()) {
            const std::string type = kodachi_moonray::light_util::getShaderName(lightMaterialAttr);
            isValidLight = type == "SpotLight" || type == "DiskLight" || type == "RectLight";
        }

        if (!isValidLight) {
            return;
        }

        // Check all children for barn door light filters
        const kodachi::StringAttribute children = interface.getPotentialChildren();
        for (const kodachi::string_view child : children.getNearestSample(0)) {
            if (kodachi::StringAttribute(interface.getAttr("type", child)) != "light filter") {
                continue;
            }

            bool isBarnDoors = false;
            const auto materialAttr = kodachi::GroupAttribute(interface.getAttr("material", child));
            if (materialAttr.isValid()) {
                isBarnDoors = kodachi_moonray::light_util::getShaderName(materialAttr, "moonrayLightfilter") == "BarnDoorsLightFilter";
            }

            if (!isBarnDoors) {
                continue;
            }

            std::string paramsPath = "material." + kodachi_moonray::light_util::getShaderParamsPath(materialAttr, "moonrayLightfilter");
            kodachi::GroupAttribute paramsAttr = kodachi::ThreadSafeCookDaps(interface, paramsPath, child.data()).getChildByName(paramsPath);
            paramsPath = "material." + kodachi_moonray::light_util::getShaderParamsPath(lightMaterialAttr);
            kodachi::GroupAttribute lightParamsAttr = kodachi::ThreadSafeCookDaps(interface, paramsPath).getChildByName(paramsPath);

            // Create blocker geometry
            kodachi::op_args_builder::AttributeSetOpArgsBuilder builder;

            builder.setCEL(kodachi::StringAttribute("//*"));
            builder.setAttr("type", kPolymesh);
            builder.setAttr("attributeEditor.exclusiveTo", interface.getAttr("attributeEditor.exclusiveTo", child));

            static const int kNumQuads = 4;
            std::array<float, 24> P;
            std::array<int, kNumQuads * 4> vertexList;
            kodachi_moonray::light_util::populateBarnDoorBuffers(lightParamsAttr, paramsAttr, P.data(), vertexList.data());
            builder.setAttr("geometry.point.P", kodachi::FloatAttribute(P.data(), P.size(), 3));
            builder.setAttr("geometry.poly.vertexList", kodachi::IntAttribute(vertexList.data(), vertexList.size(), 1));

            std::array<int, kNumQuads + 1> startIndex;
            for (int i = 0; i < startIndex.size(); ++i) {
                startIndex[i] = i * 4;
            }
            builder.setAttr("geometry.poly.startIndex", kodachi::IntAttribute(startIndex.data(), startIndex.size(), 1));

            // TODO: calculate actual bounds. does this actually do anything useful?
            std::array<double, 6> bound = { -0.5f, 0.5f, 0.0f, 0.0f, -0.5f, 0.5f };
            builder.setAttr("bound", kodachi::DoubleAttribute(bound.data(), bound.size(), 1));

            // set transform as identity transform
            std::array<double, 3> translate = { 0.0, 0.0, 0.0 };
            std::array<double, 4> rotateX = { 0.0, 1.0, 0.0, 0.0 };
            std::array<double, 4> rotateY = { 0.0, 0.0, 1.0, 0.0 };
            std::array<double, 4> rotateZ = { 0.0, 0.0, 0.0, 1.0 };
            std::array<double, 3> scale = { 1.0, 1.0, 1.0 };

            builder.setAttr("xform.interactive.translate", kodachi::DoubleAttribute(translate.data(), 3, 3));
            builder.setAttr("xform.interactive.rotateZ", kodachi::DoubleAttribute(rotateZ.data(), 4, 4));
            builder.setAttr("xform.interactive.rotateY", kodachi::DoubleAttribute(rotateY.data(), 4, 4));
            builder.setAttr("xform.interactive.rotateX", kodachi::DoubleAttribute(rotateX.data(), 4, 4));
            builder.setAttr("xform.interactive.scale", kodachi::DoubleAttribute(scale.data(), 3, 3));

            // Create black emptiness material of the void
            builder.setAttr("material.moonrayMaterialShader", kodachi::StringAttribute("DwaBaseMaterial"), "", false);
            builder.setAttr("material.moonrayMaterialParams.show_diffuse", kodachi::IntAttribute(0), "", false);
            builder.setAttr("material.moonrayMaterialParams.show_emission", kodachi::IntAttribute(0), "", false);
            builder.setAttr("material.moonrayMaterialParams.show_specular", kodachi::IntAttribute(0), "", false);
            builder.setAttr("material.moonrayMaterialParams.show_transmission", kodachi::IntAttribute(0), "", false);

            // visibility flags:
            // all must be off except shadow in order to affect only the light
            // that is supposed to be affected. If the light filter is muted
            // then also disable shadow visibility
            bool muted = false;
            {
                static const kodachi::StringAttribute kMuteEmptyAttr("muteEmpty");

                const kodachi::StringAttribute muteStateAttr =
                        interface.getAttr("info.light.muteState", child);
                if (muteStateAttr != kMuteEmptyAttr) {
                    muted = true;
                }
            }

            builder.setAttr("moonrayStatements.visible_in_camera", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_shadow", kodachi::IntAttribute(!muted));
            builder.setAttr("moonrayStatements.visible_diffuse_reflection", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_diffuse_transmission", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_glossy_reflection", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_glossy_transmission", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_mirror_reflection", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_mirror_transmission", kodachi::IntAttribute(0));
            builder.setAttr("moonrayStatements.visible_volume", kodachi::IntAttribute(0));

            const std::string location = interface.getInputLocationPath();
            // Set up shadow linking and disable light linking
            std::string lightName = location.substr(1);
            std::replace(lightName.begin(), lightName.end(), '/', '_');
            lightName = "lightList." + lightName;
            builder.setAttr(lightName + ".path", kodachi::StringAttribute(location));
            builder.setAttr(lightName + ".enable", kodachi::IntAttribute(1));
            builder.setAttr(lightName + ".geoShadowEnable", kodachi::IntAttribute(1));

            // Set the blockerGeometry attr for the shadow link resolver
            builder.setAttr("isBlockerGeometry", kodachi::IntAttribute(1));

            interface.createChild(kodachi::concat(child, "_BlockerGeometry"), "AttributeSet", builder.build());
            interface.deleteChild(child);
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Create barn door geometry from light filter location.");
        builder.setHelp("Finds all MoonrayBarnDoorLightFilter locations, deletes them, and "
                        "creates new polymesh locations for each. The polymeshes act as "
                        "light blockers to mimic barndoors with shadow linking set up. "
                        "The new location will be named %lightfilter%_BlockerGeometry");
        builder.setNumInputs(0);

        return builder.build();
    }
};

class MoonrayBarnDoorsShadowLinkResolveOp : public kodachi::Op
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        // Check if this is barn door geometry
        const kodachi::IntAttribute isBlockerGeometry =
                interface.getAttr("isBlockerGeometry");
        if (!isBlockerGeometry.isValid()) {
            return;
        }

        // Check if there's a localized light list
        kodachi::GroupAttribute lightList = interface.getAttr("lightList");

        if (!lightList.isValid()) {
            return;
        }

        kodachi::GroupBuilder shadowLinkedLightListGb;
        for (int64_t i = 0; i < lightList.getNumberOfChildren(); ++i) {
            const std::string childName = lightList.getChildName(i);
            const FnAttribute::Attribute childAttr = lightList.getChildByIndex(i);

            const kodachi::GroupAttribute childGroupAttr =
                    kodachi::GroupAttribute(childAttr);

            // Copy over the current state of the lightlist
            shadowLinkedLightListGb.set(childName, childGroupAttr);

            const kodachi::Attribute geoShadowEnableAttr =
                    childGroupAttr.getChildByName("geoShadowEnable");
            if (!geoShadowEnableAttr.isValid()) {
                // This light in the light list is not the one that the barndoor
                // filter is attached to.  Explicitly turn off shadow linking
                shadowLinkedLightListGb.set(childName + ".geoShadowEnable", kodachi::IntAttribute(0));
            }
        }

        interface.deleteAttr("isBlockerGeometry");

        // Set the new shadow linked lightList
        interface.deleteAttr("lightList");
        interface.setAttr("lightList", shadowLinkedLightListGb.build());

        interface.stopChildTraversal();
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Set up the appropriate shadow set for barn door geometry");
        builder.setHelp("Finds all blocker geometry for barndoors and sets the "
                        "correct shadow linking related attributes for the lights "
                        "that the barndoors aren't attached to");
        builder.setNumInputs(0);

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayBarnDoorsResolveOp)
DEFINE_GEOLIBOP_PLUGIN(MoonrayBarnDoorsShadowLinkResolveOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayBarnDoorsResolveOp, "MoonrayBarnDoorsResolve", 0, 1);
    REGISTER_PLUGIN(MoonrayBarnDoorsShadowLinkResolveOp, "MoonrayBarnDoorsShadowLinkResolve", 0, 1);
}

