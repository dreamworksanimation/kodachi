// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include <kodachi/op/Op.h>

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {
class KPOPFinalize : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        // Since '/root' is used for storing global state for different parts of
        // scene build it would be dangerous to remove any of the attributes,
        // but we can make sure they aren't group inherited
        if (interface.atRoot()) {
            const kodachi::GroupAttribute rootAttrs = interface.getAttr("");

            interface.deleteAttrs();

            for (const auto attr : rootAttrs) {
                interface.setAttr(attr.name, attr.attribute, false);
            }

            return;
        }

        // If the location isn't '/root' or of type 'rdl2' or 'error',
        // we don't care about its attributes and we can delete them all
        {
            static const kodachi::StringAttribute kRdl2TypeAttr("rdl2");
            static const kodachi::StringAttribute kErrorTypeAttr("error");

            const kodachi::StringAttribute typeAttr = interface.getAttr("type");

            if (typeAttr == kErrorTypeAttr) {
                return;
            }

            if (typeAttr != kRdl2TypeAttr) {
                interface.deleteAttrs();
                return;
            }
        }

        // This is an 'rdl2' location. We'll change its type to its SceneClass
        // to aid debugging in the scenegraph. Also remove all attributes
        // unneeded for scene build. We should only need:
        //
        // errorMessage/warningMessage : for non-critical errors to be printed to the logs
        // kodachi : for parallelTraversal attribute
        // instance: for auto-instancing
        // rdl2    : contains scene object, layer assignment and geometry set assignment

        const kodachi::GroupAttribute rdl2Attr = interface.getAttr("rdl2");
        if (!rdl2Attr.isValid()) {
            interface.setAttr("type", kodachi::StringAttribute("unknown"));
            kodachi::ReportNonCriticalError(interface,
                    "rdl2 location is missing rdl2 attr");
            return;
        }

        kodachi::StringAttribute typeAttr =
                rdl2Attr.getChildByName("sceneObject.sceneClass");

        if (!typeAttr.isValid()) {
            const kodachi::IntAttribute isPartAttr =
                    rdl2Attr.getChildByName("meta.isPart");

            if (isPartAttr.isValid()) {
                static const kodachi::StringAttribute kPartTypeAttr("part");
                typeAttr = kPartTypeAttr;
            } else {
                const kodachi::Attribute rdlFileAttr = interface.getAttr("rdl2.rdlFile");
                if (rdlFileAttr.isValid()) {
                    static const kodachi::StringAttribute kRdlArchiveAttr("rdl archive");
                    typeAttr = kRdlArchiveAttr;
                }
            }

            if (!typeAttr.isValid()) {
                interface.setAttr("type", kodachi::StringAttribute("unknown"));
                kodachi::ReportNonCriticalError(interface,
                        "rdl2 location is not a SceneObject, part, or rdl archive");
                return;
            }
        }

        const auto warningMessageAttr = interface.getAttr("warningMessage");
        const auto errorMessageAttr = interface.getAttr("errorMessage");
        const auto kodachiAttr = interface.getAttr("kodachi");
        const auto instanceAttr = interface.getAttr("instance");

        // TODO: katanaID should be stored in the kodachi attribute
        // So that we don't have to have a special case for a katana-specific
        // feature in kodachi code.
        const auto katanaIDAttr = interface.getAttr("katanaID");

        interface.deleteAttrs();

        if (warningMessageAttr.isValid()) {
            interface.setAttr("warningMessage", warningMessageAttr);
        }

        if (errorMessageAttr.isValid()) {
            interface.setAttr("errorMessage", errorMessageAttr);
        }

        interface.setAttr("type", typeAttr, false);
        interface.setAttr("rdl2", rdl2Attr, false);

        if (kodachiAttr.isValid()) {
            interface.setAttr("kodachi", kodachiAttr, false);
        }

        if (instanceAttr.isValid()) {
            interface.setAttr("instance", instanceAttr, false);
        }

        if (katanaIDAttr.isValid()) {
            interface.setAttr("katanaID", katanaIDAttr);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Final KPOP in the KPOPs chain");
        builder.setHelp("Removes attributes unneeded for scene build and changes the location's type to its rdl2::SceneClass name");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPFinalize)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPFinalize, "KPOPFinalize", 0, 1);
}

