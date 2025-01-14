// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

namespace {

/**
 * This Op works in conjunction with the ContextSwitch node. It is only intended
 * to be executed by Katana's runtime, and not when building the individual
 * optrees for each context in a multi-context render.
 */
class ContextSwitchOp : public FnGeolibOp::GeolibOp
{
public:
    static void setup(FnGeolibOp::GeolibSetupInterface& interface)
    {
        interface.setThreading(FnGeolibOp::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(FnGeolibOp::GeolibCookInterface& interface)
    {
        const FnAttribute::StringAttribute inputNamesAttr =
                interface.getOpArg("inputNames");

        if (inputNamesAttr.isValid()) {
            const auto inputNames = inputNamesAttr.getNearestSample(0.f);

            const FnAttribute::GroupAttribute contextsAttr =
                    interface.getAttr("contexts", "/root");

            FnAttribute::GroupBuilder gb;
            if (contextsAttr.isValid()) {
                gb.update(contextsAttr);
            }

            for (const FnPlatform::StringView inputName : inputNames) {
                const auto contextAttr = contextsAttr.getChildByName(inputName);
                if (!contextAttr.isValid()) {
                    // We have no additional data at the moment, but make these
                    // so we don't have to change the structure later
                    gb.set(inputName, FnAttribute::GroupAttribute("name",
                            FnAttribute::StringAttribute(inputName.data()), false));
                }
            }

            if (gb.isValid()) {
                interface.setAttr("contexts", gb.sort().build(), false);
            }
        }

        // We only want to cook at root
        interface.stopChildTraversal();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(ContextSwitchOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(ContextSwitchOp, "ContextSwitch", 0, 1);
}

