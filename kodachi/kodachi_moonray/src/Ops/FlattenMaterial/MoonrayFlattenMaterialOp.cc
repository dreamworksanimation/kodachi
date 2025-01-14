// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <sstream>
#include <map>
#include <unordered_set>

#include <FnGeolib/util/Path.h>

namespace {

const std::string kOpSummary("Flatten material hierarchy for current location");
const std::string kOpHelp("Localizes the material on locations with types that"
                          " map to a Moonray geometry type. The entire hierarchy"
                          " is traversed, merging each material with the one"
                          " above it. Only one shader type/terminal can exist"
                          " on the final merged material, using the values from"
                          " the closest location to the input location.");

const std::unordered_set<std::string> kMaterialAssignableTypes {
  "curves",
  "faceset",
  "pointcloud",
  "polymesh",
  "renderer procedural",
  "subdmesh",
  "volume",
  "instance array"
};

// For each material's group attribute, store a list of terminals
// that should be copied over to the final network material
struct MaterialInfo {
    FnAttribute::GroupAttribute mAttribute;
    std::set<std::string> mTerminals;
};

const std::string kMaterial("material");

class MoonrayFlattenMaterialOp : public kodachi::Op
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr("/root/world/geo//*");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // Verify location is of a valid type
        const std::string type = FnKat::GetInputLocationType(interface);
        if (kMaterialAssignableTypes.find(type) == kMaterialAssignableTypes.end()) {
            return;
        }

        // Keep track of which terminals are being used by materials already
        // examined, so that we can ignore materials that add nothing new
        std::set<std::string> terminalsUsed;

        // Recursively check parent locations and gather a full list of
        // materials being inherited by this location.
        std::vector<MaterialInfo> materialChain;

        auto location = interface.getInputLocationPath();
        // Recurse the entire way up the chain
        while (!location.empty()) {
            const FnAttribute::GroupAttribute materialAttr = interface.getAttr(kMaterial, location);
            if (materialAttr.isValid()) {
                // We only have to include materials that add new terminals to
                // our flattened material. If they don't add anything new, we
                // remove them from so the second pass can skip them.
                bool includeThisMaterial = false;

                MaterialInfo matInfo;
                // Network materials may have up to 2 attrs per terminal, X and XPort.
                // eg moonrayDisplacement and moonrayDisplacementPort
                const FnAttribute::GroupAttribute terminalsAttr =
                        materialAttr.getChildByName("terminals");
                for (int64_t i = 0; i < terminalsAttr.getNumberOfChildren(); ++i) {
                    const std::string attrName = terminalsAttr.getChildName(i);
                    const int index = attrName.rfind("Port");
                    // Skip any attributes that end with "Port"
                    if (index == std::string::npos || index != attrName.size() - 4) {
                        if (terminalsUsed.insert(attrName).second) {
                            matInfo.mTerminals.insert(attrName);
                            includeThisMaterial = true;
                        }
                    }
                }
                if (includeThisMaterial) {
                    matInfo.mAttribute = materialAttr;
                    materialChain.push_back(matInfo);
                }
            }
            location = FnKat::Util::Path::GetLocationParent(location);
        }

        // If no materials or only 1 material was found, we don't
        // need to do any merging and can just return right here.
        const size_t numMaterialsFound = materialChain.size();
        if (numMaterialsFound == 0) {
            interface.setAttr(kMaterial, FnAttribute::GroupAttribute(), false);
            return;
        } else if (numMaterialsFound == 1) {
            interface.setAttr(kMaterial, materialChain[0].mAttribute, false);
            return;
        }

        int uidCounter = 0;

        // Merge all the contributing network materials together
        FnAttribute::GroupBuilder finalNetworkMaterialBuilder, finalNodesBuilder, finalTerminalsBuilder;
        for (const auto& material : materialChain) {
            // For each network material, we have to convert their names to
            // a unique name. Two network materials may reference the same node
            // but when merging, they will have different names. Because of this,
            // the nodeNameMap must be reset for each node.
            std::map<std::string, std::string> nodeNameMap;
            const FnAttribute::GroupAttribute& materialAttr = material.mAttribute;

            FnAttribute::GroupAttribute nodesAttr = materialAttr.getChildByName("nodes");
            for (int64_t nodeNum = 0; nodeNum < nodesAttr.getNumberOfChildren(); ++nodeNum) {
                const FnAttribute::GroupAttribute nodeAttr = nodesAttr.getChildByIndex(nodeNum);
                const FnAttribute::StringAttribute nameAttr = nodeAttr.getChildByName("name");
                if (!nameAttr.isValid()) {
                    continue;
                }
                const std::string name = nameAttr.getValue();
                // Generate unique name. This isn't technically guaranteed to
                // be unique, but it's fine as long as no one appends :0 to
                // their node names.
                nodeNameMap[name] = name + ":" + std::to_string(uidCounter++);
            }

            // Copy all nodes over to the new network material, and replace the
            // name with a unique name
            for (int64_t nodeNum = 0; nodeNum < nodesAttr.getNumberOfChildren(); ++nodeNum) {
                const FnAttribute::GroupAttribute nodeAttr = nodesAttr.getChildByIndex(nodeNum);
                const FnAttribute::StringAttribute nameAttr = nodeAttr.getChildByName("name");
                if (!nameAttr.isValid()) {
                    continue;
                }
                const std::string name = nameAttr.getValue();

                // Copy over all node attributes, and replace the name and any
                // connections with the new unique names. The nodes attribute
                // of network materials are ordered such that dependencies are
                // listed first. Therefore, for connections, we assume that
                // the connected node already exists in nodeNameMap.
                FnAttribute::GroupBuilder nodeBuilder;
                nodeBuilder.setGroupInherit(nodeAttr.getGroupInherit());
                for (int64_t attrNum = 0; attrNum < nodeAttr.getNumberOfChildren(); ++attrNum) {
                    const std::string nodeAttrName = nodeAttr.getChildName(attrNum);
                    if (nodeAttrName == "name") {
                        nodeBuilder.set(nodeAttrName, FnAttribute::StringAttribute(nodeNameMap[name]));
                    } else if (nodeAttrName == "connections") {
                        FnAttribute::GroupBuilder connectionsBuilder;
                        const FnAttribute::GroupAttribute connectionsAttr =
                                nodeAttr.getChildByIndex(attrNum);
                        for (int64_t conn = 0; conn < connectionsAttr.getNumberOfChildren(); ++conn) {
                            const FnAttribute::StringAttribute connectionAttr =
                                    connectionsAttr.getChildByIndex(conn);
                            // Connection attr is changed as follows:
                            // out@Node -> out@Node:0
                            if (connectionAttr.isValid()) {
                                std::string connectionName = connectionAttr.getValue();
                                const size_t nameStart = connectionName.find('@') + 1;
                                const std::string connectedNodeName =
                                        connectionName.substr(nameStart, std::string::npos);
                                connectionName.replace(nameStart,
                                                       connectedNodeName.size(),
                                                       nodeNameMap[connectedNodeName]);
                                connectionsBuilder.set(connectionsAttr.getChildName(conn),
                                                       FnAttribute::StringAttribute(connectionName));
                            }
                        }
                        nodeBuilder.set("connections", connectionsBuilder.build());
                    } else {
                        nodeBuilder.set(nodeAttrName, nodeAttr.getChildByName(nodeAttrName));
                    }
                }

                finalNodesBuilder.set(nodeNameMap[name], nodeBuilder.build());
            }

            // Copy all terminal attributes, swapping out old names for new ones
            for (const auto& terminal : material.mTerminals) {
                const FnAttribute::GroupAttribute terminalsAttr =
                        material.mAttribute.getChildByName("terminals");
                const FnAttribute::StringAttribute terminalAttr =
                        terminalsAttr.getChildByName(terminal);
                if (terminalAttr.isValid()) {
                    finalTerminalsBuilder.set(terminal,
                            FnAttribute::StringAttribute(nodeNameMap[terminalAttr.getValue()]));

                    // If it has a "Port" attr, copy that too.
                    const std::string portAttrName = terminal + "Port";
                    const FnAttribute::Attribute portAttr = terminalsAttr.getChildByName(portAttrName);
                    if (portAttr.isValid()) {
                        finalTerminalsBuilder.set(portAttrName, portAttr);
                    }
                }
            }
        }

        finalNetworkMaterialBuilder.setGroupInherit(false);
        finalNodesBuilder.setGroupInherit(false);
        finalTerminalsBuilder.setGroupInherit(false);
        finalNetworkMaterialBuilder.set("style", FnAttribute::StringAttribute("network"));
        finalNetworkMaterialBuilder.set("nodes", finalNodesBuilder.build());
        finalNetworkMaterialBuilder.set("terminals", finalTerminalsBuilder.build());
        interface.setAttr(kMaterial, finalNetworkMaterialBuilder.build(), false);
    }

    static FnAttribute::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary(kOpSummary);
        builder.setHelp(kOpHelp);
        builder.setNumInputs(1);
        builder.describeInputAttr(
                InputAttrDescription(AttrTypeDescription::kTypeStringAttribute,
                                     "type"));

        InputAttrDescription materialInput(AttrTypeDescription::kTypeGroupAttribute,
                                           kMaterial);
        materialInput.setOptional(true);
        builder.describeInputAttr(materialInput);

        builder.describeOutputAttr(
                OutputAttrDescription(AttrTypeDescription::kTypeGroupAttribute,
                                      kMaterial));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayFlattenMaterialOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayFlattenMaterialOp, "MoonrayFlattenMaterial", 0, 1);
}

