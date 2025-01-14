// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

namespace
{

KdLogSetup("LightFilterAssignOps");

// copies the light filter material attributes assigned to a light with lightFilterAssign
class LightFilterAssignOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::StringAttribute kCelAttr = interface.getOpArg("lights");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCelAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // path to filter to assign
        const kodachi::StringAttribute lightFilterAssign =
                interface.getOpArg("lightFilterLocation");

        // filters currently already assigned to the location
        const kodachi::GroupAttribute lightFiltersGroup =
                kodachi::GetGlobalAttr(interface, "material.lightFilters");

        // filters are organized into
        // material
        //     |_filter0
        //         |_ path
        //         |_ enabled
        //     |_filter1
        //         ...

        // if the filter already exists, do nothing
        for (const auto filter : lightFiltersGroup) {
            const kodachi::GroupAttribute childFilter = filter.attribute;
            const kodachi::StringAttribute childFilterPath = childFilter.getChildByName("path");
            if (childFilterPath == lightFilterAssign) {
                // this filter is already assigned to this location
                return;
            }
        }

        static const kodachi::string_view kFilter("filter");

        kodachi::GroupBuilder gb;
        gb.deepUpdate(lightFiltersGroup);
        gb.set(kodachi::concat(kFilter, std::to_string(lightFiltersGroup.getNumberOfChildren())),
               kodachi::GroupAttribute("path", lightFilterAssign,
                                       "enabled", kodachi::IntAttribute(1),
                                       false));

        interface.setAttr("material.lightFilters", gb.build());
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        const std::string opHelp    = "";
        const std::string opSummary = "";

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {

    }
};

class LightFilterAssignResolveOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface& interface)
    {
        static const std::string kRootLocation("/root");

        // OpArgs
        if (interface.getInputLocationPath() == kRootLocation) {
            kodachi::GroupBuilder gb;
            gb.update(interface.getOpArg(""));

            const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
            if (!celAttr.isValid()) {
                static const kodachi::StringAttribute kDefaultCELAttr(
                        R"(/root/world//*{@type=="light"})");
                gb.set("CEL", kDefaultCELAttr);
            }

            if (gb.isValid()) {
                interface.replaceChildTraversalOp("", gb.build());
            }

            return;
        }

        // CEL
        {
            const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");

            kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
            kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, celAttr);

            if (!celInfo.canMatchChildren) {
                interface.stopChildTraversal();
            }

            if (!celInfo.matches) {
                return;
            }
        }

        const kodachi::GroupAttribute lightFilters =
                kodachi::GetGlobalAttr(interface, "material.lightFilters");

        if (!lightFilters.isValid()) {
            return;
        }

        for (const auto filter : lightFilters) {
            const kodachi::GroupAttribute filterGrp = filter.attribute;

            const kodachi::IntAttribute enabled = filterGrp.getChildByName("enabled");
            if (enabled.getValue(true, false)) {
                const kodachi::StringAttribute pathAttr = filterGrp.getChildByName("path");
                const kodachi::string_view path = pathAttr.getValueCStr("", false);

                if (interface.doesLocationExist(path)) {
                    interface.prefetch(path);

                    const kodachi::string_view name = path.substr(path.rfind("/") + 1);
                    const std::string childName = kodachi::concat(name, "_", pathAttr.getHash().str());

                    interface.copyLocationToChild(filter.name, path);
                }
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        const std::string opHelp    = "";
        const std::string opSummary = "";

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {

    }
};

DEFINE_KODACHIOP_PLUGIN(LightFilterAssignOp)
DEFINE_KODACHIOP_PLUGIN(LightFilterAssignResolveOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(LightFilterAssignOp, "LightFilterAssign", 0, 1);
    REGISTER_PLUGIN(LightFilterAssignResolveOp, "LightFilterAssignResolve", 0, 1);
}


