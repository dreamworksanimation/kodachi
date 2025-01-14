// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <FnAttribute/FnAttribute.h>
#include <FnPluginSystem/FnPlugin.h>

#include <kodachi/op/XFormUtil.h>
#include <kodachi_moonray/light_util/LightUtil.h>

namespace {

// true if prefix names a parent, gp, etc of s
bool isAncestor(const std::string& prefix, const std::string& s) {
    return prefix.size() < s.size() && s.compare(0, prefix.size(), prefix) == 0 && s[prefix.size()] == '/';
}

// Copy information between a MeshLight and it's source geometry. This is run by
// the gaffer.
class MoonrayMeshLightPostMergeOp: public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        FnAttribute::StringAttribute geomAttr(interface.getOpArg("geometry"));
        std::string geometry = geomAttr.getValue("", false);
        if (geometry.empty()) return;

        FnAttribute::StringAttribute lightAttr(interface.getOpArg("path"));
        std::string light = lightAttr.getValue("", false);
        if (light.empty() || geometry == light) return;

        interface.prefetch(geometry);
        interface.prefetch(light);

        const std::string location = interface.getInputLocationPath();
        if (location == geometry) {
            static const std::string materialPrefix("material.moonrayLightParams.");

            // Does not work for parts, leave the source geometry unchanged
            FnAttribute::StringAttribute partsAttr(interface.getAttr(materialPrefix+"parts", light));
            if (partsAttr.getNumberOfValues()) return;

            static const char* const visibilityAttrs[] = {
                "visible_in_camera",
                "visible_diffuse_reflection",
                "visible_diffuse_transmission",
                "visible_glossy_reflection",
                "visible_glossy_transmission",
                "visible_mirror_reflection",
                "visible_mirror_transmission"
            };
            static const std::string geometryPrefix("moonrayStatements.");
            for (const char* visibilityAttr : visibilityAttrs) {
                bool on;
                if (visibilityAttr == visibilityAttrs[0]) { // camera
                    FnAttribute::StringAttribute attr(
                        interface.getAttr(materialPrefix+visibilityAttr, light));
                    on = (attr.getValue("",false) == "force on");
                } else {
                    FnAttribute::IntAttribute attr(
                        interface.getAttr(materialPrefix+visibilityAttr, light));
                    on = attr.getValue(1, false);
                }
                if (on)
                    interface.setAttr(geometryPrefix + visibilityAttr, FnAttribute::IntAttribute(0));
            }

            // Don't cast shadows from the light:
            // get name for lightList, which has _ instead of / and removes leading /
            size_t n = light.size()-1;
            std::string _light; _light.resize(n);
            for (size_t i = 0; i < n; ++i)
                _light[i] = light[i+1] == '/' ? '_' : light[i+1];
            interface.setAttr("lightList." + _light + ".geoShadowEnable", FnAttribute::IntAttribute(0));

        } else if (location == light) {

            // get global translation of the geometry
            kodachi::DoubleAttribute matrixAttr(
                kodachi::XFormUtil::CalcTransformMatrixAtTime(
                    kodachi::GetGlobalXFormGroup(interface, geometry), 0.0f).first);
            auto&& array(matrixAttr.getNearestSample(0));
            double pivot[3] = {array[12], array[13], array[14]};

            // set the pivot for the light's transform from the geometry origin
            FnAttribute::GroupAttribute oldXform = interface.getAttr("xform");
            FnAttribute::GroupBuilder newXform;
            newXform.set("translate_pivot.translate_pivot",
                         FnAttribute::DoubleAttribute(pivot, 3, 3));
            int n = oldXform.getNumberOfChildren();
            for (int i = 0; i < n; ++i) {
                auto&& name(oldXform.getChildName(i));
                if (name != "translate_pivot" && name != "translate_pivotInverse")
                    newXform.set(name, oldXform.getChildByIndex(i));
            }
            double ipivot[3] = {-pivot[0], -pivot[1], -pivot[2]};
            newXform.set("translate_pivotInverse.translate_pivotInverse",
                         FnAttribute::DoubleAttribute(ipivot, 3, 3));
            interface.setAttr("xform", newXform.build());
        }

        if (not isAncestor(location, geometry) && not isAncestor(location, light))
            interface.stopChildTraversal();
    }

    static FnAttribute::GroupAttribute describe()
    {
        FnKat::FnOpDescription::FnOpDescriptionBuilder builder;
        builder.setSummary("Copy information between geometry and mesh light");
        return builder.build();
    }
};

DEFINE_GEOLIBOP_PLUGIN(MoonrayMeshLightPostMergeOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayMeshLightPostMergeOp, "MoonrayMeshLightPostMerge", 0, 1);
}

