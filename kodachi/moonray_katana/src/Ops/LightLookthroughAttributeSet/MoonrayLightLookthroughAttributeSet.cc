// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <FnAttribute/FnAttribute.h>
#include <FnPluginSystem/FnPlugin.h>

#include <kodachi_moonray/light_util/LightUtil.h>

#include <math.h>

namespace {

using namespace FnAttribute;

// If Foundry implements a system to give the look-through camera an offset, we can
// simply offset the SpotLight backwards and give it exactly the same fov as the
// outer cone angle.
class MoonrayLightLookthroughAttributeSetOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const std::string type = FnKat::GetInputLocationType(interface);
        if (type == "light") {
            const GroupAttribute paramsAttr = kodachi_moonray::light_util::getShaderParams(interface.getAttr("material"));
            if (paramsAttr.isValid()) {
                const std::string shaderName = kodachi_moonray::light_util::getShaderName(interface.getAttr("material"));
                if (shaderName == "SpotLight") {
                    //const float angle = FloatAttribute(paramsAttr.getChildByName("outer_cone_angle")).getValue(60, false);
                    float outerSlope1 = 1.0f, outerSlope2 = 1.0f, innerSlope = 1.0f;
                    kodachi_moonray::light_util::getSpotLightSlopes(paramsAttr, outerSlope1, outerSlope2, innerSlope);
                    const float radius = FloatAttribute(paramsAttr.getChildByName("lens_radius")).getValue(1.0f, false);
                    const float coi = DoubleAttribute(interface.getAttr("geometry.centerOfInterest")).getValue(20.0, false);
                    const float outerRadius = (1 + coi * outerSlope1) * radius;
                    const float newAngle = 180.0f - (atan2f(coi, outerRadius) * 180.0f / M_PI) * 2.0f;
                    interface.setAttr("geometry.fov", DoubleAttribute(newAngle));

                    const float ar = FloatAttribute(paramsAttr.getChildByName("aspect_ratio")).getValue(1, false);
                    if (ar && ar != 1) { // set window to match aspect ratio
                        interface.setAttr("geometry.bottom", DoubleAttribute(-1/ar));
                        interface.setAttr("geometry.top", DoubleAttribute(1/ar));
                    }
                } else {
                    // all other lights have a very wide angle to show lit area
                    interface.setAttr("geometry.fov", DoubleAttribute(90));
                }
                if (shaderName == "DistantLight") {
                    interface.setAttr("geometry.projection", StringAttribute("orthographic"));
                }
            }
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Sets various attributes to properly set up look-through mode for each light type.");
        builder.setHelp("Calculates the intersection between the SpotLight's outer cone angle and the look-through "
                        "camera's fov at the light's center of interest, and sets the look-through camera's fov to "
                        "this value. This gives an accurate look-through for geometry that falls exactly on the "
                        "center of interest, and all other geometry will be slightly off.\n\nSets DistantLights to "
                        "orthographic projection.");
        builder.setNumInputs(0);
        builder.describeOutputAttr(OutputAttrDescription(AttrTypeDescription::kTypeFloatAttribute, "geometry.fov"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayLightLookthroughAttributeSetOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLightLookthroughAttributeSetOp, "MoonrayLightLookthroughAttributeSet", 0, 1);
}

