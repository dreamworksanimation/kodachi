// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// C++
#include <algorithm>

// Katana
#include <FnAPI/FnAPI.h>
#include <FnGeolib/op/FnGeolibOp.h>
#include <FnAttribute/FnAttribute.h>
#include <pystring/pystring.h>

// Kodachi
#include <kodachi/attribute/Attribute.h>

namespace
{

class ExrMergePrepOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        if (interface.atRoot()) {

            // (1) Find "disabled" render outputs
            std::vector<kodachi::string_view> disabledOutputs;

            const FnAttribute::GroupAttribute outputsGroupAttr =
                    interface.getAttr("renderOutputs");

            for (const auto output : outputsGroupAttr) {
                if (output.attribute.getType() != kFnKatAttributeTypeString) {
                    continue; // only interested in render output paths, skip the rest
                }

                const FnAttribute::StringAttribute pathAttr = output.attribute;
                if (!pathAttr.isValid() || pathAttr.getValue().empty()) {
                    disabledOutputs.push_back(output.name);
                    continue;
                }
            }

            // (2) Find render output(s) of type "merge" and update their
            // "mergeOutputs" attribute in case referencing disabled
            // render output(s).
            //
            if (!disabledOutputs.empty()) {
                const FnAttribute::GroupAttribute renderSettingsOutputsGroupAttr =
                        interface.getAttr("renderSettings.outputs");

                for (const auto& output : renderSettingsOutputsGroupAttr) {
                    const FnAttribute::GroupAttribute outputAttrs = output.attribute;
                    const FnAttribute::StringAttribute outputTypeAttr =
                            outputAttrs.getChildByName("type");

                    // Only interested in outputs of type "merge"
#if KATANA_VERSION_MAJOR == 3
                    if (outputTypeAttr != "merge") {
                        continue;
                    }
#else
                    if (outputTypeAttr.getValue() != "merge") {
                        continue;
                    }
#endif

                    // Skip if this is a disabled merge output
                    const auto iter = std::find(disabledOutputs.cbegin(),
                                                disabledOutputs.cend(),
                                                output.name);
                    if (iter != disabledOutputs.cend()) {
                        continue;
                    }

                    const FnAttribute::StringAttribute mergeOutputsAttr =
                            outputAttrs.getChildByName("mergeOutputs");
                    if (!mergeOutputsAttr.isValid()) {
                        continue;
                    }

                    const std::string mergeOutputsStr = mergeOutputsAttr.getValue();
                    std::vector<std::string> mergeOutputsVec;
                    pystring::split(mergeOutputsStr, mergeOutputsVec, ",");

                    std::string newMergeOutputsStr; // rebuild mergeOutputs string
                    newMergeOutputsStr.reserve(mergeOutputsStr.size());

                    for (const auto& mergeOutputName : mergeOutputsVec) {
                        const auto disabledIter = std::find(disabledOutputs.cbegin(),
                                                            disabledOutputs.cend(),
                                                            mergeOutputName);
                        // Skip disabled outputs, otherwise append output name to newMergeOutputsStr
                        if (disabledIter == disabledOutputs.cend()) {
                            newMergeOutputsStr += (mergeOutputName + ",");
                        }
                    }

                    // Remove the last ','
                    if (newMergeOutputsStr.back() == ',') {
                        newMergeOutputsStr.pop_back();
                    }

                    // If mergeOutputs string is different than the original value,
                    // update the attribute
                    if (newMergeOutputsStr != mergeOutputsStr) {
                        interface.setAttr(
                                kodachi::concat("renderSettings.outputs.", output.name, ".mergeOutputs"),
                                FnAttribute::StringAttribute(newMergeOutputsStr));
                    }
                }
            }
        }

        interface.stopChildTraversal();
    }
};

DEFINE_GEOLIBOP_PLUGIN(ExrMergePrepOp)

}  // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(ExrMergePrepOp, "ExrMergePrep", 0, 1);
}

