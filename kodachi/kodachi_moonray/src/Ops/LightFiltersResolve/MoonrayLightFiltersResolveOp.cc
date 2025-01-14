// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>
#include <kodachi_moonray/light_util/LightUtil.h>
#include <kodachi/attribute/Attribute.h>

namespace {

const std::string kOpSummary("Populates light filter attributes for each light");
const std::string kOpHelp("Gathers all light filter locations assigned to a "
                          "and adds them to the appropriate attribute in the "
                          "light's material's connections. If a light has "
                          "multiple light filters, a LightFilterArray is "
                          "created first and the filters are connected to that.");


class MoonrayLightFiltersResolveOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute kLightAttr("light");
        static const kodachi::StringAttribute kLightFilterAttr("light filter");
        static const kodachi::StringAttribute kLightFilterReferenceAttr("light filter reference");
        static const kodachi::StringAttribute kLightFilterArrayAttr("LightFilterArray");

        // MoonrayResolveLightFilterReferences should be called before this op,
        // or the behavior for references will be undefined.
        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
        if (typeAttr != kLightAttr) {
            // Since all references should be resolved, we can delete any
            // light filters that aren't a child of a light. We'll delete
            // light filters that are children later.
            if (typeAttr == kLightFilterAttr) {
                interface.deleteSelf();
            }
            return;
        }

        // Don't run this op on light filters, just the lights themselves
        interface.stopChildTraversal();

        const std::string inputLocation = interface.getInputLocationPath();

        const FnAttribute::StringAttribute children =
                interface.getPotentialChildren(inputLocation);

        std::vector<kodachi::string_view> childFilters;
        for (const kodachi::string_view child : children.getNearestSample(0)) {
            interface.prefetch(child);
            const kodachi::StringAttribute typeAttr = interface.getAttr("type", child);
            if (typeAttr == kLightFilterAttr || typeAttr == kLightFilterReferenceAttr) {
                // skip barn doors just in case they aren't deleted
                FnAttribute::GroupAttribute materialAttr = interface.getAttr("material", child);
                if (kodachi_moonray::light_util::getShaderName(materialAttr, "moonrayLightfilter") == "BarnDoorsLightFilter") {
                    continue;
                }

                // Light filters don't have an on/off state like lights, so just
                // skip them if they are muted
                FnAttribute::StringAttribute mutedAttr = interface.getAttr("info.light.muteState",
                                                                           child);
                if (!mutedAttr.isValid() || mutedAttr == "muteEmpty") {
                    childFilters.push_back(child);
                } else {
                    interface.deleteChild(child);
                }
            }
        }

        const int numFilters = childFilters.size();
        if (numFilters == 0) {
            return;
        }

        const FnAttribute::StringAttribute lightNameAttr =
                interface.getAttr("material.terminals.moonrayLight");

        if (lightNameAttr.isValid()) {
            const std::string lightMaterialName = lightNameAttr.getValue();

            // Start with the light's current material, and then add to it
            FnAttribute::GroupBuilder materialBuilder;
            materialBuilder.deepUpdate(interface.getAttr("material"));
            // If there's one light filter, then add directly to the "light_filters" attr
            if (numFilters == 1) {
                const FnAttribute::StringAttribute filterNameAttr =
                        interface.getAttr("material.terminals.moonrayLightfilter",
                                          childFilters.front());
                if (filterNameAttr.isValid()) {
                    const std::string filterMaterialName = filterNameAttr.getValue();
                    materialBuilder.set("nodes." + filterMaterialName,
                                        interface.getAttr("material.nodes." + filterMaterialName, childFilters.front()));
                    materialBuilder.set("nodes." + lightMaterialName + ".connections.light_filters",
                                        FnAttribute::StringAttribute("out@" + filterMaterialName));
                }
                interface.deleteChild(childFilters.front());
            } else {
                static const std::string sLightFilterArrayName = "autogenerated_LightFilterArray";
                // If there's more than 1 light filter, first create the
                // LightFilterArray and then populate its connections. The
                // light's material will connect to the LightFilterArray.
                FnAttribute::GroupBuilder arrayBuilder;
                arrayBuilder.set("name", FnAttribute::StringAttribute(sLightFilterArrayName));
                arrayBuilder.set("srcName", FnAttribute::StringAttribute(sLightFilterArrayName));
                arrayBuilder.set("target", FnAttribute::StringAttribute("moonray"));
                arrayBuilder.set("type", kLightFilterArrayAttr);
                for (size_t i = 0; i < childFilters.size(); ++i) {
                    const kodachi::string_view& location = childFilters[i];
                    const FnAttribute::StringAttribute filterNameAttr =
                            interface.getAttr("material.terminals.moonrayLightfilter", location);
                    if (filterNameAttr.isValid()) {
                        const std::string filterMaterialName = filterNameAttr.getValue();
                        materialBuilder.set("nodes." + filterMaterialName,
                                            interface.getAttr("material.nodes." + filterMaterialName, location));
                        arrayBuilder.set("connections.i" + std::to_string(i),
                                         FnAttribute::StringAttribute("out@" + filterMaterialName));
                    }
                    interface.deleteChild(location);
                }
                materialBuilder.set("nodes." + sLightFilterArrayName, arrayBuilder.build());
                materialBuilder.set("nodes." + lightMaterialName + ".connections.light_filters",
                                    FnAttribute::StringAttribute("out@" + sLightFilterArrayName));
            }

            interface.setAttr("material", materialBuilder.build());
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary(kOpSummary);
        builder.setHelp(kOpHelp);
        builder.setNumInputs(0);

        builder.describeOutputAttr(
                OutputAttrDescription(AttrTypeDescription::kTypeGroupAttribute,
                                      "light_filters"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayLightFiltersResolveOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLightFiltersResolveOp, "MoonrayLightFiltersResolve", 0, 1);
}

