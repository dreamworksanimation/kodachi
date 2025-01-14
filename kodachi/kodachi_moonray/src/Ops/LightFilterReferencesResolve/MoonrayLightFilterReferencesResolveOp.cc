// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <unordered_set>

#include <FnGeolib/util/Path.h>

namespace {

const std::string kOpSummary("Copy light filter material to reference location");
const std::string kOpHelp("For each light filter reference, finds the light filter "
                          "it is referencing and copies its material attribute to "
                          "the reference's location.");


class MoonrayLightFilterReferencesResolveOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const std::string type = FnKat::GetInputLocationType(interface);
        if (type != "light filter reference") {
            return;
        }

        const FnAttribute::StringAttribute refPathAttr = interface.getAttr("referencePath");
        if (refPathAttr.isValid()) {
            const std::string location = refPathAttr.getValue();
            if (interface.doesLocationExist(location)) {
                interface.setAttr("material", interface.getAttr("material", location));
                // Both the light filter reference and the light filter could be
                // muted, so we have to check both. If either of them are, then
                // this reference shouldn't be added.
                FnAttribute::StringAttribute muteAttr =
                        interface.getAttr("info.light.muteState");
                if (muteAttr.isValid() && muteAttr != "muteEmpty") {
                    interface.setAttr("info.light.muteState", muteAttr);
                } else {
                    FnAttribute::StringAttribute refMuteAttr =
                            interface.getAttr("info.light.muteState", location);
                    if (refMuteAttr.isValid() && refMuteAttr != "muteEmpty") {
                        interface.setAttr("info.light.muteState", refMuteAttr);
                    }
                }
            }
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
                                      "material"));
        builder.describeOutputAttr(
                OutputAttrDescription(AttrTypeDescription::kTypeIntAttribute,
                                      "info.light.muteState"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayLightFilterReferencesResolveOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLightFilterReferencesResolveOp, "MoonrayLightFilterReferencesResolve", 0, 1);
}

