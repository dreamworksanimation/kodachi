// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// kodachi
#include <kodachi/KodachiOpId.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/plugin_system/PluginManager.h>

// foundry
#include <internal/FnGeolib/runtime/FnGeolibRuntime.h>

// tbb
#include <tbb/enumerable_thread_specific.h>
#include <tbb/concurrent_unordered_map.h>
#include <tbb/compat/thread>

// system
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace kodachi {

namespace internal {
using namespace FnGeolib;
}

/**
 * A thread-safe wrapper around Geolib3. It mostly matches the Geolib3 API with
 * some changes and additions. When a new thread calls into the Runtime or one
 * of its child classes, a thread-local copy of the internal Geolib Runtime is
 * made. This is primarily a replacement for FnScenegraphIterator, as it
 * allows us to directly interact with optrees rather than abstracting them.
 *
 * Once Geolib3 MT is shipped with Katana 3.5, much of this class's code can
 * be replaced with calls into Geolib3 MT.
 * @deprecated Much of this class's functionality is likely made irrelevant with current versions of 
 * Katana and Geolib3 MT.
 */
class KodachiRuntime
{
private:
    // Allows for the use of make_shared while preventing users from calling
    // the constructors
    struct runtime_constructor_key;
    struct client_constructor_key;
    struct transaction_constructor_key;
    struct op_constructor_key;

public:
    using Ptr = std::shared_ptr<KodachiRuntime>;
    using WeakPtr = std::weak_ptr<KodachiRuntime>;

    class Client;
    class Transaction;

    class Op
    {
    public:
        using Ptr = std::shared_ptr<Op>;

        Op(const op_constructor_key&,
           const KodachiOpId& op,
           const KodachiRuntime::WeakPtr& runtime);

        /**
         * Returns the latest committed {OpType, OpArgs} pair for this op.
         * If the op has never been committed, returns a default constructed pair.
         *
         * Set by calling Transaction::setOpArgs()
         */
        std::pair<std::string, kodachi::Attribute> getOpArgs() const;

        /*
         * Returns the latest committed inputs for this op.
         * If the op has never been committed, returns an empty vector.
         *
         * Set by calling Transaction::setOpInputs()
         */
        std::vector<Ptr> getInputs() const;

        /*
         * Returns the KodachiOpId for this op, which is used for
         * Serializing/Deserializing op trees. Unlike with GeolibRuntime,
         * pointer comparison of ops is a valid way of determining equality,
         * and you do not need to compare IDs.
         */
        const KodachiOpId& getOpId() const { return mOpId; }

    private:
        KodachiOpId mOpId;
        KodachiRuntime::WeakPtr mRuntime;
        internal::GeolibRuntime::Op::Ptr mGeolibOp;

        friend class KodachiRuntime;
    };

    // Unifies the GeolibRuntime LocationData and LocationEvent. With the main
    // difference being that GeolibRuntime LocationData does not store the
    // Scenegraph location that the data belongs to.
    class LocationData
    {
    public:
        LocationData();

        std::string getLocationPath() const;
        kodachi::StringAttribute getLocationPathAttr() const;

        kodachi::Hash getHash() const;
        bool doesLocationExist() const;
        kodachi::GroupAttribute getAttrs() const;
        kodachi::StringAttribute getPotentialChildren() const;

    private:
        LocationData(const std::string& locationPath,
                     const internal::GeolibRuntime::LocationData& locationData);
        LocationData(const internal::GeolibRuntime::LocationEvent& locationEvent);

        kodachi::StringAttribute mLocationPathAttr;
        internal::GeolibRuntime::LocationData mLocationData;

        friend class Client;
        friend class MonitoringTraversal;
    };

    class Client
    {
    public:
        using Ptr = std::shared_ptr<Client>;

        Client(const client_constructor_key&,
               const KodachiRuntime::WeakPtr& runtime);

        /**
         * The last committed ClientOp for this Client. Calls to cookLocation()
         * will execute each op in the op tree down to this op.
         *
         * Set by calling Transaction::setClientOp()
         */
        KodachiRuntime::Op::Ptr getOp() const;

        std::shared_ptr<KodachiRuntime> getRuntime();

        /**
         * Cooks the provided scenegraph location.
         *
         * @param locationPath
         * @param evict Evict all cooked locations from the thread-local
         * runtime cache except this one
         */
        LocationData cookLocation(const std::string& locationPath, bool evict);

        //
        // Asyncronous accessors
        //
        void setLocationsActive(const std::vector<std::string>& locationPaths);

        std::vector<LocationData> getLocationEvents();

    private:
        friend class MonitoringTraversal;

        internal::GeolibRuntime::Client& getTLGeolibClient();

        // used only for syncing client op between per-thread clients
        internal::GeolibRuntime::Client::Ptr mMasterClient;
        mutable std::mutex mMasterClientMutex;

        struct ThreadLocalClientStruct
        {
            internal::GeolibRuntime::Client::Ptr mClient;
            FnGeolibCommitId mLastSyncedCommitId = -1;
            tbb::tbb_thread::id mThreadId;
        };

        void syncClient(ThreadLocalClientStruct& clientStruct);
        FnGeolibCommitId syncClient(const internal::GeolibRuntime::Client::Ptr& geolibClient,
                                    FnGeolibCommitId lastSyncedCommitId);

        tbb::enumerable_thread_specific<ThreadLocalClientStruct> mClients;
        std::weak_ptr<KodachiRuntime> mRuntime;

        friend class Transaction;
        friend class KodachiRuntime;
    };

    class Transaction
    {
    public:
        using Ptr = std::shared_ptr<Transaction>;

        Transaction(const transaction_constructor_key&,
                    const KodachiRuntime::WeakPtr& runtime);

        Op::Ptr createOp();

        void setOpArgs(const Op::Ptr& op,
                       std::string opType,
                       kodachi::Attribute args);

        void setOpInputs(const Op::Ptr& op, std::vector<Op::Ptr> inputs);

        Client::Ptr createClient();

        void setClientOp(const Client::Ptr& client,
                         const Op::Ptr& op);

        // Applies changes from a GroupAttribute with KodachiOpTree formatting.
        // Generally created from OpTreeBuilder::build() or OpTreeBuilder::buildDelta()
        std::vector<Op::Ptr> parseGraph(const kodachi::GroupAttribute& graphAttr);

        Op::Ptr appendOpChain(const Op::Ptr& op,
                              const kodachi::GroupAttribute& opsAttr);

        Op::Ptr appendOps(const Op::Ptr& op,
                          const std::vector<Op::Ptr>& opList);

    private:
        Op::Ptr getOrCreateOp(const KodachiOpId& opId);

        // resets the Transaction once it has been committed
        void clear();

        // ops created for this transaction, the runtime does not know about them yet
        std::map<KodachiOpId, Op::Ptr> mPendingNewOps;

        std::map<Op::Ptr, std::pair<std::string, kodachi::Attribute>> mPendingOpArgs;
        std::map<Op::Ptr, std::vector<Op::Ptr>> mPendingOpInputs;
        std::map<Client::Ptr, Op::Ptr> mPendingClientOps;

        std::weak_ptr<KodachiRuntime> mRuntime;

        friend class KodachiRuntime;
    };

    KodachiRuntime(const runtime_constructor_key&);

    static KodachiRuntime::Ptr createRuntime();

    kodachi::GroupAttribute describeOp(const std::string& opType);
    kodachi::StringAttribute getRegisteredOpTypes() const;

    bool isValidOp(const KodachiOpId& opId) const;
    Op::Ptr getOpFromOpId(const KodachiOpId& opId) const;

    Transaction::Ptr createTransaction();
    FnGeolibCommitId commit(const Transaction::Ptr& txn);

    FnGeolibCommitId getLatestCommitId() const { return mLatestCommitId; }
    std::string getRootLocationPath() const;

    kodachi::Attribute getOptions() const;
    void setOptions(const kodachi::Attribute& options);

    bool isProcessing() const;

    void flushCaches();

    static KdPluginStatus setHost(KdPluginHost* host);

private:

    internal::GeolibRuntime& getTLGeolibRuntime();

    KodachiRuntime::WeakPtr mWeakThis;
    std::atomic<FnGeolibCommitId> mLatestCommitId;

    using KodachiOpMap = tbb::concurrent_unordered_map<KodachiOpId, Op::Ptr, std::hash<KodachiOpId>>;
    using GeolibToKodachiOpMap = tbb::concurrent_unordered_map<FnGeolibOpId, Op::Ptr>;

    KodachiOpMap mKodachiOpMap;
    GeolibToKodachiOpMap mGeolibToKodachiOpMap;

    // Not used for cooking, only for keeping the per-thread runtimes synced
    const internal::GeolibRuntime::Ptr mMasterGeolibRuntime;

    // this should be locked whenever the master geolib runtime's state is being
    // changed. This includes GeolibOp reference counting. Eventually
    // we will call directly into the suite and locking this mutex all the
    // time shouldn't be necessary
    mutable std::mutex mMasterGeolibRuntimeMutex;

    tbb::enumerable_thread_specific<internal::GeolibRuntime::Ptr> mGeolibRuntimes;

    class OpTreeSynchronizer
    {
    public:
        kodachi::internal::GeolibRuntime::Op::Ptr
        syncFromOp(const kodachi::internal::GeolibRuntime& destRuntime,
                   const kodachi::internal::GeolibRuntime::Transaction::Ptr& destTxn,
                   const kodachi::internal::GeolibRuntime::Op::Ptr& srcOp);

    private:

        // Maps the OpId of the Op from the master runtime to its equivalent op
        // from the local runtime.
        using OpIdMap = std::unordered_map<FnGeolibOpId, kodachi::internal::GeolibRuntime::Op::Ptr>;

        // Keeps track of every op created for a local runtime
        std::map<const kodachi::internal::GeolibRuntime*, OpIdMap> mOpIdMaps;
    };

    std::unique_ptr<OpTreeSynchronizer> mOpTreeSynchronizer;
};

} // namespace kodachi

