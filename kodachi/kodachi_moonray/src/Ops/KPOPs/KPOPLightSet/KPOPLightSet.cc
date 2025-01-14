// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <unordered_map>

namespace {

KdLogSetup("KPOPLightSet");

enum class SetType {
    LIGHT,
    SHADOW,
    LIGHT_FILTER
};

// Creates the AttributeSet args necessary to create a child rdl2 location
// that describes a LightSet
kodachi::GroupAttribute
buildAttributeSetArgs(const kodachi::StringAttribute& setNameAttr,
                      const kodachi::StringAttribute& valueAttr,
                      SetType setType)
{
    static const kodachi::StringAttribute kLightSetAttr("LightSet");
    static const kodachi::StringAttribute kShadowSetAttr("ShadowSet");
    static const kodachi::StringAttribute kLightFilterSetAttr("LightFilterSet");
    static const kodachi::StringAttribute kAttributeSetCELAttr("//*");
    static const kodachi::StringAttribute kRdl2Attr("rdl2");

    static const std::string kType("type");
    static const std::string kRdl2SceneObject("rdl2.sceneObject");

    kodachi::GroupBuilder setRdlGb;
    setRdlGb.set("name", setNameAttr);
    switch (setType) {
    case SetType::LIGHT:
        setRdlGb.set("sceneClass", kLightSetAttr);
        setRdlGb.set("attrs.lights", valueAttr);
        break;
    case SetType::SHADOW:
        setRdlGb.set("sceneClass", kShadowSetAttr);
        setRdlGb.set("attrs.lights", valueAttr);
        break;
    case SetType::LIGHT_FILTER:
        setRdlGb.set("sceneClass", kLightFilterSetAttr);
        setRdlGb.set("attrs.lightfilters", valueAttr);
        break;
    }

    // We don't have to worry about the SceneClass of this child changing,
    // So it can be looked up directly
    setRdlGb.set("disableAliasing", kodachi::IntAttribute(true));

    kodachi::op_args_builder::AttributeSetOpArgsBuilder asBuilder;
    asBuilder.setCEL(kAttributeSetCELAttr);
    asBuilder.setAttr(kType, kRdl2Attr);
    asBuilder.setAttr(kRdl2SceneObject, setRdlGb.build());

    return asBuilder.build();
}

class KPOPLightSet: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            const kodachi::IntAttribute lightsetCachingAttr =
                    interface.getAttr("moonrayGlobalStatements.lightsetCaching");

            if (lightsetCachingAttr.isValid()) {
                kodachi::GroupBuilder opArgsGb;
                opArgsGb.update(interface.getOpArg(""));
                opArgsGb.set("isCachingEnabled", lightsetCachingAttr);
                interface.replaceChildTraversalOp("", opArgsGb.build());
            }

            return;
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isLayerAssignable\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const bool isPart = interface.getAttr("rdl2.meta.isPart").isValid();

        const kodachi::IntAttribute cachingEnabledAttr =
                interface.getOpArg("isCachingEnabled");

        const bool isCachingEnabled = cachingEnabledAttr.getValue(true, false);

        const kodachi::IntAttribute autoInstancingEnabledAttr =
                interface.getAttr("rdl2.meta.autoInstancing.enabled");

        const bool isAutoInstancingEnabled = autoInstancingEnabledAttr.isValid();

        const std::string inputLocationPath = interface.getInputLocationPath();

        // Create the LightSet and LightFilterSet for this location from the lightList
        const kodachi::GroupAttribute lightListAttr = interface.getAttr("lightList");
        const int64_t numLightListEntries = lightListAttr.getNumberOfChildren();

        // Find the enabled lights and get their RDL names
        kodachi::StringVector lightSetVec;
        kodachi::StringVector shadowSetVec;
        kodachi::StringVector lightFilterSetVec;
        for (int64_t i = 0; i < numLightListEntries; ++i) {
            const kodachi::GroupAttribute lightListEntryAttr =
                    lightListAttr.getChildByIndex(i);
            if (lightListEntryAttr.getNumberOfChildren() == 0) {
                continue;
            }

            const kodachi::StringAttribute pathAttr =
                    lightListEntryAttr.getChildByName("path");
            if (!pathAttr.isValid()) {
                KdLogWarn("lightList entry missing 'path' attribute: "
                          << lightListAttr.getChildNameCStr(i));
                continue;
            }

            const kodachi::StringAttribute typeAttr =
                    lightListEntryAttr.getChildByName("type");
            const kodachi::IntAttribute enableAttr =
                    lightListEntryAttr.getChildByName("enable");

            static const kodachi::StringAttribute kLightFilterAttr("light filter");
            if (typeAttr == kLightFilterAttr) {
                if (enableAttr.getValue(true, false)) {
                    lightFilterSetVec.emplace_back(pathAttr.getValueCStr());
                }
            } else {
                if (enableAttr.getValue(false, false)) {
                    lightSetVec.emplace_back(pathAttr.getValueCStr());
                }

                const kodachi::IntAttribute shadowEnableAttr =
                        lightListEntryAttr.getChildByName("geoShadowEnable");

                if (!shadowEnableAttr.getValue(true, false)) {
                    shadowSetVec.emplace_back(pathAttr.getValueCStr());
                }
            }
        }


        // A LightSet is always required for a layer assignment, so always
        // create one, even if it is empty.
        {
            kodachi::StringAttribute lightsAttr;
            if (!lightSetVec.empty()) {
                    lightsAttr = kodachi::ZeroCopyStringAttribute::create(std::move(lightSetVec));
            }

            kodachi::StringAttribute lightSetNameAttr;
            if (isCachingEnabled) {
                lightSetNameAttr = kodachi::StringAttribute(
                        lightsAttr.getHash().str() + "__LightSet");
            } else {
                lightSetNameAttr = kodachi::StringAttribute(
                        inputLocationPath + "/__LightSet");
            }

            const kodachi::GroupAttribute attributeSetAttrs =
                    buildAttributeSetArgs(lightSetNameAttr, lightsAttr, SetType::LIGHT);

            interface.createChild("__LightSet", "AttributeSet", attributeSetAttrs);

            interface.setAttr("rdl2.layerAssign.lightSet", lightSetNameAttr, false);

            if (isAutoInstancingEnabled) {
                interface.setAttr("rdl2.meta.autoInstancing.attrs.lightSet", lightSetNameAttr);
            }
        }

        if (!shadowSetVec.empty()) {
            const kodachi::StringAttribute lightsAttr =
                    kodachi::ZeroCopyStringAttribute::create(std::move(shadowSetVec));

            kodachi::StringAttribute shadowSetNameAttr;
            if (isCachingEnabled) {
                shadowSetNameAttr = kodachi::StringAttribute(
                        lightsAttr.getHash().str() + "__ShadowSet");
            } else {
                shadowSetNameAttr = kodachi::StringAttribute(
                        inputLocationPath + "/__ShadowSet");
            }

            const kodachi::GroupAttribute attributeSetAttrs =
                    buildAttributeSetArgs(shadowSetNameAttr, lightsAttr, SetType::SHADOW);

            interface.createChild("__ShadowSet", "AttributeSet", attributeSetAttrs);

            interface.setAttr("rdl2.layerAssign.shadowSet", shadowSetNameAttr, false);

            if (isAutoInstancingEnabled) {
                interface.setAttr("rdl2.meta.autoInstancing.attrs.shadowSet", shadowSetNameAttr);
            }
        }

        // specifying nullptr for the LightFilterSet will cause all filters in
        // the lightset to apply to this part. So We should should always specify
        // a LightFilterSet, even when it is empty.
        {
            kodachi::StringAttribute lightFiltersAttr;
            if (!lightFilterSetVec.empty()) {
                 lightFiltersAttr = kodachi::ZeroCopyStringAttribute::create(
                         std::move(lightFilterSetVec));
            }

            kodachi::StringAttribute lightFilterSetNameAttr;
            if (isCachingEnabled) {
                lightFilterSetNameAttr = kodachi::StringAttribute(
                        lightFiltersAttr.getHash().str() + "__LightFilterSet");
            } else {
                lightFilterSetNameAttr = kodachi::StringAttribute(
                        inputLocationPath + "/__LightFilterSet");
            }

            const kodachi::GroupAttribute attributeSetAttrs =
                    buildAttributeSetArgs(lightFilterSetNameAttr,
                            lightFiltersAttr, SetType::LIGHT_FILTER);

            interface.createChild("__LightFilterSet", "AttributeSet", attributeSetAttrs);

            interface.setAttr("rdl2.layerAssign.lightFilterSet", lightFilterSetNameAttr, false);

            if (isAutoInstancingEnabled) {
                interface.setAttr("rdl2.meta.autoInstancing.attrs.lightFilterSet",
                        lightFilterSetNameAttr);
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Creates the LightSet and LightFilterSet for Geometry"
                           "and Faceset locations and optional ShadowSet for Geometry locations");

        return builder.build();
    }
};



//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPLightSet)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPLightSet, "KPOPLightSet", 0, 1);
}

