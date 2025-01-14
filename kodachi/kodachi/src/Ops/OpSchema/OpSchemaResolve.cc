// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/CookInterfaceUtils.h>


namespace {

class OpSchemaResolveOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::GroupAttribute opsAttr =
                interface.getAttr("ops");

        if (!opsAttr.isValid()) {
            return;
        }

        const kodachi::GroupAttribute infoOpsAttr =
                interface.getAttr("info.ops");

        kodachi::GroupBuilder iob;
        iob.update(infoOpsAttr);

        for (const auto& child : opsAttr) {
            const kodachi::string_view opName = child.name;
            const kodachi::GroupAttribute opAttr = child.attribute;

            if (!opAttr.isValid()) {
                continue;
            }

            const kodachi::IntAttribute opSchemaAttr =
                    opAttr.getChildByName("__usdOpAPI");

            if (opSchemaAttr.isValid()
                    && opSchemaAttr.getValue() == 1) {
                // at this point the op coming from
                // the OpAPI has not been explicitly resolved
                // move it to info so it is not resolved by
                // OpResolve (resolveId=all)
                iob.set(opName, opAttr, false);
                iob.setGroupInherit(false);

                interface.deleteAttr(kodachi::concat("ops.", opName));
            }
        }

        interface.setAttr("info.ops", iob.build(), false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setHelp("Resolves any remaining ops that have been added from the Usd OpAPI schema");
        builder.setSummary("Scene graph locations that have remaining ops added from the Usd OpAPI schema"
                           "will be moved to info.ops. This prevents those specific ops from being resolved"
                           "by the OpResolve(resolveIds=all) implicit resolver");

        return builder.build();
    }
};


DEFINE_GEOLIBOP_PLUGIN(OpSchemaResolveOp)

} // anonymous namespace


void registerPlugins()
{
    REGISTER_PLUGIN(OpSchemaResolveOp, "OpSchemaResolve", 0, 1);
}

