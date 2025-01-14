// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// self
#include <kodachi/KodachiRuntime.h>

// kodachi
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/plugin_system/PluginManager.h>

#include <tbb/parallel_for_each.h>

// C++
#include <iostream>
#include <map>
#include <unordered_set>
#include <unordered_map>

namespace {
KdLogSetup("KodachiRuntime");
} // anonymous namespace

namespace kodachi {

struct KodachiRuntime::runtime_constructor_key {};
struct KodachiRuntime::client_constructor_key {};
struct KodachiRuntime::transaction_constructor_key {};
struct KodachiRuntime::op_constructor_key {};

//// KodachiRuntime::OpTreeSyncronizer ////

internal::GeolibRuntime::Op::Ptr
KodachiRuntime::OpTreeSynchronizer::syncFromOp(const kodachi::internal::GeolibRuntime& destRuntime,
                                               const kodachi::internal::GeolibRuntime::Transaction::Ptr& destTxn,
                                               const kodachi::internal::GeolibRuntime::Op::Ptr& srcOp)
{
    auto& opMap = mOpIdMaps[&destRuntime];

    const auto getOrCreate =
            [&](const kodachi::internal::GeolibRuntime::Op::Ptr& srcOp,
                bool syncArgs=true) -> kodachi::internal::GeolibRuntime::Op::Ptr
    {
        auto& destOp = opMap[srcOp->getOpId()];
        if (!destOp) {
            destOp = destTxn->createOp();
        }

        if (syncArgs) {
            const auto srcArgs = srcOp->getOpArgs();
            destTxn->setOpArgs(destOp, srcArgs.first, srcArgs.second);
        }

        return destOp;
    };

    std::vector<kodachi::internal::GeolibRuntime::Op::Ptr> srcOpStack;
    std::unordered_set<FnGeolibOpId> visited;

    srcOpStack.push_back(srcOp);
    while (!srcOpStack.empty()) {
        const auto srcOp = std::move(srcOpStack.back());
        srcOpStack.pop_back();

        // if we haven't visited it, set its inputs and add the inputs to the stack
        if(visited.emplace(srcOp->getOpId()).second) {
            std::vector<kodachi::internal::GeolibRuntime::Op::Ptr> srcInputs;
            srcOp->getInputs(srcInputs);

            std::vector<kodachi::internal::GeolibRuntime::Op::Ptr> dstInputs;
            dstInputs.reserve(srcInputs.size());
            for (auto& srcInput : srcInputs) {
                dstInputs.push_back(getOrCreate(srcInput));
                srcOpStack.push_back(std::move(srcInput));
            }

            destTxn->setOpInputs(getOrCreate(srcOp, false), dstInputs);
        }
    }

    return getOrCreate(srcOp);
}

//// KodachiRuntime::Op ////

std::pair<std::string, kodachi::Attribute>
KodachiRuntime::Op::getOpArgs() const
{
    if (!mGeolibOp) {
        KdLogInfo("Op " << mOpId << ": cannot get OpArgs for an uncommitted op");
        return {};
    }

    return mGeolibOp->getOpArgs();
}
std::vector<KodachiRuntime::Op::Ptr>
KodachiRuntime::Op::getInputs() const
{
    if (!mGeolibOp) {
        KdLogInfo("Op " << mOpId << ": cannot get inputs for an uncommitted op");
        return {};
    }

    const auto runtime = mRuntime.lock();
    if (!runtime) {
        KdLogWarn("Op " << mOpId << ": Parent runtime has expired");
        return {};
    }

    std::vector<internal::GeolibRuntime::Op::Ptr> geolibInputs;
    {
        // Geolib Op reference counting happens in the Geolib runtime and
        // is not thread safe. Lock the geolib runtime.
        std::lock_guard<std::mutex> lock(runtime->mMasterGeolibRuntimeMutex);
        mGeolibOp->getInputs(geolibInputs);
    }

    std::vector<Op::Ptr> inputs;
    inputs.reserve(geolibInputs.size());

    for (auto& geolibInput : geolibInputs) {
        const auto iter = runtime->mGeolibToKodachiOpMap.find(geolibInput->getOpId());
        if (iter != runtime->mGeolibToKodachiOpMap.end()) {
            inputs.push_back(iter->second);
        } else {
            KdLogWarn("Geolib Op with ID: " << geolibInput->getOpId()
                      << " has not matching Kodachi Op");
        }
    }

    return inputs;
}

KodachiRuntime::Op::Op(const op_constructor_key&,
                       const KodachiOpId& opId,
                       const KodachiRuntime::WeakPtr& runtime)
: mOpId(opId)
, mRuntime(runtime)
{
}

//// KodachiRuntime::LocationData ////

KodachiRuntime::LocationData::LocationData() {}

KodachiRuntime::LocationData::LocationData(
        const std::string& locationPath,
        const internal::GeolibRuntime::LocationData& locationData)
:   mLocationPathAttr(locationPath)
,   mLocationData(locationData)
{}

KodachiRuntime::LocationData::LocationData(
        const internal::GeolibRuntime::LocationEvent& locationEvent)
:   mLocationPathAttr(locationEvent.getLocationPathAttr())
,   mLocationData(locationEvent.getLocationData())
{}

std::string
KodachiRuntime::LocationData::getLocationPath() const
{
    return mLocationPathAttr.getValue(std::string{}, false);
}

kodachi::StringAttribute
KodachiRuntime::LocationData::getLocationPathAttr() const
{
    return mLocationPathAttr;
}

kodachi::Hash
KodachiRuntime::LocationData::getHash() const
{
    return mLocationData.getHash();
}
bool
KodachiRuntime::LocationData::doesLocationExist() const
{
    return mLocationData.doesLocationExist();
}

kodachi::GroupAttribute
KodachiRuntime::LocationData::getAttrs() const
{
    return mLocationData.getAttrs();
}

kodachi::StringAttribute
KodachiRuntime::LocationData::getPotentialChildren() const
{
    return mLocationData.getPotentialChildren();
}

//// KodachiRuntime::Client ////

KodachiRuntime::Client::Client(const client_constructor_key& key,
                               const KodachiRuntime::WeakPtr& runtime)
:   mRuntime(runtime)
{
}

KodachiRuntime::Op::Ptr
KodachiRuntime::Client::getOp() const
{
    if (!mMasterClient) {
        KdLogWarn("Client's Transaction has not been committed yet");
        return nullptr;
    }

    const auto runtime = mRuntime.lock();
    if (!runtime) {
        KdLogWarn("Runtime has expired");
        return nullptr;
    }

    internal::GeolibRuntime::Op::Ptr clientOp;
    {
        std::lock_guard<std::mutex> lock(runtime->mMasterGeolibRuntimeMutex);
        clientOp =  mMasterClient->getOp();
    }

    const auto iter = runtime->mGeolibToKodachiOpMap.find(clientOp->getOpId());
    if (iter != runtime->mGeolibToKodachiOpMap.end()) {
        return iter->second;
    }

    return nullptr;
}

std::shared_ptr<KodachiRuntime>
KodachiRuntime::Client::getRuntime()
{
    return mRuntime.lock();
}

KodachiRuntime::LocationData
KodachiRuntime::Client::cookLocation(const std::string& locationPath, bool evict)
{
    auto& geolibClient = getTLGeolibClient();

    const auto geolibLocationData = geolibClient.cookLocation(locationPath);

    if (evict) {
        mRuntime.lock()->getTLGeolibRuntime().evict(locationPath);
    }

    return LocationData(locationPath, geolibLocationData);
}

void
KodachiRuntime::Client::setLocationsActive(const std::vector<std::string>& locationPaths)
{
    getTLGeolibClient().setLocationsActive(locationPaths);
}

std::vector<KodachiRuntime::LocationData>
KodachiRuntime::Client::getLocationEvents()
{
    using locationEventVector = std::vector<internal::GeolibRuntime::LocationEvent>;

    std::mutex locationEventVectorsMutex;
    std::vector<locationEventVector> locationEventVectors;

    tbb::parallel_for_each(mClients.begin(), mClients.end(),
            [&](KodachiRuntime::Client::ThreadLocalClientStruct& clientStruct)
            {
                syncClient(clientStruct);

                std::vector<internal::GeolibRuntime::LocationEvent> locationEvents;

                clientStruct.mClient->getLocationEvents(
                        locationEvents, std::numeric_limits<int32_t>::max());

                if (!locationEvents.empty()) {
                    std::lock_guard<std::mutex> g(locationEventVectorsMutex);
                    locationEventVectors.push_back(std::move(locationEvents));
                }
            });

    const std::size_t numLocationEvents = std::accumulate(
            locationEventVectors.begin(),
            locationEventVectors.end(),
            0,
            [](std::size_t sum, const locationEventVector& v) { return sum + v.size(); });

    std::vector<LocationData> locationData;
    locationData.reserve(numLocationEvents);

    for (auto& locationEvents : locationEventVectors) {
        for (auto& locationEvent : locationEvents) {
            if (locationEvent.hasLocationData()) {
                locationData.emplace_back(LocationData(locationEvent));
            }
        }
    }

    return locationData;
}

internal::GeolibRuntime::Client&
KodachiRuntime::Client::getTLGeolibClient()
{
    bool exists = false;
    ThreadLocalClientStruct& clientStruct = mClients.local(exists);
    if (!exists) {
        const auto kodachiRuntime = mRuntime.lock();
        if (!kodachiRuntime) {
            throw std::runtime_error("KodachiRuntime::Client - Runtime has expired");
        }

        internal::GeolibRuntime& tlGeolibRuntime = kodachiRuntime->getTLGeolibRuntime();
        auto txn = tlGeolibRuntime.createTransaction();
        clientStruct.mClient = txn->createClient();
        tlGeolibRuntime.commit(txn);

        clientStruct.mThreadId = tbb::this_tbb_thread::get_id();
    }

    syncClient(clientStruct);

    return *clientStruct.mClient;
}

void
KodachiRuntime::Client::syncClient(ThreadLocalClientStruct& clientStruct)
{
    clientStruct.mLastSyncedCommitId = syncClient(
            clientStruct.mClient, clientStruct.mLastSyncedCommitId);
}

FnGeolibCommitId
KodachiRuntime::Client::syncClient(const internal::GeolibRuntime::Client::Ptr& geolibClient,
                                   FnGeolibCommitId lastSyncedCommitId)
{
    if (!geolibClient) {
        throw std::runtime_error("KodachiRuntime::Client::syncClient - geolibClient is null");
    }

    const auto kodachiRuntime = mRuntime.lock();
    if (!kodachiRuntime) {
        throw std::runtime_error("KodachiRuntime::Client - Runtime has expired");
    }

    if (lastSyncedCommitId < kodachiRuntime->getLatestCommitId()) {
        auto geolibRuntime = geolibClient->getRuntime();
        auto txn = geolibRuntime->createTransaction();
        const auto clientOp = geolibClient->getOp();

        internal::GeolibRuntime::Op::Ptr syncOp;
        FnGeolibCommitId commitId = -1;
        // lock the master GeolibRuntime since we will be querying ops from it
        {
            std::lock_guard<std::mutex> lock(kodachiRuntime->mMasterGeolibRuntimeMutex);

            const auto masterClientOp = mMasterClient->getOp();
            if (!masterClientOp) {
                throw std::runtime_error("KodachiRuntime::Client - ClientOp not set");
            }

            syncOp = kodachiRuntime->mOpTreeSynchronizer->syncFromOp(
                    *geolibRuntime, txn, masterClientOp);

            commitId = kodachiRuntime->getLatestCommitId();
        }

        if (!clientOp || syncOp->getOpId() != clientOp->getOpId()) {
            txn->setClientOp(geolibClient, syncOp);
        }

        geolibRuntime->commit(txn);
        return commitId;
    } else {
        return lastSyncedCommitId;
    }
}

//// KodachiRuntime::Transaction ////

KodachiRuntime::Transaction::Transaction(const transaction_constructor_key& key,
        const std::weak_ptr<KodachiRuntime>& runtime)
:   mRuntime(runtime)
{}

KodachiRuntime::Op::Ptr
KodachiRuntime::Transaction::createOp()
{
    auto op = std::make_shared<Op>(op_constructor_key{}, KodachiOpId::generate(), mRuntime);

    mPendingNewOps.emplace(op->getOpId(), op);

    return op;
}

void
KodachiRuntime::Transaction::setOpArgs(const Op::Ptr& op,
                                       std::string opType,
                                       kodachi::Attribute args)
{
    auto& pendingArgs = mPendingOpArgs[op];

    pendingArgs.first = std::move(opType);
    pendingArgs.second = std::move(args);
}

void
KodachiRuntime::Transaction::setOpInputs(const Op::Ptr& op,
                                         std::vector<Op::Ptr> inputs)
{
    mPendingOpInputs[op] = std::move(inputs);
}

std::shared_ptr<KodachiRuntime::Client>
KodachiRuntime::Transaction::createClient()
{
    return std::make_shared<Client>(client_constructor_key{}, mRuntime);
}

void
KodachiRuntime::Transaction::setClientOp(const Client::Ptr& client, const Op::Ptr& op)
{
    mPendingClientOps[client] = op;
}

std::vector<KodachiRuntime::Op::Ptr>
KodachiRuntime::Transaction::parseGraph(const kodachi::GroupAttribute& graphAttr)
{
    std::vector<KodachiRuntime::Op::Ptr> ops;
    ops.reserve(graphAttr.getNumberOfChildren());

    for (auto opAttr : graphAttr) {
        const KodachiOpId opId(opAttr.name.data());
        if (!opId.is_valid()) {
            KdLogError("Op Name is not a valid KodachiOpId: " << opAttr.name);
            return {};
        }

        const auto op = getOrCreateOp(opId);

        const GroupAttribute opAttrs(opAttr.attribute);

        const StringAttribute opTypeAttr(opAttrs.getChildByName("opType"));
        const Attribute opArgsAttr(opAttrs.getChildByName("opArgs"));
        if (opTypeAttr.isValid()) {
            setOpArgs(op, opTypeAttr.getValue(), opArgsAttr);
        }

        const StringAttribute opInputsAttr(opAttrs.getChildByName("opInputs"));
        if (opInputsAttr.isValid()) {
            const auto opInputs = opInputsAttr.getNearestSample(0.f);

            std::vector<Op::Ptr> inputs;
            inputs.reserve(opInputs.size());

            for (auto opInput : opInputs) {
                const KodachiOpId opInputId(opInput);
                if (!opInputId.is_valid()) {
                    KdLogError("Op Input Name is not a valid KodachiOpId: " << opInput);
                    continue;
                }
                inputs.emplace_back(getOrCreateOp(opInputId));
            }

            setOpInputs(op, std::move(inputs));
        }

        ops.push_back(std::move(op));
    }

    return ops;
}

KodachiRuntime::Op::Ptr
KodachiRuntime::Transaction::appendOpChain(const Op::Ptr& op,
                                           const kodachi::GroupAttribute& opChainAttr)
{
    auto rootOp = op;

    for (const auto child : opChainAttr) {
        const GroupAttribute opAttrs(child.attribute);
        const StringAttribute opTypeAttr(opAttrs.getChildByName("opType"));
        const Attribute opArgsAttr(opAttrs.getChildByName("opArgs"));

        auto createdOp = createOp();
        setOpArgs(createdOp, opTypeAttr.getValue(), opArgsAttr);
        setOpInputs(createdOp, {rootOp});
        rootOp = createdOp;
    }

    return rootOp;
}

KodachiRuntime::Op::Ptr
KodachiRuntime::Transaction::appendOps(const Op::Ptr& op,
                                       const std::vector<Op::Ptr>& opList)
{
    auto rootOp = op;
    for (const auto& op : opList) {
        setOpInputs(rootOp, {op});
        rootOp = op;
    }

    return rootOp;
}

KodachiRuntime::Op::Ptr
KodachiRuntime::Transaction::getOrCreateOp(const KodachiOpId& opId)
{
    // check if this is a new op created by this transaction
    const auto localIter = mPendingNewOps.find(opId);
    if (localIter != mPendingNewOps.end()) {
        return localIter->second;
    }

    // check if this is an existing op
    auto runtime = mRuntime.lock();
    if (const auto existingOp = runtime->getOpFromOpId(opId)) {
        return existingOp;
    }

    // create the op
    return mPendingNewOps.emplace(opId, std::make_shared<Op>(op_constructor_key{}, opId, mRuntime)).first->second;
}

void
KodachiRuntime::Transaction::clear()
{
    mPendingNewOps.clear();
    mPendingOpArgs.clear();
    mPendingOpInputs.clear();
    mPendingClientOps.clear();
}

//// KodachiRuntime ////

KodachiRuntime::KodachiRuntime(const runtime_constructor_key&)
:   mMasterGeolibRuntime(internal::GeolibRuntime::CreateRuntime())
,   mOpTreeSynchronizer(new OpTreeSynchronizer)
{
    assert(mMasterGeolibRuntime);
}

std::shared_ptr<KodachiRuntime>
KodachiRuntime::createRuntime()
{
    auto ret = std::make_shared<KodachiRuntime>(runtime_constructor_key{});
    ret->mWeakThis = ret;

    return ret;
}

kodachi::GroupAttribute
KodachiRuntime::describeOp(const std::string& opType)
{
    return mMasterGeolibRuntime->describeOp(opType);
}

kodachi::StringAttribute
KodachiRuntime::getRegisteredOpTypes() const
{
    return mMasterGeolibRuntime->getRegisteredOpTypes();
}

bool
KodachiRuntime::isValidOp(const KodachiOpId& opId) const
{
    return mKodachiOpMap.count(opId) == 1;
}

KodachiRuntime::Op::Ptr
KodachiRuntime::getOpFromOpId(const KodachiOpId& opId) const
{
    const auto iter = mKodachiOpMap.find(opId);
    if (iter != mKodachiOpMap.end()) {
        return iter->second;
    }

    return nullptr;
}

std::shared_ptr<KodachiRuntime::Transaction>
KodachiRuntime::createTransaction()
{
    return std::make_shared<Transaction>(transaction_constructor_key{},
                                         mWeakThis);
}

FnGeolibCommitId
KodachiRuntime::commit(const std::shared_ptr<Transaction>& txn)
{
    FnGeolibCommitId commitId = 0;
    std::lock_guard<std::mutex> lock(mMasterGeolibRuntimeMutex);
    {

    // create the geolib Transaction
    const auto geolibTxn = mMasterGeolibRuntime->createTransaction();

    // add new ops and create their matching Geolib ops
    for (auto& pendingOpPair : txn->mPendingNewOps) {
        auto kodachiOp = std::move(pendingOpPair.second);

        if (mKodachiOpMap.insert({pendingOpPair.first, kodachiOp}).second) {
            auto geolibOp = geolibTxn->createOp();
            kodachiOp->mGeolibOp = geolibOp;
            mGeolibToKodachiOpMap.insert({geolibOp->getOpId(), kodachiOp});
        } else {
            KdLogWarn("KodachiOp with ID: " << pendingOpPair.first
                      << " already exists in the runtime");
        }
    }

    // set OpArgs
    for (auto& pendingOpArgsPair : txn->mPendingOpArgs) {
        const auto kodachiOp = pendingOpArgsPair.first;
        const auto& opArgs = pendingOpArgsPair.second;

        if (!kodachiOp->mGeolibOp) {
            KdLogWarn("Op " << kodachiOp->getOpId()
                      << " does not have a matching geolib op");
            continue;
        }

        geolibTxn->setOpArgs(kodachiOp->mGeolibOp, opArgs.first, opArgs.second);
    }

    // set OpInputs
    for (auto& pendingOpInputsPair : txn->mPendingOpInputs) {
        const auto kodachiOp = pendingOpInputsPair.first;
        const auto& kodachiOpInputs = pendingOpInputsPair.second;

        if (!kodachiOp->mGeolibOp) {
            KdLogWarn("Op " << kodachiOp->getOpId()
                      << " does not have a matching geolib op");
            continue;
        }

        std::vector<internal::GeolibRuntime::Op::Ptr> geolibOpInputs;
        geolibOpInputs.reserve(kodachiOpInputs.size());

        for (auto& kodachiInput : kodachiOpInputs) {
            if (kodachiInput->mGeolibOp) {
                geolibOpInputs.emplace_back(kodachiInput->mGeolibOp);
            } else {
                KdLogWarn("Input Op " << kodachiInput->getOpId()
                          << " does not have a matching geolib op");
            }
        }

        geolibTxn->setOpInputs(kodachiOp->mGeolibOp, geolibOpInputs);
    }

    // Set the client ops and create the geolib clients if necessary
    for (auto& clientPair : txn->mPendingClientOps) {
        auto& client = clientPair.first;
        if (!client->mMasterClient) {
            client->mMasterClient = geolibTxn->createClient();
        }

        auto& kodachiOp = clientPair.second;

        if (!kodachiOp->mGeolibOp) {
            KdLogWarn("Client Op " << kodachiOp->getOpId()
                      << " does not have a matching geolib op");
            continue;
        }

        geolibTxn->setClientOp(client->mMasterClient, kodachiOp->mGeolibOp);
    }

    // commit the geolib Transaction
    commitId = mMasterGeolibRuntime->commit(geolibTxn);
    mLatestCommitId = commitId;
    }

    // clear the Kodachi Transaction
    txn->clear();

    return commitId;
}

std::string
KodachiRuntime::getRootLocationPath() const
{
    return mMasterGeolibRuntime->getRootLocationPath();
}

kodachi::Attribute
KodachiRuntime::getOptions() const
{
    return mMasterGeolibRuntime->getOptions();
}

void
KodachiRuntime::setOptions(const kodachi::Attribute& options)
{
    std::lock_guard<std::mutex> lock(mMasterGeolibRuntimeMutex);
    mMasterGeolibRuntime->setOptions(options);
}

bool
KodachiRuntime::isProcessing() const
{
    return mMasterGeolibRuntime->isProcessing();
}

void
KodachiRuntime::flushCaches()
{
    for (auto& runtime : mGeolibRuntimes) {
        runtime->flushCaches();
    }
}

internal::GeolibRuntime&
KodachiRuntime::getTLGeolibRuntime()
{
    bool exists = false;
    internal::GeolibRuntime::Ptr& runtime = mGeolibRuntimes.local(exists);

    if (!exists) {
        runtime = internal::GeolibRuntime::CreateRuntime();
        runtime->setOptions(mMasterGeolibRuntime->getOptions());
    }

    return *runtime;
}

KdPluginStatus
KodachiRuntime::setHost(KdPluginHost* host)
{
    return internal::GeolibRuntime::setHost(host);
}

} // namespace kodachi

