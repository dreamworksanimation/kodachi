// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/StringView.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

#include <unordered_set>
#include <unordered_map>
#include <vector>

// TODO: Replace with kodachi Cache (speedhammer) instead
#include <FnGeolib/util/AttributeKeyedCache.h>
#include <boost/thread/shared_mutex.hpp>

namespace {

KdLogSetup("KPOPTerminal");

void
isolateNetworkNodes(const kodachi::string_view& rootName,
                    const kodachi::GroupAttribute& allNodes,
                    kodachi::GroupBuilder& gb,
                    std::unordered_set<kodachi::StringAttribute, kodachi::AttributeHash>& visitedNodes)
{
    // Traverse allNodes, starting from rootName.
    // Add the GroupAttribute to the groupbuilder if it hasn't been visited

    // Get the current node and its name
    const kodachi::GroupAttribute currentNode = allNodes.getChildByName(rootName);

    // Somehow the specified node isn't in the network.  Return.
    if (!currentNode.isValid()) {
        KdLogWarn("Node not valid: " << rootName);
        return;
    }

    const kodachi::StringAttribute name = currentNode.getChildByName("name");
    if (!name.isValid()) {
        KdLogWarn("Material network node(" << rootName << ") does not have a valid name attribute");
        return;
    }

    // Networks may by cyclical. Check if the current node has been visited
    if (visitedNodes.emplace(name).second) {
        gb.set(name.getValueCStr(), currentNode);

        const kodachi::GroupAttribute connections =
                                      currentNode.getChildByName("connections");

        // No connections at this node.  Return.
        if (!connections.isValid()) {
            return;
        }

        const int64_t numConnections = connections.getNumberOfChildren();

        // Loop through the connections
        for (int64_t i = 0; i < numConnections; ++i) {
            const kodachi::StringAttribute connection = connections.getChildByIndex(i);

            const kodachi::string_view connValue = connection.getValueCStr();
            const std::size_t pos = connValue.find('@');

            // Visit it
            if (pos != kodachi::string_view::npos) {
                isolateNetworkNodes(connValue.substr(pos + 1),
                                    allNodes,
                                    gb,
                                    visitedNodes);
            }
        }
    }
}


kodachi::GroupAttribute
isolateNetworkNodes(const kodachi::string_view& terminalNodeName,
                    const kodachi::GroupAttribute& materialAttr)
{
    // Don't want to hash on all of the nodes in the network.
    // Instead, only build out a GroupAttribute for the nodes that our root
    // has connections to
    kodachi::GroupAttribute networkNodesAttr;

    const kodachi::GroupAttribute nodesAttr = materialAttr.getChildByName("nodes");

    kodachi::GroupBuilder networkNodesBuilder;
    std::unordered_set<kodachi::StringAttribute, kodachi::AttributeHash> visited;
    isolateNetworkNodes(terminalNodeName,
                        nodesAttr,
                        networkNodesBuilder,
                        visited);
    networkNodesAttr = networkNodesBuilder.build();

    // Get all the parameter values as a per-shader lookup
    const kodachi::GroupAttribute materialParamsAttr =
                          materialAttr.getChildByName("parameters");
    const kodachi::GroupAttribute interfaceParamsAttr =
                           materialAttr.getChildByName("interface");

    kodachi::GroupBuilder gb;
    gb.update(networkNodesAttr);

    if (materialParamsAttr.isValid() && interfaceParamsAttr.isValid()) {
        for (auto child : materialParamsAttr) {
            std::string paramName;
            paramName.reserve(child.name.size() + 4);
            paramName.append(child.name.data(), child.name.size());
            paramName.append(".src");
            //KdLogDebug("Applying material param: " << paramName);

            // Find the param in the material interface and get the
            // shading node name and param
            const kodachi::StringAttribute paramSrcAttr =
                      interfaceParamsAttr.getChildByName(paramName);
            if (paramSrcAttr.isValid()) {
                std::string paramSrc = paramSrcAttr.getValue();
                const size_t pos = paramSrc.find_first_of('.');
                if (pos != std::string::npos) {
                    paramSrc.insert(pos, ".parameters");
                }
                gb.set(paramSrc, child.attribute);
            } else {
                //KdLogDebug("Could not find param: " << paramName);
            }
        }
    }

    return gb.build();
}

struct NodeData
{
    std::string mChildName;
    kodachi::StringAttribute mObjectName;
    kodachi::StringAttribute mNodeName; // Backs the string_view keys in NodeDataMap
    kodachi::StringAttribute mType;
    kodachi::GroupAttribute mParams;
    kodachi::GroupAttribute mConnections;
};

using NodeDataMap = std::unordered_map<kodachi::string_view, NodeData, kodachi::StringViewHash>;

// Converts Katana ramp attributes to Moonray RampMap attributes
kodachi::GroupAttribute
convertRampMapAttrs(const kodachi::GroupAttribute& rampMapAttrs)
{
    const kodachi::FloatAttribute rampKnotsAttr =
            rampMapAttrs.getChildByName("ramp_Knots");

    const kodachi::FloatAttribute rampColorsAttr =
            rampMapAttrs.getChildByName("ramp_Colors");

    kodachi::GroupBuilder gb;
    gb.update(rampMapAttrs);
    gb.del("ramp_Knots");
    gb.del("ramp_Colors");
    gb.set("positions", rampKnotsAttr);
    gb.set("colors", rampColorsAttr);

    return gb.build();
}

// Extracts all relevant data from a material's 'nodes' attribute
std::shared_ptr<NodeDataMap>
createNodeData(const kodachi::string_view& nodePrefix,
               const kodachi::GroupAttribute& nodesAttr,
               const kodachi::string_view& childPrefix)
{
    static const kodachi::StringAttribute kMoonrayAttr("moonray");
    static const kodachi::StringAttribute kRampMapAttr("RampMap");

    NodeDataMap nodeDataMap(nodesAttr.getNumberOfChildren());
    for (const auto node : nodesAttr) {
        const kodachi::GroupAttribute nodeAttr = node.attribute;

        const kodachi::StringAttribute targetAttr = nodeAttr.getChildByName("target");
        if (targetAttr.isValid() && targetAttr != kMoonrayAttr) {
            continue;
        }

        kodachi::StringAttribute nameAttr = nodeAttr.getChildByName("name");
        kodachi::StringAttribute typeAttr = nodeAttr.getChildByName("type");
        kodachi::GroupAttribute parametersAttr = nodeAttr.getChildByName("parameters");
        if (typeAttr == kRampMapAttr) {
            parametersAttr = convertRampMapAttrs(parametersAttr);
        }

        const kodachi::string_view name(nameAttr.getValueCStr());
        const kodachi::string_view type(typeAttr.getValueCStr());

        std::string childName  = kodachi::concat("_", childPrefix, "_", name);

        const std::string materialObjectName =
                kodachi::concat(nodePrefix, childName, "_", type);

        nodeDataMap.emplace(name,
                            NodeData{std::move(childName),
                                     kodachi::StringAttribute(materialObjectName),
                                     std::move(nameAttr),
                                     std::move(typeAttr),
                                     parametersAttr,
                                     nodeAttr.getChildByName("connections")});
    }

    // Sets the params for nodes that have connections to other nodes
    for (auto& node : nodeDataMap) {
        NodeData& nodeData = node.second;
        if (nodeData.mConnections.isValid()) {
            kodachi::GroupBuilder paramsGb;
            paramsGb.update(nodeData.mParams);

            for (const auto connection : nodeData.mConnections) {
                const kodachi::StringAttribute connAttr(connection.attribute);
                kodachi::string_view nodeName(connAttr.getValueCStr());
                const std::size_t pos = nodeName.find('@');
                if (pos != kodachi::string_view::npos) {
                    nodeName.remove_prefix(pos + 1);
                    const auto iter = nodeDataMap.find(nodeName);
                    if (iter != nodeDataMap.end()) {
                        // Moonray supports an attribute having a value and
                        // a binding at the same time. Use a group attribute to
                        // express this.
                        const kodachi::Attribute valueAttr =
                                nodeData.mParams.getChildByName(connection.name);
                        if (valueAttr.isValid()) {
                            paramsGb.set(connection.name,
                                         kodachi::GroupAttribute("value", valueAttr,
                                                                 "bind", iter->second.mObjectName,
                                                                 false));
                        } else {
                            paramsGb.set(connection.name, iter->second.mObjectName);
                        }
                    }
                }
            }

            nodeData.mParams = paramsGb.build();
        }
    }

    return std::make_shared<NodeDataMap>(std::move(nodeDataMap));
}

kodachi::GroupAttribute
buildShaderAttrs(const kodachi::StringAttribute& typeAttr,
                      const kodachi::StringAttribute& nameAttr,
                      const kodachi::GroupAttribute& paramsAttr)
{
    static const std::string kType("type");
    static const std::string kSceneObjectSceneClass("rdl2.sceneObject.sceneClass");
    static const std::string kSceneObjectName("rdl2.sceneObject.name");
    static const std::string kSceneObjectAttrs("rdl2.sceneObject.attrs");
    static const std::string kDisableAliasing("rdl2.sceneObject.disableAliasing");
    static const kodachi::StringAttribute kAttributeSetCELAttr("//*");
    static const kodachi::StringAttribute kRdl2Attr("rdl2");

    kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
    asb.setCEL(kAttributeSetCELAttr);
    asb.setAttr(kType, kRdl2Attr);
    asb.setAttr(kSceneObjectSceneClass, typeAttr);
    asb.setAttr(kSceneObjectName, nameAttr);
    asb.setAttr(kDisableAliasing, kodachi::IntAttribute(true));
    asb.setAttr(kSceneObjectAttrs, paramsAttr);

    return asb.build();
}

class MaterialCache : public FnKat::Util::AttributeKeyedCache<NodeDataMap>
{
public:
    MaterialCache(const kodachi::StringAttribute& stateKeyAttr)
    : mStateKeyAttr(stateKeyAttr)
    {}

    const kodachi::StringAttribute&
    getStateKeyAttr()
    {
        return mStateKeyAttr;
    }

protected:

    std::shared_ptr<NodeDataMap>
    createValue(const FnAttribute::Attribute & iAttr) override
    {
        // Check if caching has been disabled in the KPOP state
        if (mStateKeyAttr.isValid()) {
            static const std::string kGetKPOPState("GetKPOPState");

            const kodachi::GroupAttribute stateAttr =
                    kodachi::AttributeFunctionUtil::run(kGetKPOPState, mStateKeyAttr);

            const kodachi::IntAttribute materialCachingEnabledAttr =
                    stateAttr.getChildByName("materialCachingEnabled");

            if (!materialCachingEnabledAttr.getValue(true, false)) {
                return nullptr;
            }
        }

        const kodachi::GroupAttribute keyAttr(iAttr);
        const kodachi::GroupAttribute nodesAttr = keyAttr.getChildByIndex(0);

        // Use the hash as the prefix for each node name
        const std::string materialHash =
                nodesAttr.getHash().str();

        return createNodeData(nodesAttr.getHash().str(), nodesAttr, keyAttr.getChildNameCStr(0));
    }

    const kodachi::StringAttribute mStateKeyAttr;
};

const std::string kMoonrayMaterial("moonrayMaterial");
const std::string kMoonraySurface("moonraySurface");
const std::string kMoonrayDisplacement("moonrayDisplacement");
const std::string kMoonrayVolume("moonrayVolume");

class KPOPMaterial: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            kodachi::GroupBuilder opArgsGb;
            const kodachi::IntAttribute reuseMaterialsAttr =
                    interface.getAttr("moonrayGlobalStatements.reuse cached materials");

            if (reuseMaterialsAttr.isValid()) {
                opArgsGb.set("isCachingEnabled", reuseMaterialsAttr);
            }

            const kodachi::StringAttribute renderIDAttr =
                    interface.getAttr("kodachi.renderID");

            if (renderIDAttr.isValid()) {
                opArgsGb.set("stateKey", renderIDAttr);
            }

            if (opArgsGb.isValid()) {
                opArgsGb.update(interface.getOpArg(""));
                interface.replaceChildTraversalOp("", opArgsGb.build());
            }

            return;
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and hasattr("rdl2.meta.isMaterialAssignable")})");

        static const kodachi::StringAttribute kDwaBaseMaterialAttr("DwaBaseMaterial");
        static const std::string kDefaultMaterialName = "__defaultMaterial_DwaBaseMaterial";

        static const std::unordered_map<kodachi::string_view, const std::string,
                                 kodachi::StringViewHash> kTerminalLayerAssignmentMap
        {
            { kMoonrayMaterial     , "rdl2.layerAssign.material"     },
            { kMoonraySurface      , "rdl2.layerAssign.material"     },
            { kMoonrayDisplacement , "rdl2.layerAssign.displacement" },
            { kMoonrayVolume       , "rdl2.layerAssign.volumeShader" }
        };

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const std::string inputLocationPath = interface.getInputLocationPath();

        // auto instancing
        const kodachi::IntAttribute autoInstancingEnabledAttr =
                interface.getAttr("rdl2.meta.autoInstancing.enabled");

        const bool autoInstancingEnabled = autoInstancingEnabledAttr.isValid();

        // For live renders, when caching is enabled, we only create cached
        // materials during initial scene build, and then we create
        // location-specific materials when applying deltas
        const kodachi::IntAttribute cachingEnabledAttr =
                        interface.getOpArg("isCachingEnabled");

        bool cachingEnabled = cachingEnabledAttr.getValue(true, false);

        const kodachi::GroupAttribute materialAttr = interface.getAttr("material");
        if (materialAttr.isValid()) {
            const kodachi::Attribute stateKeyAttr = interface.getOpArg("stateKey");

            const kodachi::GroupAttribute terminalsAttr =
                    materialAttr.getChildByName("terminals");

            // separate the nodes by terminal type. This way we can create
            // a hash per terminal when caching is enabled. If not caching,
            // we will potentially create child locations multiple times, which
            // isn't harmful, just not as efficient.
            for (const auto terminal : terminalsAttr) {
                const auto terminalIter =
                        kTerminalLayerAssignmentMap.find(terminal.name);

                if (terminalIter == kTerminalLayerAssignmentMap.end()) {
                    // not a supported terminal type
                    continue;
                }

                const kodachi::StringAttribute terminalNodeNameAttr(terminal.attribute);
                if (!terminalNodeNameAttr.isValid()) {
                    // not a valid terminal name
                    continue;
                }
                const kodachi::string_view terminalNodeName(terminalNodeNameAttr.getValueCStr());

                const kodachi::GroupAttribute isolatedNetworkNodesAttr =
                        isolateNetworkNodes(terminalNodeName, materialAttr);

                std::shared_ptr<NodeDataMap> nodeDataMap;

                if (cachingEnabled) {
                    // If caching hasn't been disabled in the KPOP State for
                    // this render, this will get or create the cached material
                    const kodachi::GroupAttribute keyAttr(
                            terminal.name.data(), isolatedNetworkNodesAttr, false);

                    nodeDataMap = getMaterialCache(stateKeyAttr).getValue(keyAttr);
                }

                // Either caching has been disabled for the scene, or we are
                // applying a delta, so make materials specific to this location
                if (!nodeDataMap) {
                    const std::string nodePrefix = inputLocationPath + '/';

                    nodeDataMap = createNodeData(nodePrefix, isolatedNetworkNodesAttr, terminal.name);
                }

                // Create a child location for each node
                for (auto& node : *nodeDataMap) {
                    NodeData& nodeData = node.second;

                    const kodachi::GroupAttribute childAttrs =
                            buildShaderAttrs(nodeData.mType,
                                                  nodeData.mObjectName,
                                                  nodeData.mParams);

                    interface.createChild(nodeData.mChildName,
                                          "AttributeSet", childAttrs);
                }

                const auto nodeIter = nodeDataMap->find(terminalNodeName);
                if (nodeIter != nodeDataMap->end()) {
                    interface.setAttr(terminalIter->second,
                                      nodeIter->second.mObjectName, false);

                    // instance.ID attrs
                    if (autoInstancingEnabled) {
                        interface.setAttr(
                                kodachi::concat("rdl2.meta.autoInstancing.attrs.material.", terminal.name),
                                nodeIter->second.mObjectName);
                    }
                }
            }
        } else {
            bool useDefaultMaterial = true;

            // Only apply the default base material to material-less meshes whose
            // children also don't have materials
            if (interface.getAttr("rdl2.meta.isMesh").isValid()) {
                const auto potentialChildrenSamples =
                        interface.getPotentialChildren().getSamples();

                if (potentialChildrenSamples.isValid()) {
                    for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                        interface.prefetch(childName);
                    }

                    for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                        const auto isPartAttr =
                                interface.getAttr("rdl2.meta.isPart", childName);

                        const auto childMaterialAttr =
                                interface.getAttr("material", childName);

                        if (isPartAttr.isValid() && childMaterialAttr.isValid()) {
                            // A child has a material assignment.  Don't use the
                            // default material
                            useDefaultMaterial = false;

                            // No point in checking the rest of the children.
                            // Break early.
                            break;
                        }
                    }
                }
            } else if (interface.getAttr("rdl2.meta.isPart").isValid() ||
                        kodachi::StringAttribute(interface.getAttr("rdl2.meta.kodachiType")) == "renderer procedural") {
                useDefaultMaterial = false;
            }

            if (useDefaultMaterial) {
                KdLogWarn("Location does not have a 'material' attribute. Applying a default DwaBaseMaterial.");
            
                // Create a default material
                const std::string materialName = cachingEnabled ?
                        kDefaultMaterialName :
                        kodachi::concat(interface.getInputLocationPath(), "/", kDefaultMaterialName);

                kodachi::StringAttribute materialNameAttr(materialName);

                const kodachi::GroupAttribute childAttrs =
                        buildShaderAttrs(kDwaBaseMaterialAttr,
                                              materialNameAttr,
                                              kodachi::GroupAttribute{});

                interface.createChild(kDefaultMaterialName, "AttributeSet", childAttrs);

                const auto terminalIter = kTerminalLayerAssignmentMap.find(kMoonrayMaterial);
                interface.setAttr(terminalIter->second, materialNameAttr, false);

                // instance.ID attrs
                if (autoInstancingEnabled) {
                    interface.setAttr(
                            kodachi::concat("rdl2.meta.autoInstancing.attrs.material.", kMoonrayMaterial),
                            materialNameAttr);
                }
            }
        }

    }

    static void flush()
    {
        boost::lock_guard<boost::shared_mutex> lock(sMaterialCacheMutex);
        sMaterialCaches.clear();
    }

private:

    using MaterialCachePtr = std::unique_ptr<MaterialCache>;
    using MaterialCacheVector = std::vector<MaterialCachePtr>;

    // Under the assumption that there won't be many cases where a process has
    // multiple renders going, we are storing the caches in a vector and
    // doing a linear search, since almost always the 0th entry will be the
    // only cache.
    static MaterialCache& getMaterialCache(const kodachi::StringAttribute& stateKeyAttr)
    {
        const auto pred = [&stateKeyAttr](MaterialCachePtr& cache)
                {
                    return cache->getStateKeyAttr() == stateKeyAttr;
                };

        {
            boost::shared_lock<boost::shared_mutex> sharedLock(sMaterialCacheMutex);
            const auto iter = std::find_if(sMaterialCaches.begin(), sMaterialCaches.end(), pred);
            if (iter != sMaterialCaches.end()) {
                // the cache already exists so return it
                return *(*iter);
            }
        }

        boost::lock_guard<boost::shared_mutex> uniqueLock(sMaterialCacheMutex);
        // check that the cache hasn't been made by another thread that
        // grabbed the lock_guard before us
        const auto iter = std::find_if(sMaterialCaches.begin(), sMaterialCaches.end(), pred);

        if (iter == sMaterialCaches.end()) {
            // Its this thread's job to make the cache
            sMaterialCaches.emplace_back(new MaterialCache(stateKeyAttr));
            return *sMaterialCaches.back();
        } else {
            // Another thread beat us to it, so return the existing cache
            return *(*iter);
        }
    }

    static boost::shared_mutex sMaterialCacheMutex;
    static MaterialCacheVector sMaterialCaches;
};

boost::shared_mutex KPOPMaterial::sMaterialCacheMutex;
KPOPMaterial::MaterialCacheVector KPOPMaterial::sMaterialCaches;

class KPOPLightFilterList: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and @rdl2.meta.kodachiType=="light"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // prefetch children so we can check for any child lightfilter locations
        const auto potentialChildrenSamples = kodachi::StringAttribute(
                interface.getPotentialChildren()).getSamples();

        if (potentialChildrenSamples.isValid()) {
            for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                interface.prefetch(childName);
            }

            static const kodachi::StringAttribute kMuteEmptyAttr("muteEmpty");

            const std::string inputLocationPath = interface.getInputLocationPath();
            kodachi::StringVector lightFilters;

            // get the sceneObject name of all child lightfilter locations
            for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                static const kodachi::StringAttribute kLightFilterAttr("light filter");
                const bool isLightFilter =
                        interface.getAttr("rdl2.meta.kodachiType", childName) == kLightFilterAttr;

                if (isLightFilter) {
                    const kodachi::StringAttribute lightFilterMuteStateAttr =
                            interface.getAttr("info.light.muteState", childName);

                    if (lightFilterMuteStateAttr == kMuteEmptyAttr) {
                        lightFilters.push_back(kodachi::concat(inputLocationPath, "/", childName));
                    }
                }
            }

            if (!lightFilters.empty()) {
                interface.setAttr("rdl2.sceneObject.attrs.light_filters",
                        kodachi::ZeroCopyStringAttribute::create(std::move(lightFilters)), false);
            }
        }
    }
};

// Implicit resolver that reaplaces light map_shader_material name with the material.
// This is then copied to the rdl2 nodes for the lights. This should be expanded if other
// inputs to light shaders are supported.
class LightInputResolve : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kLight("light");
        if (interface.getAttr("type") != kLight) return;
        interface.stopChildTraversal();

        const kodachi::GroupAttribute params = interface.getAttr("material.moonrayLightParams");
        if (not params.isValid()) return;

        std::string map_shader_shader =
            kodachi::StringAttribute(params.getChildByName("map_shader_shader")).getValue("", false);
        if (map_shader_shader.empty()) return;

        std::string map_shader_material =
            kodachi::StringAttribute(params.getChildByName("map_shader_material")).getValue("", false);
        if (map_shader_material.empty()) {
            // use the material from the geometry
            std::string geometry =
                kodachi::StringAttribute(params.getChildByName("geometry")).getValue("", false);
            if (geometry.empty()) return;
            map_shader_material = kodachi::StringAttribute(
                GetGlobalAttr(interface, "materialAssign", geometry)).getValue("", false);
            if (map_shader_material.empty()) return;
        }

        kodachi::GroupAttribute materialAttr(interface.getAttr("material", map_shader_material));
        if (not materialAttr.isValid()) return;

        interface.setAttr("material.moonrayLightParams.map_shader_material", materialAttr);
    }
};

class KPOPLight: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and @rdl2.meta.kodachiType=="light"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // get the light material
        kodachi::GroupAttribute materialAttr = interface.getAttr("material");

        const kodachi::StringAttribute terminalNodeNameAttr =
                materialAttr.getChildByName("terminals.moonrayLight");

        if (!terminalNodeNameAttr.isValid()) {
            kodachi::ReportWarning(interface,
                    "Light location does not have a 'moonrayLight' terminal");
            return;
        }

        const kodachi::string_view terminalNodeName =
                terminalNodeNameAttr.getValueCStr();

        const std::string inputLocationPath = interface.getInputLocationPath();

        kodachi::GroupAttribute networkNodesAttr =
                isolateNetworkNodes(terminalNodeName, materialAttr);

        // caching nyi
        std::shared_ptr<NodeDataMap> nodeDataMap(
            createNodeData(inputLocationPath + '/', networkNodesAttr, "moonrayLight"));

        kodachi::GroupBuilder attrsGb;
        attrsGb.setGroupInherit(false)
            .update(interface.getAttr("rdl2.sceneObject.attrs"));

        std::string map_shader_shader;
        kodachi::GroupAttribute map_shader_material;
        for (auto& node : *nodeDataMap) {
            NodeData& nodeData = node.second;
            if (node.first == terminalNodeName) {
                // the terminal node in the material is written to the light

                interface.setAttr("rdl2.sceneObject.sceneClass", nodeData.mType, false);
                const std::string objectName = kodachi::concat(
                    inputLocationPath, "_", nodeData.mType.getValueCStr());
                interface.setAttr("rdl2.sceneObject.name",
                                  kodachi::StringAttribute(objectName), false);
                size_t n(nodeData.mParams.getNumberOfChildren());
                for (size_t i = 0; i < n; ++i) {
                    std::string name(nodeData.mParams.getChildName(i));
                    kodachi::Attribute value(nodeData.mParams.getChildByIndex(i));
                    // extract the non-network connection attributes for meshlight
                    if (name == "map_shader_shader")
                        map_shader_shader = kodachi::StringAttribute(value).getValue();
                    else if (name == "map_shader_material")
                        map_shader_material = value;
                    else
                        attrsGb.set(name, value);
                }
            } else {
                // other nodes in the material
                const kodachi::GroupAttribute childAttrs =
                    buildShaderAttrs(nodeData.mType,
                                     nodeData.mObjectName,
                                     nodeData.mParams);
                interface.createChild(nodeData.mChildName,
                                      "AttributeSet", childAttrs);
            }
        }
        // try non-network shader connection for mesh light
        // This was already tested for in LightInputResolve, if a valid group was set
        // then the map_shader was specified
        if (map_shader_material.isValid()) {
            networkNodesAttr = isolateNetworkNodes(map_shader_shader, map_shader_material);
            nodeDataMap = createNodeData(inputLocationPath + '/', networkNodesAttr, "map_shader");
            for (auto& node : *nodeDataMap) {
                NodeData& nodeData = node.second;
                const kodachi::GroupAttribute childAttrs =
                    buildShaderAttrs(nodeData.mType,
                                     nodeData.mObjectName,
                                     nodeData.mParams);
                interface.createChild(nodeData.mChildName,
                                      "AttributeSet", childAttrs);
                if (node.first == map_shader_shader)
                    attrsGb.set("map_shader", kodachi::StringAttribute(nodeData.mObjectName));
            }
        }

        const kodachi::StringAttribute muteStateAttr(interface.getAttr("info.light.muteState"));
        // muteState will be set by implicit resolvers and may be
        // "muteEmpty", "muteInherited", "muteInheritInactive" or "muteLocal".
        // We can say that the light is muted unless it is set to "muteEmpty".
        // Note that we cannot use the actual "mute" attribute as it doesn't get
        // set in the case of light rig inheritance.
        static const kodachi::StringAttribute kMuteEmptyAttr("muteEmpty");
        if (muteStateAttr.isValid() && muteStateAttr != kMuteEmptyAttr)
            attrsGb.set("on", kodachi::IntAttribute(false));

        interface.setAttr("rdl2.sceneObject.attrs", attrsGb.build(), false);
    }
};

class KPOPLightFilter: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and @rdl2.meta.kodachiType=="light filter"})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::GroupAttribute materialAttr = interface.getAttr("material");

        const kodachi::StringAttribute terminalNodeNameAttr =
                materialAttr.getChildByName("terminals.moonrayLightfilter");

        if (!terminalNodeNameAttr.isValid()) {
            kodachi::ReportWarning(interface, "Light filter location does not have a 'moonrayLightfilter' terminal");
            return;
        }

        const kodachi::string_view terminalNodeName =
                terminalNodeNameAttr.getValueCStr();

        const kodachi::GroupAttribute networkNodesAttr =
                isolateNetworkNodes(terminalNodeName, materialAttr);

        const kodachi::GroupAttribute terminalNodeAttr =
                networkNodesAttr.getChildByName(terminalNodeName);

        const kodachi::StringAttribute typeAttr =
                terminalNodeAttr.getChildByName("type");

        const kodachi::GroupAttribute paramsAttr =
                terminalNodeAttr.getChildByName("parameters");

        interface.setAttr("rdl2.sceneObject.sceneClass", typeAttr, false);

        const std::string objectName = kodachi::concat(
                interface.getInputLocationPath(), "_", typeAttr.getValueCStr());
        interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);

        kodachi::GroupBuilder attrsGb;
        attrsGb.setGroupInherit(false)
               .update(paramsAttr)
               .update(interface.getAttr("rdl2.sceneObject.attrs"));

        interface.setAttr("rdl2.sceneObject.attrs", attrsGb.build(), false);
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPMaterial);
DEFINE_KODACHIOP_PLUGIN(KPOPLightFilterList);
DEFINE_KODACHIOP_PLUGIN(LightInputResolve);
DEFINE_KODACHIOP_PLUGIN(KPOPLight);
DEFINE_KODACHIOP_PLUGIN(KPOPLightFilter);

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPMaterial, "KPOPMaterial", 0, 1);
    REGISTER_PLUGIN(KPOPLightFilterList, "KPOPLightFilterList", 0, 1);
    REGISTER_PLUGIN(LightInputResolve, "LightInputResolve", 0, 1);
    REGISTER_PLUGIN(KPOPLight, "KPOPLight", 0, 1);
    REGISTER_PLUGIN(KPOPLightFilter, "KPOPLightFilter", 0, 1);
}

