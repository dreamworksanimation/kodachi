// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>

#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>


namespace {

const std::string kOpSummary("Localizes an attribute at a specified location");
const std::string kOpHelp("To avoid using GetGlobalAttr, the global values and "
                          "localized values are combined into an opArg for "
                          "future locations to set. If a CEL location is provided, "
                          "the attribute is localized only at matching locations."
                          "Otherwise, it's localized everywhere.");

class MoonrayLocalizeAttributeOp : public kodachi::Op
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(
                Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }


    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        using namespace FnGeolibServices;

        // Get the CEL location if there is one.
        const kodachi::StringAttribute celArg = interface.getOpArg("CEL");

        // If CEL is not provided we want to match everywhere
        FnGeolibCookInterfaceUtils::MatchesCELInfo matchInfo{true, true};
        if (celArg.isValid()) {
            FnGeolibCookInterfaceUtils::matchesCEL(matchInfo, interface, celArg);

            if (!matchInfo.canMatchChildren) {
                interface.stopChildTraversal();
            }
        }

        // Get the attr name to localize from the attributeName opArg
        const kodachi::StringAttribute attributeNamesArg =
                interface.getOpArg("attributeNames");

        if (attributeNamesArg.isValid()) {

            kodachi::GroupAttribute localizedAttrs =
                            interface.getOpArg("localizedAttrs");

            kodachi::GroupBuilder localizedAttrsGb;
            localizedAttrsGb.update(localizedAttrs);

            for (const kodachi::string_view attrName : attributeNamesArg.getNearestSample(0.f)) {
                 const kodachi::Attribute localizedAttr =
                         localizeAttribute(interface, localizedAttrs, attrName);

                 if (localizedAttr.isValid()) {
                     // Set the attr both in the group and at the location to properly
                     // localize it.
                     localizedAttrsGb.set(attrName, localizedAttr);

                     if (matchInfo.matches) {
                         interface.setAttr(attrName, localizedAttr);
                     }
                 }
            }

            const kodachi::GroupAttribute childOpArgs("CEL", celArg,
                                                          "attributeNames", attributeNamesArg,
                                                          "localizedAttrs", localizedAttrsGb.build(),
                                                          false);

            interface.replaceChildTraversalOp("", childOpArgs);

        }
    }


    static kodachi::Attribute localizeAttribute(
                                  const Foundry::Katana::GeolibCookInterface& interface,
                                  const kodachi::GroupAttribute& localizedAttrs,
                                  const kodachi::string_view& attrName)
    {
        // Check if the attr is already localized at this location
        kodachi::Attribute attr = interface.getAttr(attrName);

        // If the attr is in the localizedAttrs, then its global state has also
        // been previously localized
        kodachi::Attribute localizedAttr = localizedAttrs.getChildByName(attrName);

        // It doesn't matter if the localized is valid or not
        if (!attr.isValid()) {
            return localizedAttr;
        }

        // If the attribute is a GroupAttribute, merge it with the localizedAttr
        if (localizedAttr.isValid() &&
                attr.getType() == kodachi::GroupAttribute::getKatAttributeType()) {
            kodachi::GroupBuilder gb;
            attr = gb.update(localizedAttr).deepUpdate(attr).build();
        }

        return attr;
    }


    static kodachi::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary(kOpSummary);
        builder.setHelp(kOpHelp);
        builder.setNumInputs(1);

        builder.describeOpArg(OpArgDescription(
                AttrTypeDescription::kTypeStringAttribute,
                "attributeNames"));

        builder.describeOpArg(OpArgDescription(
                AttrTypeDescription::kTypeStringAttribute,
                "CEL"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(MoonrayLocalizeAttributeOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLocalizeAttributeOp, "MoonrayLocalizeAttribute", 0, 1);
}

