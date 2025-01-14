// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Traversal.h"

#include <kodachi/TaskArena.h> // @deprecated
#include <kodachi/logging/KodachiLogging.h>

#include <tbb/task.h>
#include <tbb/parallel_for_each.h>

#include <stack>
#include <thread>

namespace {
KdLogSetup("KodachiTraversal");

using LocationStack = std::stack<std::pair<std::string, bool>>;

void
pushChildrenToStack(LocationStack& stack,
                    const std::string& parentName,
                    const kodachi::KodachiRuntime::LocationData& locationData,
                    bool executeCallbackOnChildren)
{
    const kodachi::StringAttribute potentialChildrenAttr =
            locationData.getPotentialChildren();
    const auto potentialChildren = potentialChildrenAttr.getNearestSample(0.f);

    for (const kodachi::string_view childName : potentialChildren) {
        stack.push({kodachi::concat(parentName, "/", childName), executeCallbackOnChildren});
    }
}

inline bool
isParallelTraversalEnabled(const kodachi::KodachiRuntime::LocationData& locationData)
{
    const kodachi::IntAttribute parallelTraversalAttr =
                     locationData.getAttrs().getChildByName(
                             kodachi::Traversal::kParallelTraversal);

    return parallelTraversalAttr.getValue(true, false);
}

class TraversalCompletionTask : public tbb::task
{
public:
    TraversalCompletionTask(std::function<void()> callback)
    :   mCallback(callback)
    {}

    tbb::task* execute() override
    {
        if (mCallback) {
            mCallback();
        }

        return nullptr;
    }

    ~TraversalCompletionTask() { }
private:
    std::function<void()> mCallback;
};

std::vector<kodachi::string_view>
getSortedChildNames(const kodachi::StringAttribute& potentialChildrenAttr)
{
    const auto potentialChildrenSample = potentialChildrenAttr.getNearestSample(0.f);

    // Not using std::partial_sort_copy since StringAttribute stores its values
    // as const char*, so the comparison would need to call strlen repeatedly
    std::vector<kodachi::string_view> potentialChildren(
            potentialChildrenSample.begin(), potentialChildrenSample.end());

    std::sort(potentialChildren.begin(), potentialChildren.end());

    return potentialChildren;
}

template <typename iter>
std::vector<std::string>
createLocationVector(const iter& beginIter, const iter& endIter,
                     const kodachi::StringAttribute& locationPathAttr)
{
    const kodachi::string_view locationPath = locationPathAttr.getValueCStr();

    std::vector<std::string> values;
    values.reserve(std::distance(beginIter, endIter));

    std::transform(beginIter, endIter, std::back_inserter(values),
       [&](const kodachi::string_view& childName)
       {
            return kodachi::concat(locationPath, "/", childName);
       });

    return values;
}

// During initial traversal, all original child locations will have been monitored
// If the potentialChildren attr changes, we want to find only the names
// of children that have been added.
std::vector<std::string>
getUnmonitoredChildLocations(const kodachi::StringAttribute& originalChildrenAttr,
                             const kodachi::StringAttribute& previousChildrenAttr,
                             const kodachi::StringAttribute& currentChildrenAttr,
                             const kodachi::StringAttribute& locationPathAttr)
{
    if (currentChildrenAttr.getNumberOfValues() == 0) {
        return {};
    }

    const bool origSameAsPrevious = originalChildrenAttr == previousChildrenAttr;

    if (origSameAsPrevious && previousChildrenAttr.getNumberOfValues() == 0) {
        const auto potentialChildrenSample = currentChildrenAttr.getNearestSample(0.f);

        return createLocationVector(potentialChildrenSample.begin(),
                                    potentialChildrenSample.end(),
                                    locationPathAttr);
    }

    const auto currentChildren = getSortedChildNames(currentChildrenAttr);

    std::vector<kodachi::string_view> monitoredChildren;

    if (!origSameAsPrevious) {
        // All original and previous locations are already monitored, so
        // create the union of their names
        const auto originalChildren = getSortedChildNames(originalChildrenAttr);

        const auto previousChildren = getSortedChildNames(previousChildrenAttr);

        std::set_union(originalChildren.begin(), originalChildren.end(),
                       previousChildren.begin(), previousChildren.end(),
                       std::back_inserter(monitoredChildren));

    } else {
        monitoredChildren = getSortedChildNames(previousChildrenAttr);
    }

    std::vector<kodachi::string_view> unmonitoredChildNames;

    // find the current children that are not already monitored
    std::set_difference(currentChildren.begin(), currentChildren.end(),
                        monitoredChildren.begin(), monitoredChildren.end(),
                        std::back_inserter(unmonitoredChildNames));

    if (!unmonitoredChildNames.empty()) {
        return createLocationVector(unmonitoredChildNames.begin(),
                                    unmonitoredChildNames.end(),
                                    locationPathAttr);
    } else {
        return {};
    }
}

} // anonymous namespace

namespace kodachi {

const std::string Traversal::kParallelTraversal("kodachi.parallelTraversal");

class Traversal::ParallelTraversalTask : public tbb::task
{
public:
    ParallelTraversalTask(std::string locationPath,
                          Traversal& strategy,
                          Traversal::PreCookCallback preCookCallback,
                          bool evict)
    : tbb::task()
    , mLocationPath(std::move(locationPath))
    , mTraversal(strategy)
    , mPreCookCallback(preCookCallback)
    , mEvict(evict)
    {}

    tbb::task*
    execute() override
    {
        try {
            // In the case that we are being used as a continuation task,
            // behave like empty_task by doing nothing
            if (mLocationPath.empty()) {
                return nullptr;
            }

            kodachi::KodachiLogging::ThreadLogPool tlp(true, mLocationPath);

            if (mPreCookCallback && !mPreCookCallback(mLocationPath)) {
                // don't call the callback on child locations
                mPreCookCallback = nullptr;
            }

            // @deprecated
            // kodachi::TaskArena is a manager and dispatcher for a pool of tbb::task_arena(s).
            // This can be replaced by tbb::this_task_arena::isolate in TBB2017+.
            // By running each cookLocation call in a task_arena, it avoids
            // the case where the TBB scheduler allows a thread to cook a second
            // location while in the middle of cooking the first
            kodachi::TaskArena taskArena;

            kodachi::KodachiRuntime::LocationData locationData;

            taskArena.execute([&]() { locationData =
                    mTraversal.mCookClient->cookLocation(mLocationPath, mEvict); });

            if (!locationData.doesLocationExist()) {
                return nullptr;
            }

            mTraversal.addData(locationData);

            // we're processing the children.
            // What we do depends on how many there are.
            const StringAttribute potentialChildrenAttr = locationData.getPotentialChildren();
            const int64_t numChildren = potentialChildrenAttr.getNumberOfValues();

            if (numChildren == 0) {
                return nullptr; // nothing to do
            }

            // Bypass the scheduler and avoid task allocation and deallocation by
            // recycling ourself for the single child case
            if (numChildren == 1) {
                mLocationPath = kodachi::concat(mLocationPath, "/", potentialChildrenAttr.getValueCStr());

                // Set ourselves to the child and recycle
                recycle_as_child_of(*parent());

                // execute this task immediately on the current thread
                return this;
            }

            /* Multiple children.
             * Determine the traversal method
             */
            if (isParallelTraversalEnabled(locationData)) {
                return processChildrenParallel(locationData);
            }

            tbb::task* returnTask = nullptr;

            taskArena.execute([&]() { returnTask = processChildrenSerial(locationData); });

            return returnTask;

        } catch (std::exception& e) {
            KdLogError("Caught Exception in ParallelTraversalTask: " << e.what());
        }

        return nullptr;
    }

protected:

    tbb::task*
    processChildrenParallel(const kodachi::KodachiRuntime::LocationData& locationData)
    {
        const StringAttribute potentialChildrenAttr = locationData.getPotentialChildren();
        const auto potentialChildren = potentialChildrenAttr.getNearestSample(0.f);
        /*
         * Reuse this task for the first child and spawn new tasks for the rest.
         * We don't have any additional work to do once the children are processed
         * so use an empty_task for the continuation.
         */
        tbb::empty_task *continuation = new (allocate_continuation()) tbb::empty_task();

        tbb::task_list childTasks;

        for (auto iter = std::next(potentialChildren.begin()); iter != potentialChildren.end(); ++iter) {
            ParallelTraversalTask* t = new (continuation->allocate_child())
            ParallelTraversalTask(kodachi::concat(mLocationPath, "/", *iter), mTraversal, mPreCookCallback, mEvict);
            // add to list, keeping count
            childTasks.push_back(*t);
        }

        mLocationPath = kodachi::concat(mLocationPath, "/", potentialChildren.front());
        recycle_as_child_of(*continuation);

        continuation->set_ref_count(potentialChildren.size());

        // push these tasks to the bottom of the current thread's deque
        // they will be executed by this thread unless stolen by a different
        // thread that has no work in its own deque
        spawn(childTasks);

        // execute this task immediately on the current thread
        return this;
    }

    tbb::task*
    processChildrenSerial(const kodachi::KodachiRuntime::LocationData& locationData)
    {
        LocationStack stack;
        {
            const bool executeCallbackOnChildren = mPreCookCallback != nullptr;

            pushChildrenToStack(stack, mLocationPath, locationData, executeCallbackOnChildren);
        }

        /*
         * In the case that we spawn new TreeWalkTasks, we will use ourself as the
         * continuation. We don't want to recycle ourself as a new task since
         * we don't know how long the depth first traversal will take, and another
         * thread may be free to execute the child tasks. recycle_as_safe_continuation
         * requires that we add 1 to our ref count to account for ourself.
         * Setting location to invalid iterator results in execute() doing nothing.
         */
        set_ref_count(1);
        mLocationPath.clear();
        recycle_as_safe_continuation();

        while (!stack.empty()) {
            const std::pair<std::string, bool> currentLocation = std::move(stack.top());
            stack.pop();

            bool callbackOnChildren = false;
            if (currentLocation.second) {
                callbackOnChildren = mPreCookCallback(currentLocation.first);
            }

            const auto currentLocationData =
                        mTraversal.mCookClient->cookLocation(currentLocation.first, mEvict);

            if (!currentLocationData.doesLocationExist()) {
                continue;
            }

            mTraversal.addData(currentLocationData);

            const StringAttribute potentialChildrenAttr =
                                     currentLocationData.getPotentialChildren();
            if (potentialChildrenAttr.getNumberOfValues() == 0) {
                continue;
            }

            if (isParallelTraversalEnabled(currentLocationData)) {
                const auto potentialChildren = potentialChildrenAttr.getNearestSample(0.f);

                /*
                 * spawn the tasks as we go, instead of waiting until depth
                 * first traversal is finished. This requires using
                 * allocate_additional_child_of instead of allocate_child
                 * since we are potentially allocating child tasks
                 * after we begin spawning them. ref_count is incremented
                 * automatically.
                 */
                tbb::task_list childTasks;

                Traversal::PreCookCallback callback;
                if (callbackOnChildren) {
                    callback = mPreCookCallback;
                }

                for (const char* childName : potentialChildren) {
                    ParallelTraversalTask* t = new (allocate_additional_child_of(*this))
                        ParallelTraversalTask(kodachi::concat(currentLocation.first, "/", childName), mTraversal, callback, mEvict);

                    childTasks.push_back(*t);
                }

                spawn(childTasks);
            } else {
                pushChildrenToStack(stack, currentLocation.first, currentLocationData, callbackOnChildren);
            }
        }

        // we're a continuation now, nothing additional to execute
        return nullptr;
    }

    std::string mLocationPath;
    Traversal& mTraversal;
    Traversal::PreCookCallback mPreCookCallback;
    bool mEvict = true;
};

Traversal::Traversal(const std::shared_ptr<KodachiRuntime::Client>& kodachiClient)
:   mState(State::INITIALIZING)
,   mRootLocationPath("/root")
,   mCookClient(kodachiClient)
{}

Traversal::Traversal(const KodachiRuntime::Ptr& runtime,
                     const kodachi::KodachiRuntime::Op::Ptr& cookOp)
: mState(State::INITIALIZING)
, mRootLocationPath("/root")
{
    const auto txn = runtime->createTransaction();
    mCookClient = txn->createClient();
    txn->setClientOp(mCookClient, cookOp);
    runtime->commit(txn);
}

Traversal::~Traversal() {}

KodachiRuntime::LocationData
Traversal::getLocation()
{
    State expected = State::INITIALIZING;
    if (mState.compare_exchange_strong(expected, State::RUNNING)) {
        initialize();
    }

    std::unique_lock<std::mutex> lock(mLocationDataMutex);

    mLocationDataCv.wait(lock,
            [&](){return !mLocationData.empty() || mState.load() == State::COMPLETE; });

    KodachiRuntime::LocationData ret;
    if (!mLocationData.empty()) {
        ret = std::move(mLocationData.front());
        mLocationData.erase(mLocationData.begin());
    }

    return ret;
}

std::vector<KodachiRuntime::LocationData>
Traversal::getLocations()
{
    State expected = State::INITIALIZING;
    if (mState.compare_exchange_strong(expected, State::RUNNING)) {
        initialize();
    }

    std::unique_lock<std::mutex> lock(mLocationDataMutex);

    mLocationDataCv.wait(lock,
            [&](){return !mLocationData.empty() || mState.load() == State::COMPLETE; });

    auto ret(std::move(mLocationData));

    mLocationData.clear();

    return ret;
}

void
Traversal::setRootLocationPath(std::string rootPath)
{
    if (mState == State::INITIALIZING) {
        mRootLocationPath = std::move(rootPath);
    }
}

bool
Traversal::isValid() const
{
    if (mState == State::COMPLETE) {
        std::unique_lock<std::mutex> lock(mLocationDataMutex);
        return !mLocationData.empty();
    }

    return true;
}

void
Traversal::initialize()
{
    if (!mCookClient) {
        throw std::runtime_error("Traversal - KodachiRuntime::Client is null");
    }

    mRootTask = new(tbb::task::allocate_root())
            TraversalCompletionTask([this]() { onTraversalComplete(); });

    // Allocation 1 child to start the traversal
    // We want rootTask to execute at the very end, which will happen when ref_count is 0
    mRootTask->set_ref_count(1);
    ParallelTraversalTask* childTask =
            new(mRootTask->allocate_child()) ParallelTraversalTask(
                    mRootLocationPath, *this, createPreCookCallback(), evictAfterCook());
    mRootTask->spawn(*childTask);
}

void
Traversal::onTraversalComplete()
{
    mState.store(State::COMPLETE);

    mLocationDataCv.notify_one();
}

Traversal::PreCookCallback
Traversal::createPreCookCallback()
{
    return nullptr;
}

bool
Traversal::evictAfterCook() const
{
    return true;
}

void
Traversal::addData(const KodachiRuntime::LocationData& data)
{
    {
        std::lock_guard<std::mutex> lock(mLocationDataMutex);
        mLocationData.push_back(data);
    }
    mLocationDataCv.notify_one();
}

////////////////////////////////////////////////////////////////////////////////
//////////////////////// MonitoringTraversal ///////////////////////////////////
////////////////////////////////////////////////////////////////////////////////

MonitoringTraversal::MonitoringTraversal(
        const kodachi::KodachiRuntime::Ptr& runtime,
        const kodachi::KodachiRuntime::Op::Ptr& cookOp,
        const kodachi::KodachiRuntime::Op::Ptr& monitorOp)
: Traversal(nullptr)
{
    auto txn = runtime->createTransaction();

    mCookClient = txn->createClient();
    mMonitorClient = txn->createClient();

    txn->setClientOp(mCookClient, cookOp);
    txn->setClientOp(mMonitorClient, monitorOp);

    runtime->commit(txn);
}

std::vector<KodachiRuntime::LocationData>
MonitoringTraversal::getLocations()
{
    if (mState != State::MONITORING && mState != State::PARTIAL_MONITORING) {
        return Traversal::getLocations();
    }

    const bool performPartialMonitoring = (mState == State::PARTIAL_MONITORING);
    if (performPartialMonitoring) {
        KdLogDebug("Partial Monitoring enabled.");
    }

    std::mutex locationEventsMutex;
    std::vector<KodachiRuntime::LocationData> locationEvents;

    tbb::parallel_for_each(mGeolibClients.begin(), mGeolibClients.end(),
    [&](GeolibClientStruct& clientStruct)
    {
        // @deprecated
        // Replace with tbb::this_task_arena::isolate
        kodachi::TaskArena taskArena;
        taskArena.execute([&]()
        {
            {
                // Each geolib client has its own copy of the optree, so each needs to sync
                // with the main client
                clientStruct.mLastSyncedCommitId = mCookClient->syncClient(
                        clientStruct.mCookClient, clientStruct.mLastSyncedCommitId);
            }

            std::vector<KodachiRuntime::LocationData> locationEventsTL;
            std::vector<internal::GeolibRuntime::LocationEvent> geolibLocationEvents;

            clientStruct.mMonitorClient->getLocationEvents(
                    geolibLocationEvents, std::numeric_limits<int32_t>::max());

            while (!geolibLocationEvents.empty()) {
                for (auto& locationEvent : geolibLocationEvents) {
                    const auto locationPathAttr = locationEvent.getLocationPathAttr();
                    auto iter = clientStruct.mActiveLocations.find(locationPathAttr);
                    if (iter == clientStruct.mActiveLocations.end()) {
                        // This client is not responsible for this location
                        continue;
                    }

                    if (locationEvent.hasLocationData()) {
                        const auto locationData = locationEvent.getLocationData();
                        if (locationData.doesLocationExist()) {
                            MonitoringTraversal::monitorUnmonitoredChildren(
                                    locationPathAttr, locationData, clientStruct, iter);

                            const auto locationAttrs = locationData.getAttrs();

                            // If partial monitoring enabled, check if this location is marked and then 
                            // include/exclude based on partial monitoring method.
                            //
                            if (performPartialMonitoring) {
                                const kodachi::IntAttribute locationMarkedAttr =
                                        locationAttrs.getChildByName("kodachi.live_render_locations.marked");

                                // Exclude this location?
                                if (mExcludeLocations) {
                                    if (locationMarkedAttr.getValue(0, false) == 1) {
                                        continue; // skip cookLocationAndChildren() call on this location
                                    }
                                }
                                // Include this location?
                                else {
                                    if (locationMarkedAttr.getValue(0, false) == 0) {
                                        continue; // skip cookLocationAndChildren() call on this location
                                    }
                                }
                            }

                            const kodachi::StringAttribute typeAttr = locationAttrs.getChildByName("type");
                            if (typeAttr == mLeafTypeAttr) {
                                MonitoringTraversal::cookLocationAndChildren(locationPathAttr,
                                        clientStruct.mCookClient, locationEventsTL);
                            }
                        } else {
                            locationEventsTL.push_back(
                                    KodachiRuntime::LocationData(locationEvent));
                        }
                    }
                }

                // This call clears locationEventsTL first
                clientStruct.mMonitorClient->getLocationEvents(
                        geolibLocationEvents, std::numeric_limits<int32_t>::max());
            }

            std::lock_guard<std::mutex> g(locationEventsMutex);
            if (locationEvents.empty()) {
                locationEvents = std::move(locationEventsTL);
            } else {
                locationEvents.reserve(locationEvents.size() + locationEventsTL.size());
                std::move(locationEventsTL.begin(), locationEventsTL.end(),
                          std::back_inserter(locationEvents));
            }
        });
    });

    return locationEvents;
}

bool
MonitoringTraversal::isValid() const
{
    if (mState == State::MONITORING || mState == State::PARTIAL_MONITORING) {
        return true;
    }

    return Traversal::isValid();
}

void
MonitoringTraversal::applyOpTreeDeltas(const kodachi::GroupAttribute& deltasAttr, bool doPartialLiveRender, bool excludeLocations)
{
    if (mState != State::MONITORING && mState != State::PARTIAL_MONITORING) {
        initializeOpTreeDeltaProcessing(doPartialLiveRender, excludeLocations);
    }

    auto runtime = mMonitorClient->getRuntime();

    auto txn = runtime->createTransaction();

    // process all of the deltas in order that they were received
    // This way if we received many deltas that all update the same OpArgs,
    // only the newest will be processed
    const int64_t numDeltas = deltasAttr.getNumberOfChildren();
    for (int64_t i = 0; i < numDeltas; ++i) {
        txn->parseGraph(deltasAttr.getChildByIndex(i));
    }

    runtime->commit(txn);
}

void
MonitoringTraversal::setLeafType(const kodachi::StringAttribute& leafTypeAttr)
{
    if (mState == State::INITIALIZING) {
        mLeafTypeAttr = leafTypeAttr;
    } else {
        KdLogError("Cannot set leaf type once traversal has started");
    }
}

void
MonitoringTraversal::onTraversalComplete()
{
    // Clear out the initial location events since they are identical to what we just cooked
    tbb::parallel_for_each(mMonitorClient->mClients.begin(), mMonitorClient->mClients.end(),
    [](kodachi::KodachiRuntime::Client::ThreadLocalClientStruct& clientStruct)
    {
        std::vector<internal::GeolibRuntime::LocationEvent> locationEvents;

        do {
            clientStruct.mClient->getLocationEvents(
                    locationEvents, std::numeric_limits<int32_t>::max());
        } while (!locationEvents.empty());
    });

    Traversal::onTraversalComplete();
}

Traversal::PreCookCallback
MonitoringTraversal::createPreCookCallback()
{
    return std::bind(&MonitoringTraversal::monitorLocation, this, std::placeholders::_1);
}

bool
MonitoringTraversal::evictAfterCook() const
{
    return false;
}

bool
MonitoringTraversal::monitorLocation(const std::string& location)
{
    mMonitorClient->setLocationsActive({location});

    const auto locationData = mMonitorClient->cookLocation(location, false);

    const kodachi::StringAttribute potentialChildrenAttr =
            locationData.getPotentialChildren();

    getActiveLocationsMap()[locationData.getLocationPathAttr()] =
            std::make_pair(potentialChildrenAttr, potentialChildrenAttr);

    return locationData.doesLocationExist();
}

void
MonitoringTraversal::initializeOpTreeDeltaProcessing(bool doPartialMonitor, bool excludeLocations)
{
    if (mState != State::COMPLETE) {
        throw std::runtime_error(
                "Cannot initialize monitoring until initial traversal is complete");
    }

    // We expect that each thread in the Traversal initialized a monitor client,
    // a cook client, and an active locations map. If that didn't happen then
    // error out
    auto& geolibMonitorClients = mMonitorClient->mClients;
    auto& geolibCookClients = mCookClient->mClients;

    const std::size_t numMonitoringClients = geolibMonitorClients.size();
    const std::size_t numCookClients = geolibCookClients.size();

    // If this is the case, something has seriously gone wrong.
    // The initial traversal used a thread to monitor a location without
    // then cooking the location on that same thread.
    if (numCookClients < numMonitoringClients) {
        std::ostringstream ss;
        ss << "MonitoringTraversal: More monitoring clients (" << numMonitoringClients
           << ") were created than cook clients (" << numCookClients << ")";

        throw std::runtime_error(ss.str());
    }

    // If this is the case, it probably means parallel traversal happened at
    // child locations that only exist further down the optree than where
    // we are monitoring. Live rendering will still be correct, just fewer
    // threads can participate in applying optree deltas.
    if (numCookClients > numMonitoringClients) {
        KdLogWarn("MonitoringTraversal: Fewer monitoring clients (" << numMonitoringClients
                  << ") were created than cook clients (" << numCookClients << ")")
    }

    KdLogDebug("Using " << numMonitoringClients << " clients for optree delta processing");

    mGeolibClients.reserve(numMonitoringClients);

    for (auto& clientStruct : geolibMonitorClients) {
        mGeolibClients.emplace_back();

        GeolibClientStruct& geolibClientStruct = mGeolibClients.back();

        geolibClientStruct.mThreadId = clientStruct.mThreadId;
        geolibClientStruct.mMonitorClient = clientStruct.mClient;
    }

    for (auto& clientStruct : geolibCookClients) {
        auto iter = std::find_if(mGeolibClients.begin(), mGeolibClients.end(),
                [&](GeolibClientStruct& s) { return s.mThreadId == clientStruct.mThreadId; });

        if (iter != mGeolibClients.end()) {
            iter->mCookClient = clientStruct.mClient;
            iter->mLastSyncedCommitId = clientStruct.mLastSyncedCommitId;
        }
    }

    for (auto& mapPair : mActiveLocations) {
        auto iter = std::find_if(mGeolibClients.begin(), mGeolibClients.end(),
                [&](GeolibClientStruct& s) { return s.mThreadId == mapPair.first; });

        if (iter == mGeolibClients.end()) {
            throw std::runtime_error(
                    "MonitoringTraversal: Monitor client does not have matching potential children map");
        }

        iter->mActiveLocations = std::move(mapPair.second);
    }

    mState = (doPartialMonitor ? State::PARTIAL_MONITORING : State::MONITORING);
    mExcludeLocations = excludeLocations;
}

MonitoringTraversal::ActiveLocationsMap&
MonitoringTraversal::getActiveLocationsMap()
{
    bool exists = false;
    auto& activeLocationsMapPair = mActiveLocations.local(exists);

    if (!exists) {
        activeLocationsMapPair.first = tbb::this_tbb_thread::get_id();
    }

    return activeLocationsMapPair.second;
}

void
MonitoringTraversal::monitorUnmonitoredChildren(
        const kodachi::StringAttribute& locationPathAttr,
        const internal::GeolibRuntime::LocationData& locationData,
        GeolibClientStruct& clientStruct,
        ActiveLocationsMap::iterator& iter)
{
    const auto potentialChildrenAttr = locationData.getPotentialChildren();
    const auto previousPotentialChildrenAttr = iter->second.second;
    if (previousPotentialChildrenAttr != potentialChildrenAttr) {
        const auto originalPotentialChildrenAttr = iter->second.first;

        const auto unmonitoredChildLocations =
                getUnmonitoredChildLocations(originalPotentialChildrenAttr,
                                             previousPotentialChildrenAttr,
                                             potentialChildrenAttr,
                                             locationPathAttr);

        if (!unmonitoredChildLocations.empty()) {
            clientStruct.mMonitorClient->setLocationsActive(unmonitoredChildLocations);

            for (auto & childLocation : unmonitoredChildLocations) {
                clientStruct.mActiveLocations[kodachi::StringAttribute(childLocation)] =
                        std::make_pair(kodachi::StringAttribute{}, kodachi::StringAttribute{});
            }
        }

        if (!originalPotentialChildrenAttr.isValid()) {
            iter->second = std::make_pair(potentialChildrenAttr, potentialChildrenAttr);
        } else {
            iter->second.second = potentialChildrenAttr;
        }
    }
}

void
MonitoringTraversal::cookLocationAndChildren(
        const kodachi::StringAttribute& locationPathAttr,
        const kodachi::internal::GeolibRuntime::Client::Ptr& cookClient,
        std::vector<kodachi::KodachiRuntime::LocationData>& locationData)
{
    std::stack<std::string> locationStack;
    locationStack.push(locationPathAttr.getValue());

    while (!locationStack.empty()) {
        const std::string locationPath = std::move(locationStack.top());
        locationStack.pop();

        const auto cookedData = cookClient->cookLocation(locationPath);
        locationData.push_back(
                kodachi::KodachiRuntime::LocationData(locationPath, cookedData));
        if (cookedData.doesLocationExist()) {
            const auto potentialChildrenAttr = cookedData.getPotentialChildren();
            for (const kodachi::string_view childName : potentialChildrenAttr.getNearestSample(0.f)) {
                locationStack.push(kodachi::concat(locationPath, "/", childName));
            }
        }
    }
}

} // namespace kodachi

