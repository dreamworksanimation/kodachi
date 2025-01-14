// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

namespace {

using namespace FnAttribute;

class MoonrayCutoutMaterialResolveOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        const std::string location = interface.getInputLocationPath();

        // Verify that the location has the "cutout" attribute
        const IntAttribute cutoutAttr = interface.getAttr("moonrayStatements.cutout");
        if (!cutoutAttr.getValue(false, false)) {
            return;
        }

        // Verify that there's a material set here
        const GroupAttribute materialAttr =
                GroupAttribute(interface.getAttr("material"));
        if (!materialAttr.isValid()) {
            return;
        }

        // Verify that the location has the "moonrayMaterial" attribute.
        // There are some places where we support "moonraySurface" for backwards
        // compatibility, so check for that too
        StringAttribute moonrayMaterialAttr =
                materialAttr.getChildByName("terminals.moonrayMaterial");
        if (!moonrayMaterialAttr.isValid()) {
            // moonrayMaterial is invalid.  Check the moonraySurface attr too
            moonrayMaterialAttr =
                    materialAttr.getChildByName("terminals.moonraySurface");
            if (!moonrayMaterialAttr.isValid()) {
                // Neither are valid.  Let's get out of here
                return;
            }
        }

        const std::string moonrayMaterialName = moonrayMaterialAttr.getValue();
        static std::string cutoutMaterialName = "moonray_cutout_insert";
        const StringAttribute cutoutMaterialNameAttr =
                StringAttribute(cutoutMaterialName);

        // Found a location with "cutout" and "moonrayMaterial"
        // Create a group builder and copy the existing material
        GroupBuilder materialBuilder;
        materialBuilder.update(materialAttr);

        // Set the cutout material as the new root material on the object
        materialBuilder.set("terminals.moonrayMaterial",
                            cutoutMaterialNameAttr);

        // Set the various attributes needed to connect the cutout material
        // to the existing network
        const std::string nodePrefix = "nodes." + cutoutMaterialName + ".";
        materialBuilder.set(nodePrefix+"name", cutoutMaterialNameAttr);
        materialBuilder.set(nodePrefix+"type", StringAttribute("CutoutMaterial"));
        materialBuilder.set(nodePrefix+"target", StringAttribute("moonray"));
        materialBuilder.set(nodePrefix+"connections.indirect_material",
                            StringAttribute("out@"+moonrayMaterialName));

        // Build the group and set the material
        interface.setAttr("material", materialBuilder.build());
    }


    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Turn a geometry into a cutout by adding a "
                           "CutoutMaterial to its material network");
        builder.setHelp("Finds the locations whose 'cutout' and 'moonrayMaterial/moonraySurface' "
                        "attributes have been set.  It will then create a CutoutMaterial"
                        "and set the necessary attributes to add it to the location's "
                        "material network");
        builder.setNumInputs(0);

        return builder.build();
    }
};



//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayCutoutMaterialResolveOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayCutoutMaterialResolveOp, "MoonrayCutoutMaterialResolve", 0, 1);
}

