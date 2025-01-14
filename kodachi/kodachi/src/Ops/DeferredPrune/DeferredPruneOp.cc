// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// kodachi
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/PathUtil.h>


namespace {
KdLogSetup("DeferredPrune");

// Op
const std::string kDeferredPruneOpName                  { "DeferredPrune" };
const std::string kDeferredPruneRestoreOpName           { "DeferredPruneRestore" };
const std::string KDeferredPruneResolveOpName           { "DeferredPruneResolve" };
const std::string kDeferredPruneViewerTerminalOpName    { "DeferredPruneViewerTerminal" };

// OpArgs
const std::string kCEL              { "CEL" };
const std::string kDeferredPrune    { "deferredPrune" };
const std::string kRestorePaths     { "restorePaths" };

// Attribute Names
const std::string kViewerFill       { "viewer.default.drawOptions.fill" };
const std::string kViewerColor      { "viewer.default.drawOptions.color" };

const int kDeferredPruneOff = 0;
const int kDeferredPruneOn = 1;


struct DeferredPruneOp : public kodachi::Op
{
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const FnKat::StringAttribute celAttr = interface.getOpArg(kCEL);
        if (!celAttr.isValid()) {
            interface.stopChildTraversal();
            return;
        }

        const kodachi::string_view cel = celAttr.getValueCStr();
        if (cel.empty()) {
            interface.stopChildTraversal();
            return;
        }

        kodachi::CookInterfaceUtils::MatchesCELInfo info;
        kodachi::CookInterfaceUtils::matchesCEL(info,
                                                interface,
                                                celAttr);
        if (!info.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!info.matches) {
            return;
        }

        interface.setAttr(kDeferredPrune, kodachi::IntAttribute(kDeferredPruneOn), false /* groupInherit */);

    }

    static FnAttribute::GroupAttribute describe()
    {

        kodachi::OpDescriptionBuilder builder;
        builder.setNumInputs(1);

        builder.setSummary("Marks matching locations to be pruned at a later point");
        builder.setHelp("Any locations that are marked as 'deferredPrune' will be pruned when implicit resolvers are run");

        builder.describeOpArg(kodachi::OpArgDescription(kodachi::AttrTypeDescription::kTypeStringAttribute, kCEL));

        return builder.build();
    }
};

struct DeferredPruneRestoreOp : public kodachi::Op
{
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::StringAttribute restorePathsAttr = interface.getOpArg(kRestorePaths);
        if (!restorePathsAttr.isValid()) {
            interface.stopChildTraversal();

            return;
        }

        const std::string inputLocation = interface.getInputLocationPath();
        kodachi::IntAttribute deferredPruneAttr;
        kodachi::IntAttribute deferredPruneOpArg;
        kodachi::GroupBuilder gb;
        gb.setGroupInherit(false);
        
        // internally we want to use CEL but users need to specify
        // explicit SceneGraph location paths
        kodachi::CookInterfaceUtils::MatchesCELInfo info;
        kodachi::CookInterfaceUtils::matchesCEL(info, interface, restorePathsAttr);

        gb.set(kRestorePaths, kodachi::StringAttribute(restorePathsAttr));

        if (info.matches) {
            // we are restoring the current input location
           interface.setAttr(kDeferredPrune, kodachi::IntAttribute(kDeferredPruneOff), false /* groupInherit */);
           interface.replaceChildTraversalOp(kDeferredPruneRestoreOpName, gb.build());
        } else if (info.canMatchChildren) {
            // we are not restoring the input location but some of it's children
            deferredPruneAttr = interface.getAttr(kDeferredPrune);
            if (deferredPruneAttr.isValid()) {
                // we have a location that is marked to be pruned but
                // we need to push that to children locations who are not being restored
                KdLogDebug("Restoring Parent " << inputLocation);
                if (deferredPruneAttr.getValue() == kDeferredPruneOn) {
                    interface.setAttr(kDeferredPrune, kodachi::IntAttribute(kDeferredPruneOff), false /* groupInherit */);
                    gb.set(kDeferredPrune, kodachi::IntAttribute(kDeferredPruneOn), false /* groupInherit */);
                } else {
                    gb.set(kDeferredPrune, kodachi::IntAttribute(kDeferredPruneOff), false /* groupInherit */);
                }

            } else {
                deferredPruneOpArg = interface.getOpArg(kDeferredPrune);
                if (deferredPruneOpArg.isValid()) {
                    // the location is not marked to be pruned but we need to make sure that it's
                    // children being restored are not pruned
                    interface.setAttr(kDeferredPrune, kodachi::IntAttribute(kDeferredPruneOff), false /* groupInherit */);

                    gb.set(kDeferredPrune, kodachi::IntAttribute(deferredPruneOpArg), false /* groupInherit */);
                }
            }

            interface.replaceChildTraversalOp(kDeferredPruneRestoreOpName, gb.build());

        } else {
            // the parent may need to force the deferredPrune attribute
            // being that it's deferredPrune state changed
            deferredPruneOpArg = interface.getOpArg(kDeferredPrune);
            if (deferredPruneOpArg.isValid()) {
                KdLogDebug("Forcing Deferred Prune on " << inputLocation);

                // we just need to change the first deferredPrune attribute
                // we encounter.
                interface.setAttr(kDeferredPrune, kodachi::IntAttribute(deferredPruneOpArg), false /* groupInherit */);
                interface.stopChildTraversal();
            }
        }
    }

    static FnAttribute::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;
        builder.setNumInputs(1);

        builder.setSummary("Restores matching locations to not be pruned at a later point");
        builder.setHelp("Any locations that are marked as 'deferredPrune' will be restored so they are not pruned when implicit resolvers are run");

        builder.describeOpArg(kodachi::OpArgDescription(kodachi::AttrTypeDescription::kTypeStringAttribute, kRestorePaths));

        return builder.build();
    }
};

struct DeferredPruneResolveOp : public kodachi::Op
{
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::IntAttribute deferredPruneAttr = interface.getAttr(kDeferredPrune);
        if (!deferredPruneAttr.isValid()) {
            return;
        }

        const int deferredPrune = deferredPruneAttr.getValue(0, false /* throwOnError */);
        if (deferredPrune == kDeferredPruneOn) {
            interface.deleteSelf();
        }
    }
};

struct DeferredPruneViewerTerminalOp : public kodachi::Op
{
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::IntAttribute deferredPruneAttr = interface.getAttr(kDeferredPrune);
        if (!deferredPruneAttr.isValid()) {
            return;
        }

        const int deferredPrune = deferredPruneAttr.getValue(0, false /* throwOnErr */);
        if (deferredPrune == kDeferredPruneOn) {
            static const std::array<float, 3> sColor = { 0.0f, 0.025f, 0.0f };

            interface.setAttr(kViewerFill, kodachi::StringAttribute("wireframe"));
            interface.setAttr(kViewerColor, kodachi::FloatAttribute(sColor.data(), 3, 1));
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(DeferredPruneOp)
DEFINE_GEOLIBOP_PLUGIN(DeferredPruneRestoreOp)
DEFINE_GEOLIBOP_PLUGIN(DeferredPruneResolveOp)
DEFINE_GEOLIBOP_PLUGIN(DeferredPruneViewerTerminalOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(DeferredPruneOp, kDeferredPruneOpName.c_str(), 0, 1);
    REGISTER_PLUGIN(DeferredPruneRestoreOp, kDeferredPruneRestoreOpName.c_str(), 0, 1);
    REGISTER_PLUGIN(DeferredPruneResolveOp, KDeferredPruneResolveOpName.c_str(), 0, 1);
    REGISTER_PLUGIN(DeferredPruneViewerTerminalOp, kDeferredPruneViewerTerminalOpName.c_str(), 0, 1);
}

