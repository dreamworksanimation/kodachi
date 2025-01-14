// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// local
#include <kodachi/KodachiRuntime.h>

// system
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <string>

namespace tbb {
class task;
}

namespace kodachi {

/**
 * The Traversal (Traverser is probably a more accurate name but it's too late),
 * fully expands the scenegraph below a given root location and holds onto the
 * LocationData for each location that it cooks until retrieved by the user.
 *
 * Expansion is parallelized by default, but if a location has the
 * 'kodachi.parallelTraversal' attribute set to 0, then the locations children
 * will be cooked on the same thread as the original location. Setting this
 * attribute on every location causes the entire scene graph to be cooked
 * using a single thread.
 *
 * Foundry has discussed creating expansion strategies and helper functions
 * for the upcoming Geolib MT in Katana 3.5 that could replace most of this
 * class's functionality.
 */
class Traversal
{
public:
    /*
     * The name of the attribute used to check if parallel traversal
     * has been disabled for a location
     */
    static const std::string kParallelTraversal;

    enum class PartialLiveRenderMethod : int
    {
        None = 0,
        Include,
        Exclude,
    };

    enum class State
    {
        INITIALIZING,
        RUNNING,
        COMPLETE,
        MONITORING, // Only for MonitoringTraversal
        PARTIAL_MONITORING, // Only for MonitoringTraversal
    };

    Traversal(const KodachiRuntime::Client::Ptr& kodachiClient);

    /**
     * Convenience constructor that creates the KodachiClient from the
     * provided runtime and op.
     */
    Traversal(const KodachiRuntime::Ptr& runtime,
              const kodachi::KodachiRuntime::Op::Ptr& cookOp);

    virtual ~Traversal();

    /**
     * These methods return LocationData stored during the scene graph
     * expansion. getLocation() pops the oldest entry, and getLocations()
     * returns all stored entries and then clears them.
     */
    KodachiRuntime::LocationData getLocation();
    virtual std::vector<KodachiRuntime::LocationData> getLocations();

    /**
     * The location to begin travering from. Defaults to '/root'
     */
    void setRootLocationPath(std::string rootPath);
    const std::string& getRootLocationPath() const { return mRootLocationPath; }

    /**
     * Returns false when scene graph expansion is complete and all LocationData
     * has been retrieved.
     */
    virtual bool isValid() const;

protected:
    void initialize();

    virtual void onTraversalComplete();

    // Optional callback that is called before processing a location.
    // Function should return true if the function should also be called by
    // its child locations
    using PreCookCallback = std::function<bool(const std::string&)>;
    virtual PreCookCallback createPreCookCallback();

    // Return true if the runtime should evict after cooking a location
    virtual bool evictAfterCook() const;

    void addData(const KodachiRuntime::LocationData& data);

    bool mExcludeLocations = false;
    std::atomic<State> mState;

    std::string mRootLocationPath;
    KodachiRuntime::Client::Ptr mCookClient;

    std::condition_variable mLocationDataCv;
    mutable std::mutex mLocationDataMutex;
    std::vector<KodachiRuntime::LocationData> mLocationData;

    tbb::task* mRootTask = nullptr;

    class ParallelTraversalTask;
};

// Allows for multi-threaded processing of optree deltas after initial traversal
class MonitoringTraversal : public Traversal
{
public:
    MonitoringTraversal(const kodachi::KodachiRuntime::Ptr& runtime,
                        const kodachi::KodachiRuntime::Op::Ptr& cookOp,
                        const kodachi::KodachiRuntime::Op::Ptr& monitorOp);

    std::vector<KodachiRuntime::LocationData> getLocations() override;

    bool isValid() const override;

    void applyOpTreeDeltas(const kodachi::GroupAttribute& deltasAttr,
                           bool doPartialLiveRender = false,
                           bool excludeLocations = false);

    // Set if only interested in location updates of a specific type
    void setLeafType(const kodachi::StringAttribute& leafTypeAttr);

protected:
    void onTraversalComplete() override;
    PreCookCallback createPreCookCallback() override;
    bool evictAfterCook() const override;

    // returns true if the location exists
    bool monitorLocation(const std::string& location);

    // Takes the thread-local data structures from the Traversal and prepares
    // them to be used for multi-threaded optree delta processing
    void initializeOpTreeDeltaProcessing(bool doPartialMonitor = false, bool excludeLocations = false);

    // <original potentialChildren value, latest potentialChildren value>
    using PotentialChildrenAttrs =
            std::pair<kodachi::StringAttribute, kodachi::StringAttribute>;

    // Map of active locations for the thread-local client
    // to the last seen potentialChildren attribute
    using ActiveLocationsMap = std::unordered_map<kodachi::StringAttribute,
                                                  PotentialChildrenAttrs,
                                                  kodachi::AttributeHash>;
    ActiveLocationsMap& getActiveLocationsMap();

    tbb::enumerable_thread_specific<
        std::pair<tbb::tbb_thread::id, ActiveLocationsMap>> mActiveLocations;

    kodachi::StringAttribute mLeafTypeAttr;
    KodachiRuntime::Client::Ptr mMonitorClient;

    // The Traversal uses tbb::tasks to spread work across all available threads.
    // Since we don't have a way to ensure that all threads that participated
    // in the Traversal will be used for processing deltas, we will extract
    // the thread-local data from the Traversal and store it all together
    struct GeolibClientStruct
    {
        tbb::tbb_thread::id mThreadId;
        internal::GeolibRuntime::Client::Ptr mMonitorClient;
        internal::GeolibRuntime::Client::Ptr mCookClient;
        FnGeolibCommitId mLastSyncedCommitId;
        ActiveLocationsMap mActiveLocations;
    };

    // Determines if there are any potential children that haven't been set
    // active on the client, and sets them active.
    static void monitorUnmonitoredChildren(
            const kodachi::StringAttribute& locationPathAttr,
            const internal::GeolibRuntime::LocationData& locationData,
            GeolibClientStruct& geolibClient,
            ActiveLocationsMap::iterator& iter);

    // Cooks the location and all descendants on a single thread until exhausted
    static void cookLocationAndChildren(
            const kodachi::StringAttribute& locationPathAttr,
            const kodachi::internal::GeolibRuntime::Client::Ptr& cookClient,
            std::vector<kodachi::KodachiRuntime::LocationData>& locationData);

    std::vector<GeolibClientStruct> mGeolibClients;
};

} // namespace kodachi

