// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi_moonray/light_util/LightUtil.h>

#include <sys/stat.h>

#include <algorithm>

namespace {

KdLogSetup("UDIMPathsResolverOp");

const std::string kMaterialType = "material";
const std::string kImageMapType = "ImageMap";
static const int sMaxUdim = 10;
static const int sUdimStart = 1001;

class UDIMPathsResolveOp : public kodachi::Op
{
public:
    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    // The logic of this resolver is copied from UdimTexture.cc in Moonray - If the logic in
    // UdimTexture.cc ever changes, we should also update this resolver.
    static void cook(kodachi::GeolibCookInterface &interface)
    {
        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
        if (typeAttr != kMaterialType) {
            return;
        }

        const kodachi::GroupAttribute nodesAttr = interface.getAttr("material.nodes");

        if (!nodesAttr.isValid()) {
            return;
        }

        for (int64_t i = 0; i < nodesAttr.getNumberOfChildren(); ++i) {
            const std::string nodeName = nodesAttr.getChildName(i);
            const kodachi::GroupAttribute nodeAttr = nodesAttr.getChildByIndex(i);

            if (!nodeAttr.isValid() && nodeAttr.getType() == kFnKatAttributeTypeNull) {
                continue;
            }

            const kodachi::StringAttribute typeAttr = nodeAttr.getChildByName("type");
            if (typeAttr == kImageMapType) {
                const kodachi::GroupAttribute paramAttr = nodeAttr.getChildByName("parameters");
                if (!paramAttr.isValid()) {
                    continue;
                }
                const kodachi::StringAttribute textureAttr = paramAttr.getChildByName("texture");
                if (!textureAttr.isValid()) {
                    continue;
                }

                int maxV = 0;
                const kodachi::IntAttribute maxVdim = paramAttr.getChildByName("udim_max_v");
                if (maxVdim.isValid()) {
                    maxV = maxVdim.getValue();
                }
                else {
                    maxV = 10;
                }

                const std::string filename = textureAttr.getValueCStr();
                const std::size_t udimPos = filename.find("<UDIM>");
                const kodachi::Attribute udimValues = paramAttr.getChildByName("udim_values");
                const kodachi::Attribute udimFiles = paramAttr.getChildByName("udim_files");

                if (!udimValues.isValid() && !udimFiles.isValid() && udimPos != std::string::npos) {
                    int udimVal = 0;
                    std::vector<int> udim_vals;
                    std::vector<std::string> udim_files;

                    std::string udimFileName = filename;
                    udimFileName.replace(udimPos, 6, "UDIM");

                    for (int i = 0; i < sMaxUdim * maxV; ++i) {
                        udimVal = i + sUdimStart;
                        udimToStr(udimVal, udimFileName, udimPos);

                        if (fileExists(udimFileName)) {
                            udim_vals.push_back(udimVal);
                            udim_files.push_back(udimFileName);
                        }
                    }

                    interface.setAttr("material.nodes." + nodeName + ".parameters.udim_values",
                        kodachi::ZeroCopyIntAttribute::create(std::move(udim_vals)));
                    interface.setAttr("material.nodes." + nodeName + ".parameters.udim_files",
                            kodachi::ZeroCopyStringAttribute::create(std::move(udim_files)));
                }
            }
        }
    }

    static inline void udimToStr(const int udim, std::string &result, const size_t pos)
    {
        const char digits[10] = { '0', '1', '2', '3', '4', '5', '6', '7',
                                  '8', '9' };
        result[pos]           = digits[(udim / 1000) % 10];
        result[pos + 1]       = digits[(udim / 100)  % 10];
        result[pos + 2]       = digits[(udim / 10)   % 10];
        result[pos + 3]       = digits[ udim         % 10];
    }

    // Check if a file exists at the specified filepath
    static bool
    fileExists(const std::string& filePath)
    {
        struct stat buffer;
        return (stat(filePath.c_str(), &buffer) == 0);
    }

    static inline bool isInteger(const std::string & s, int &result)
{
    if (!s.empty() && std::all_of(s.begin(), s.end(), ::isdigit)) {
        result = std::stoi(s);

        return true;
    }

    return false;
}

    static kodachi::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("Looks up the individual UDIM texture paths using the <UDIM> flag contained in ImageMap.");
        builder.setHelp("Using the <UDIM> token contained in USD files, this implicit "
                        "resolver obtains the individual UDIM files that actually "
                        "correspond to this flag and sets then as parameters to be sent "
                        "to Moonray.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(UDIMPathsResolveOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(UDIMPathsResolveOp, "UDIMPathsResolver", 0, 1);
}

