// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

namespace {

const std::string kMoonrayLiveAttributeUpdate("MoonrayLiveAttributeUpdate");

class MoonrayLiveAttributeOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            const kodachi::GroupAttribute liveAttributesAttr =
                                               interface.getOpArg("liveAttrs");

            if (!liveAttributesAttr.isValid()) {
                return;
            }

            kodachi::op_args_builder::StaticSceneCreateOpArgsBuilder sscb(true);

            for (auto childAttr : liveAttributesAttr) {
                const std::string location(childAttr.name);
                const kodachi::GroupAttribute valueAttr = childAttr.attribute;

                sscb.addSubOpAtLocation(location, kMoonrayLiveAttributeUpdate, valueAttr);
            }

            interface.execOp("StaticSceneCreate", sscb.build());
        }
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Creates SubOps to handle setting of Live Attributes");

        return builder.build();
    }
};

class MoonrayLiveAttributeUpdateOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::GroupAttribute valueAttr = interface.getOpArg("");

        for (auto attr : valueAttr) {
            if (attr.attribute.getType() == kodachi::kAttrTypeGroup) {
                kodachi::GroupBuilder gb;
                gb.setGroupInherit(false);
                gb.update(interface.getAttr(attr.name));
                gb.deepUpdate(attr.attribute);
                interface.setAttr(attr.name, gb.build());
            } else {
                interface.setAttr(attr.name, attr.attribute);
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("DeepUpdates Attributes for a location with Live Attribute values");

        return builder.build();
    }
};

DEFINE_KODACHIOP_PLUGIN(MoonrayLiveAttributeOp)
DEFINE_KODACHIOP_PLUGIN(MoonrayLiveAttributeUpdateOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLiveAttributeOp, "MoonrayLiveAttribute", 0, 1);
    REGISTER_PLUGIN(MoonrayLiveAttributeUpdateOp, "MoonrayLiveAttributeUpdate", 0, 1);
}

