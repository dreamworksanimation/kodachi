// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>
#include <FnGeolib/op/FnOpDescriptionBuilder.h>
#include <FnGeolib/util/Path.h>

namespace {

const std::string kOpSummary("Localizes faceset data to the parent mesh");
const std::string kOpHelp("Copies relevant attributes from child facesets to the"
                          " 'facesets' attribute of the current mesh location.");

std::vector<std::string>
getPotentialChildren(const Foundry::Katana::GeolibCookInterface &interface)
{
    const auto potentialChildrenAttr = interface.getPotentialChildren();
    const auto nearestSample = potentialChildrenAttr.getNearestSample(0.f);

    return std::vector<std::string>(nearestSample.begin(), nearestSample.end());
}

// runs on mesh locations
class MoonrayLocalizeFacesetsOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        // Only mesh types have facesets
        const std::string type = FnKat::GetInputLocationType(interface);
        if (type != "subdmesh" && type != "polymesh") {
            return;
        }

        const auto potentialChildren = getPotentialChildren(interface);

        // prefetch children
        for (const auto& childName : potentialChildren) {
            interface.prefetch(childName);
        }

        // group builder for facesets
        FnAttribute::GroupBuilder gbFacesets;

        for (const auto& childName : potentialChildren) {
            const std::string childType =
                    FnKat::GetInputLocationType(interface, childName);

            // Verify child is a faceset
            if (childType != "faceset") {
                continue;
            }

            gbFacesets.set(childName, interface.getAttr("", childName));
        }

        // only create the facesets attribute if necessary
        if (gbFacesets.isValid()) {
            interface.setAttr("facesets", gbFacesets.build(), false);
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary(kOpSummary);
        builder.setHelp(kOpHelp);
        builder.setNumInputs(1);
        builder.describeInputAttr(
                InputAttrDescription(AttrTypeDescription::kTypeStringAttribute,
                                     "type"));
        builder.describeOutputAttr(
                OutputAttrDescription(AttrTypeDescription::kTypeGroupAttribute,
                                      "facesets"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayLocalizeFacesetsOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLocalizeFacesetsOp, "MoonrayLocalizeFacesets", 0, 1);
}

