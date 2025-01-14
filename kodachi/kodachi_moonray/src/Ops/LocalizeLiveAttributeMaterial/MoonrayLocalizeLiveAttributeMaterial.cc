// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

namespace {

class MoonrayLocalizeLiveAttributeMaterialOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const FnAttribute::GroupAttribute liveMaterialAttr =
                interface.getAttr("liveAttributes.material");

        if (!liveMaterialAttr.isValid()) {
            return;
        }

        FnAttribute::GroupBuilder newMaterialAttr;
        const FnAttribute::GroupAttribute oldMaterialAttr =
                interface.getAttr("material");

        if (oldMaterialAttr.isValid()) {
            newMaterialAttr.update(oldMaterialAttr);
        }
        newMaterialAttr.deepUpdate(liveMaterialAttr);
        interface.setAttr("material", newMaterialAttr.build());
        interface.deleteAttr("liveAttributes.material");
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Copy liveAttributes.material to material when using viewer manipulators.");
        builder.setHelp("Manipulating attributes in the viewer while live rendering will copy them "
                "to the liveAttributes attribute. This op checks for liveAttributes.material, and if "
                "it exists, does a deepUpdate on the existing material attribute, effectively copying "
                "any deltas generated from viewer manipulation. liveAttributes.material is deleted "
                "once it is finished.");

        builder.describeOutputAttr(OutputAttrDescription(
                AttrTypeDescription::kTypeGroupAttribute, "material"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayLocalizeLiveAttributeMaterialOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLocalizeLiveAttributeMaterialOp, "MoonrayLocalizeLiveAttributeMaterial", 0, 1);
}

