// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <FnAttribute/FnAttribute.h>
#include <FnPluginSystem/FnPlugin.h>
#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>

namespace {

class MoonrayCookLightDAPsOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const std::string type = FnKat::GetInputLocationType(interface);
        if (type == "light" || type == "light filter") {
            const FnAttribute::GroupAttribute materialAttr = interface.getAttr("material");
            if (materialAttr.isValid()) {
                FnAttribute::GroupBuilder gb;
                gb.deepUpdate(materialAttr);
                gb.deepUpdate(FnKat::FnGeolibCookInterfaceUtils::cookDaps(
                        interface, "material").getChildByName("material"));
                interface.setAttr("material", gb.build());
            }
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Cook DAPs on each light's material.");
        builder.setHelp("Ensure that all default attributes are explicitly "
                        "set so that no data is missing in some situations.");
        builder.setNumInputs(0);

        builder.describeOutputAttr(
                OutputAttrDescription(AttrTypeDescription::kTypeGroupAttribute,
                                      "material"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayCookLightDAPsOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayCookLightDAPsOp, "MoonrayCookLightDAPs", 0, 1);
}

