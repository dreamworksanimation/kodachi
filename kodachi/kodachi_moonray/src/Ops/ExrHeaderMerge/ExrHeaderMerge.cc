// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <unordered_map>
#include <FnGeolib/op/FnGeolibOp.h>
#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>

namespace
{

struct FnAttributeHash
{
public:
    size_t operator()(const FnAttribute::Attribute& key) const
    {
        return key.getHash().uint64();
    }
};

class ExrHeaderMergeOp : public Foundry::Katana::GeolibOp
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        if (interface.atRoot()) {
            const FnAttribute::GroupAttribute outputs = interface.getAttr("renderSettings.outputs");
            if (outputs.isValid()) {
                // exr location -> list(output name, header attributes)
                std::unordered_map<FnAttribute::StringAttribute, std::vector<std::pair<
                           std::string, FnAttribute::GroupAttribute>>, FnAttributeHash> headerMap;
                // Sort the headers out all output channels by their target exr
                for (size_t i = 0; i < outputs.getNumberOfChildren(); ++i) {
                    const FnAttribute::GroupAttribute output = outputs.getChildByIndex(i);
                    if (output.isValid()) {
                        const FnAttribute::StringAttribute location = output.getChildByName(
                                "rendererSettings.tempRenderLocation");
                        const FnAttribute::GroupAttribute header =
                                output.getChildByName("rendererSettings.exr_header_attributes");
                        headerMap[location].push_back(std::make_pair(outputs.getChildName(i), header));
                    }
                }

                // For all outputs writing to the same exr, combine their
                // headers so that they're all identical.
                FnAttribute::GroupBuilder gb;
                for (const auto& header : headerMap) {
                    for (const auto& headerAttr : header.second) {
                        gb.update(headerAttr.second);
                    }
                    const auto finalAttr = gb.sort().build();
                    for (const auto& headerAttr : header.second) {
                        interface.setAttr("renderSettings.outputs." + headerAttr.first +
                                ".rendererSettings.exr_header_attributes", finalAttr);
                    }
                }
            }
        }

        interface.stopChildTraversal();
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Combine EXR headers of multiple outputs writing to the same file");
        builder.setHelp(
                "Moonray will throw errors when using multiple Metadata objects "
                "on the same EXR. This op will concatenate all headers within "
                "the same file to a single Metadata object. The op simply calls "
                "GroupBuilder.update() in random order and does not do anything "
                "special with conflicting header values.");
        builder.setNumInputs(0);

        builder.describeOutputAttr(OutputAttrDescription(
                AttrTypeDescription::kTypeGroupAttribute,
                "renderSettings.outputs.*.rendererSettings.exr_header_attributes"));

        return builder.build();
    }
};

DEFINE_GEOLIBOP_PLUGIN(ExrHeaderMergeOp)

}  // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(ExrHeaderMergeOp, "ExrHeaderMerge", 0, 1);
}

