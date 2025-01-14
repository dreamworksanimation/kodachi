// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

// Local
#include "IdPassManager.h"
#include "util/AttrUtil.h"

// kodachi
#include <kodachi/attribute/Attribute.h>

// TBB
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_unordered_set.h>
#include <tbb/concurrent_vector.h>
#include <tbb/concurrent_hash_map.h>

// scene_rdl2
#include <scene_rdl2/common/math/Viewport.h>
#include <scene_rdl2/scene/rdl2/Types.h>
namespace arras {
namespace rdl2 {
class Attribute;
class SceneContext;
class SceneObject;
class UserData;
}
}

#include <vector>
#include <utility>

namespace kodachi_moonray {

/**
 * Helper class for populating an rdl2::SceneContext from Kodachi LocationData
 */

class MoonrayRenderState
{
public:
    using SceneObjectPtr = std::shared_ptr<arras::rdl2::SceneObject>;
    using SceneObjectWeakPtr = std::weak_ptr<arras::rdl2::SceneObject>;

    MoonrayRenderState(const kodachi::GroupAttribute& rootAttrs);
    ~MoonrayRenderState();

    void useNewSceneContext();
    void useExternalSceneContext(arras::rdl2::SceneContext* scp);

    void initializeKodachiRuntimeObject(const kodachi::GroupAttribute& opTreeAttr);

    void processLocation(const kodachi::StringAttribute& locationPathAttr,
                         const kodachi::GroupAttribute& locationAttributes);

    void deleteLocation(const kodachi::StringAttribute& locationPathAttr);

    // Call after initial scene build or processing of optree deltas is complete
    void processingComplete();

    bool isLiveRender() const { return mIsLiveRender; }
    bool skipRender() const { return mSkipRender; }
    bool kodachiGeometryUseRuntime() { return mKodachiRuntime != nullptr; }

    IdPassManager& getIdPassManager() { return mIdPassManager; }

    void loadRdlSceneFile(const std::string& sceneFile);

    void writeSceneToFile(const std::string& filePath);
    void writeCryptomatteManifest(const std::string& filePath);

    const arras::math::HalfOpenViewport& getApertureWindow() const { return mApertureWindow; }
    const arras::math::HalfOpenViewport& getRegionWindow() const { return mRegionWindow; }
    bool isROIEnabled() const { return mIsROIEnabled; }
    const arras::math::HalfOpenViewport& getSubViewport() const { return mSubViewport; }

private:

    MoonrayRenderState(const MoonrayRenderState&) = delete;
    MoonrayRenderState& operator=(const MoonrayRenderState&) = delete;

    SceneObjectPtr getOrCreateSceneObject(const kodachi::string_view& locationPath,
                                          const kodachi::StringAttribute& classNameAttr,
                                          const std::string& objectName,
                                          bool disableAliasing = false);

    /**
     * Returns the SceneObject with the specified name, or nullptr if not found.
     * type is for printing error message so it can say something more specific than "object"
     */
    arras::rdl2::SceneObject* getSceneObject(const kodachi::string_view& objectName,
                                const char* type);

    /**
     * Since SceneObject's can't be deleted, instead hide them.
     */
    void hideSceneObject(arras::rdl2::SceneObject* sceneObject);

    /**
     * Resets the value of the object's attribute to default
     */
    void resetAttributeToDefault(const SceneObjectPtr& obj,
                                 const arras::rdl2::Attribute& attr);

    /**
     * Stores the SceneGraph location of a SceneObject to be set as an attribute.
     * Since the SceneObject may not have been created yet, it will be processed
     * once the rest of the SceneGraph has been traversed.
     */
    void addDeferredConnection(const SceneObjectPtr& sourceObject,
                               const arras::rdl2::Attribute& attr,
                               kodachi::StringAttribute targetLocation);

    void processPotentialInstanceSources();

    void processDeferredConnections();

    void addDeferredLayerAssignment(kodachi::GroupAttribute assignmentAttr);
    void processDeferredLayerAssignments();

    void addDeferredGeoSetAssignment(kodachi::GroupAttribute assignmentAttr);
    void processDeferredGeoSetAssignments();

    void addDeferredRdlArchiveUpdate(const std::string& rdlFileName);
    void processDeferredRdlArchiveUpdates();

    void addDeferredRenderOutputCreation(const std::string& locationPath,
                                         kodachi::GroupAttribute sceneObjectAttr);
    void processDeferredRenderOutputCreations();

    void addTraceSetEntries(const SceneObjectPtr& traceSet, const std::string& location,
                            const kodachi::StringAttribute& baked);
    void processTraceSetEntries();

    void registerConnection(const SceneObjectPtr& source,
                            const arras::rdl2::Attribute* sourceAttr,
                            const arras::rdl2::SceneObject* target);

    void removeConnection(const SceneObjectPtr& source,
                          const arras::rdl2::Attribute* sourceAttr,
                          const arras::rdl2::SceneObject* target);

    void updateConnection(const SceneObjectPtr& source,
                          const arras::rdl2::Attribute* sourceAttr,
                          const arras::rdl2::SceneObject* oldTarget,
                          const arras::rdl2::SceneObject* newTarget);

    /**
     * For cases where the scene object representing a location has changed
     * we need to update any bindings and connection to the new object
     */
    void addDeferredConnectionTargetReplacement(arras::rdl2::SceneObject* oldTarget,
                                                SceneObjectPtr newTarget);
    void processDeferredConnectionTargetReplacements();

    void addDeferredIdRegistration(kodachi::GroupAttribute registrationAttr);
    void processDeferredIdRegistrations();

    /***************************************************************************
     * The setAttrValue methods are used to set the value for an attribute
     * on a SceneObject. It handles the conversion from the attribute's name
     * and kodachi::Attribute value, to the rdl2::AttributeKey and rdl2::Value
     * expected by the SceneObject
     */

    // Finds the rdl2::attribute from the SceneObject, and constructs the
    // AttributeKey based on the rdl2::Attributes type.
    // Also handles deferring of bindings
    void setAttrValue(const SceneObjectPtr& obj,
                      const arras::rdl2::Attribute* attribute,
                      kodachi::Attribute value,
                      float shutterOpen, float shutterClose);

    // Attempts to convert the kodachi::Attribute to type specified by the
    // AttributeKey, then sets it on the object.
    template <class T>
    void setAttrValue(const SceneObjectPtr& obj,
                      const arras::rdl2::AttributeKey<T>& attributeKey,
                      kodachi::DataAttribute attr,
                      float shutterOpen, float shutterClose);

    arras::rdl2::SceneObject* getBinding(const SceneObjectPtr& obj,
                                         const arras::rdl2::Attribute& attr);

    void setBinding(const SceneObjectPtr& obj,
                    const arras::rdl2::Attribute& attr,
                    arras::rdl2::SceneObject* targetObject);

    void resetBinding(const SceneObjectPtr& obj, const
                      arras::rdl2::Attribute& attr);

    void setSceneObjectAttributes(const SceneObjectPtr& obj,
                                  const kodachi::GroupAttribute attrs,
                                  float shutterOpen, float shutterClose);

    arras::rdl2::SceneObjectVector createInstanceUserData(const std::string& locationPath,
                                                          const kodachi::GroupAttribute& arbAttrs);

    /**************************************************************************/

    using SceneContextPtr = std::unique_ptr<arras::rdl2::SceneContext,
                                            std::function<void(arras::rdl2::SceneContext*)>>;

    // The actual Rdl2 scene
    SceneContextPtr mSceneContext;

    bool mIsLiveRender = false;
    bool mSkipRender = false;

    /**************************************************************************/

    using AttributeIdMap = std::unordered_map<const arras::rdl2::Attribute*, std::size_t>;

    // Pair of Attribute and AttributeId
    using Attribute = std::pair<const arras::rdl2::Attribute*, std::size_t>;

    /**
     * Map of an Attributes name and aliases to the attribute
     * The string_view keys are created from the strings owned by the Attribute.
     *
     * This allows us to avoid string allocation for every attribute being set
     * just to use it to lookup the Attribute* in the SceneObject.
     *
     * Not thread-safe as expectation is that it will only be read-only.
     */
    using AttributeLookupMap = std::unordered_map<kodachi::string_view, Attribute,
                                                  kodachi::StringViewHash>;

    using Rdl2AttrVec = std::vector<const arras::rdl2::Attribute*>;

    // Attribute Map, number of attributes in SceneClass
    using SceneClassData = std::tuple<AttributeLookupMap, AttributeIdMap, Rdl2AttrVec>;

    const SceneClassData& getSceneClassData(const arras::rdl2::SceneClass& sceneClass);

    // Stored attributes for each SceneClass
    using SceneClassDataMap = tbb::concurrent_unordered_map<const arras::rdl2::SceneClass*,
                                                                 const SceneClassData>;

    SceneClassDataMap mSceneClassDataMap;

    /**************************************************************************/

    // Cache of SceneClasses to allow creation of a scene object without
    // converting the SceneClass StringAttribute to a string every time
    tbb::concurrent_unordered_map<kodachi::StringAttribute,
                                  const arras::rdl2::SceneClass*,
                                  kodachi::AttributeHash> mSceneClassMap;

    /**************************************************************************/

    using SceneClassSceneObjectPair = std::pair<kodachi::StringAttribute,
                                                SceneObjectPtr>;

    using SceneObjectHashMap = tbb::concurrent_hash_map<kodachi::string_view,
                                                        SceneClassSceneObjectPair,
                                                        kodachi::StringViewHash>;

    // When processing Scenegraph locations, the resulting SceneObject can
    // register itself with its SceneObject name or its scenegraph location
    // This allows us to resolve connections without knowing the final name
    // of the target SceneObject ahead of time.
    SceneObjectHashMap mActiveSceneObjects;
    SceneObjectHashMap mActiveInstanceSourceSceneObjects;

    /**************************************************************************/

    // pair of <index into Rdl2AttrVec, Attribute Hash>
    using SetValueHash = std::pair<std::size_t, kodachi::Hash>;

    // Contains the hashes of all non-default values for a SceneObject
    using SetValueHashVec = std::vector<SetValueHash>;

    using SetValueHashMap = tbb::concurrent_hash_map<arras::rdl2::SceneObject*, SetValueHashVec>;

    SetValueHashMap mSetValueHashMap;

    /**************************************************************************
     * For live renders, we need to keep track of SceneObject attributes that
     * point to other SceneObjects (we'll call these connections). This is for
     * cases where the SceneObject representing a location changes, then we have
     * to update all of the connections pointing at the old object to point
     * at the new object.
     **************************************************************************/

    // SceneObject and its attribute to be connected
    using AttributeConnection = std::pair<SceneObjectWeakPtr, const arras::rdl2::Attribute*>;
    using AttributeConnectionVec = std::vector<AttributeConnection>;

    // Map of SceneObject to all the connections pointed at it
    using ReverseConnectionsHashMap = tbb::concurrent_hash_map<
            const arras::rdl2::SceneObject*, AttributeConnectionVec>;

    ReverseConnectionsHashMap mReverseConnections;

    // Pairs of < SceneObject to be replaced, replacement SceneObject>
    tbb::concurrent_vector<std::pair<arras::rdl2::SceneObject*, SceneObjectPtr>> mDeferredConnectionReplacements;

    /**************************************************************************/

    // map of unique ID to pair<instance source location, hasReferences>
    using InstanceIdMap = tbb::concurrent_hash_map<kodachi::StringAttribute,
                                                   std::pair<kodachi::StringAttribute, bool>,
                                                   kodachi::AttributeHash>;
    InstanceIdMap mInstanceIdMap;

    // When we have seen an instance.ID for the second time, we need to do additional
    // processing to convert the first location into an instance source
    tbb::concurrent_unordered_map<kodachi::StringAttribute, kodachi::GroupAttribute, kodachi::AttributeHash> mPotentialInstanceSourceData;

    /**************************************************************************/

    // Deferred SceneObject* connections
    using DeferredConnection = std::tuple<const SceneObjectPtr,
                                          const arras::rdl2::Attribute* const,
                                          const kodachi::StringAttribute>;

    tbb::concurrent_vector<DeferredConnection> mDeferredConnections;

    /**************************************************************************/

    tbb::concurrent_vector<kodachi::GroupAttribute> mDeferredLayerAssignments;
    tbb::concurrent_vector<kodachi::GroupAttribute> mDeferredGeoSetAssignments;
    tbb::concurrent_vector<std::pair<std::string, kodachi::GroupAttribute>> mDeferredRenderOutputCreations;
    tbb::concurrent_vector<std::string> mDeferredRdlArchiveUpdates;

    using TraceSetEntries = std::tuple<arras::rdl2::SceneObject*,
                                       const std::string, // location
                                       const kodachi::StringAttribute>;
    tbb::concurrent_vector<TraceSetEntries> mTraceSetEntries;

    // During the initial scene build and any further updates, we may be given
    // the data for a location more than once, but we only need to process it
    // the first time we see it.
    tbb::concurrent_unordered_set<const arras::rdl2::SceneObject*> mProcessedSceneObjects;

    IdPassManager mIdPassManager;
    tbb::concurrent_vector<kodachi::GroupAttribute> mDeferredIdRegistrations;

    tbb::concurrent_vector<std::pair<std::string, float>> mCryptomatteObjectIds;
    tbb::concurrent_vector<std::pair<std::string, float>> mCryptomatteMaterialIds;

    arras::math::HalfOpenViewport mApertureWindow;
    arras::math::HalfOpenViewport mRegionWindow;
    arras::math::HalfOpenViewport mSubViewport;
    bool mIsROIEnabled = false;

    arras::rdl2::SceneObject* mKodachiRuntime = nullptr;

    int mMachineId = -1;
    int mNumMachines = -1;
};

} // namespace kodachi_moonray

