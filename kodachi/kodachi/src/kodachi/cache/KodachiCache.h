// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// C++
#include <algorithm>
#include <cstring>
#include <future>
#include <iostream>
#include <list>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

// TBB
#include <tbb/concurrent_hash_map.h>

// Kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>

// Internal
#include "CacheStats.h"
#include "CacheRegistry.h"
#include "CacheUtils.h"
#include "DiskIOUtils.h"

namespace kodachi
{
    template <typename T>
    inline bool
    is_future_ready(const std::future<T> & f)
    {
        return (f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    }

    template <typename T>
    inline bool
    is_shared_future_ready(const std::shared_future<T> & f)
    {
        return (f.valid() && f.wait_for(std::chrono::seconds(0)) == std::future_status::ready);
    }

    template <typename value_t>
    inline std::size_t
    defaultValueSizeApproximate(const value_t& val)
    {
        return sizeof( val );
    }

    template <typename AttrType>
    inline std::pair</* valid attribute */ bool, typename AttrType::value_type>
    getSetting(const kodachi::GroupAttribute settings,
               const std::string& setting)
    {
        if (settings.isValid()) {
            AttrType settingAttr = settings.getChildByName(setting);
            if (settingAttr.isValid()) {
                return { true, settingAttr.getValue() };
            }
        }

        return { false, typename AttrType::value_type { } };
    }

    template <typename AttrType>
    inline std::pair</* valid attribute */ bool, typename AttrType::value_type>
    getSetting(const kodachi::GroupAttribute globalSettings,
               const kodachi::GroupAttribute localSettings,
               const std::string& setting)
    {
        if (localSettings.isValid()) {
            AttrType settingAttr = localSettings.getChildByName(setting);
            if (settingAttr.isValid()) {
                return { true, settingAttr.getValue() };
            }
        }

        if (globalSettings.isValid()) {
            AttrType settingAttr = globalSettings.getChildByName(setting);
            if (settingAttr.isValid()) {
                return { true, settingAttr.getValue() };
            }
        }

        return { false, typename AttrType::value_type { } };
    }

    //--------------------------------------------------------------------
    //
    //    Kodachi Cache is used to cache data to disk in the form of binary files, and retrieve them when needed.
    //
    //    The cache is a class template with 9 template arguments:
    //            key_t: type of cache keys
    //            value_t: type of values; we store these in memory and/or on disk and reuse them.
    //            metadata_t: type of a container which may be used to provide extra data for template argument 5
    //                        (CreateValueFunc) so it can produce correct results.
    //            KeyHashFunc: the hash function that can convert objects of type key_t to 64-bit unsigned integers.
    //            CreateValueFunc: a function that uses an object of type key_t plus an optional object of type metadata_t
    //                             in order to produce the corresponding object of type value_t.
    //            IsValidFunc: a function that can be used to check whether or not an object of type value_t is valid.
    //            ReadValueFromDiskFunc: a function that knows how to read an object of type value_t from disk;
    //                                   should only require a file path and should return an object of type value_t.
    //            WriteValueToDiskFunc: a function that knows how to write an object of type value_t to disk, given the
    //                                  object and a file path.
    //            ApproximateValueSizeFunc: a function that can return the size of an object of type value_t.
    //
    //    Kodachi cache’s constructor is protected in order to stop users from directly creating the cache; instead,
    //    a method is provided, createCache(), which knows where to look for cache configurations.
    //
    //    Cache configs are expected to be found as attributes on location /root, under kodachi.cache; you can find more
    //    details in KodachiCache.h, in the comment block further down.
    //
    //    Almost everything of note happens inside KodachiCache::getValue():
    //          It starts with a one-time initialization of the cache, a call to KodachiCache::initialize(),
    //          which may include creating the cache directory on disk.
    //          Inside getValue() call, the cache decides whether or not the value is already cached and can
    //          be retrieved, or to create a new value and store it in memory or on disk (or both).
    //
    //    In order to place an upper limit on in-memory storage of cache entries (to avoid high memory usage),
    //    the cache employs two different “eviction policies”: one is LRU, and the other one is a random eviction.
    //
    //    The cache maintains a hash table of hash values (of type std::uint64_t) to shared futures (std::shared_future);
    //    note that we do not store key objects, and only store their hash value, as it is highly possible that keys are
    //    large in size. Using futures helps us to make sure multiple threads will not end up working on generating the same
    //    value from the same key, if one thread is busy working on value generation, the other threads will hold on to
    //    futures associated with that key and will get the value once the first thread is done generating the value.
    //
    //--------------------------------------------------------------------
    //
    // To change/set cache settings, you can modify/set the following attributes
    // on "/root" location.
    //
    // 1) kodachi.cache.global
    //      Applies to all Kodachi caches.
    //
    //      enabled                 (IntAttribute), 0/1 (default value should be 1)
    //      memory_enabled      (IntAttribute), 0/1 (default assumed to be 1)
    //      disk_enabled        (IntAttribute), 0/1 (default assumed to be 1)
    //      regenerate          (IntAttribute), 0/1 (default assumed to be 0)
    //      debug_messages      (IntAttribute), 0/1 (default assumed to be 0)
    //      force_permanent     (IntAttribute), 0/1 (default assumed to be 0)
    //                              force all Kodachi caches to read from/write to the permanent cache directory.
    //
    //      force_temporary     (IntAttribute), 0/1 (default assumed to be 0)
    //                              force all Kodachi caches to read from/write to the temporary cache directory (e.g., on local machine under /usr/render_tmp).
    //
    //      max_size_gb         (FloatAttribute), maximum in-memory cache size in GB (default value is 1000 GB)
    //      permanent_cache_loc (StringAttribute), full path to the permanent (shared) cache location
    //      temporary_cache_loc (StringAttribute), full path to the temporary cache location (e.g., on local machine under /usr/render_tmp).
    //
    // 2) kodachi.cache.NAME
    //      Only applies to the named Kodachi cache, and always overwrites global settings.
    //
    //      enabled             (IntAttribute), 0/1 (default value should be 1)
    //      memory_enabled      (IntAttribute), 0/1 (default value should be 1)
    //      disk_enabled        (IntAttribute), 0/1 (default value should be 1)
    //      regenerate          (IntAttribute), 0/1 (default value should be 0)
    //      debug_messages      (IntAttribute), 0/1 (default value should be 0)
    //      is_permanent        (IntAttribute), 0/1 (default value should be 0), if set to 1, this cache will read from/write to the permanent cache directory, if set to 0, will read from/write to the temporary cache directory (e.g., on local machine under /usr/render_tmp).
    //      max_size_gb         (FloatAttribute), maximum in-memory cache size in GB (default value is 1000 GB)
    //      permanent_cache_loc (StringAttribute), full path to the permanent (shared) cache location
    //      temporary_cache_loc (StringAttribute), full path to the temporary cache location (e.g., on local machine under /usr/render_tmp).
    //
    //--------------------------------------------------------------------
    // NOTE (Old settings):
    //
    //  The following environment variables should/can be used to modify cache behavior:
    //
    //  Required:
    //      Both types of on-disk caches (permanent and temporary) need to know where
    //      to store their entries:
    //
    //      1) KODACHI_PERM_CACHE              "/usr/pic1/kodachi_cache"
    //          If not set, we will use whatever internal::CacheBase::getTempDirPath() returns,
    //          most likely /usr/render_tmp/kodachi_cache_PID
    //
    //      2) KODACHI_TEMP_CACHE              "/usr/render_temp/kodachi_cache"
    //          If not set, we will use whatever internal::CacheBase::getTempDirPath() returns
    //          most likely /usr/render_tmp/kodachi_cache_PID
    //
    //  Optional:
    //      1) KODACHI_DISABLE_MEM_CACHE       0/1
    //          Use this env variable to enable/disable in-memory caching on all
    //          Kodachi caches at runtime.
    //
    //      2) KODACHI_DISABLE_DISK_CACHE      0/1
    //          Use this env variable to enable/disable on-disk caching on all
    //          Kodachi caches at runtime.
    //

    template <typename key_t,
              typename value_t,
              typename metadata_t,
              std::uint64_t (* KeyHashFunc)              (const key_t&),
              value_t       (* CreateValueFunc)          (const key_t&, metadata_t*), /* Must be thread-safe */
              bool          (* IsValidFunc)              (const value_t&),
              value_t       (* ReadValueFromDiskFunc)    (const std::string& /* file name */),
              void          (* WriteValueToDiskFunc)     (const value_t&, const std::string& /* file name */),
              std::size_t   (* ApproximateValueSizeFunc) (const value_t&) = defaultValueSizeApproximate>
    class KodachiCache : public internal::CacheBase
    {
    public:
        struct cache_entry {
            cache_entry() { }

            cache_entry(std::size_t size, value_t value)
                : mSize(size), mValue(value) { }

            cache_entry(value_t value)
                : mSize(ApproximateValueSizeFunc(value)), mValue(value) { }

            cache_entry(const cache_entry&) = default;
            cache_entry& operator=(const cache_entry&) = default;

            cache_entry(cache_entry&&) = default;
            cache_entry& operator=(cache_entry&&) = default;

            inline bool valid() const { return IsValidFunc(mValue); }

            std::size_t mSize = 0;
            value_t     mValue;
        };

        using LRU_t                   = std::list<std::uint64_t /* key hash value */>;
        using LRUIter_t               = std::list<std::uint64_t /* key hash value */>::iterator;
        using Ptr_t                   = std::shared_ptr<KodachiCache>;
        using FuturesMap              = tbb::concurrent_hash_map<std::uint64_t, std::pair<std::shared_future<cache_entry>, LRUIter_t>>;
        using FuturesMapAccessor      = typename FuturesMap::accessor;
        using FuturesMapConstAccessor = typename FuturesMap::const_accessor;

        static KodachiCache::Ptr_t
        createCache(const kodachi::GroupAttribute& settings, const std::string& scope)
        {
            static const std::string enabled_str             = "enabled";
            static const std::string memory_enabled_str      = "memory_enabled";
            static const std::string disk_enabled_str        = "disk_enabled";
            static const std::string regenerate_str          = "regenerate";
            static const std::string debug_messages_str      = "debug_messages";
            static const std::string force_permanent_str     = "force_permanent";
            static const std::string force_temporary_str     = "force_temporary";
            static const std::string is_permanent_str        = "is_permanent";
            static const std::string max_size_gb_str         = "max_size_gb";
            static const std::string permanent_cache_loc_str = "permanent_cache_loc";
            static const std::string temporary_cache_loc_str = "temporary_cache_loc";
            static const std::string enable_eviction_str     = "enable_eviction";

            const kodachi::GroupAttribute globalSettingsAttr =
                    settings.getChildByName("global");

            const kodachi::GroupAttribute localSettingsAttr =
                    settings.getChildByName(scope);

            bool isMemoryEnabled = true;
            bool isDiskEnabled   = true;
            bool isEnabled       = true;

            const auto isEnabledVal = getSetting<kodachi::IntAttribute>(globalSettingsAttr, localSettingsAttr, enabled_str);
            if (isEnabledVal.first == true) {
                isEnabled = isEnabledVal.second;
            }

            // If isEnabled is false, then both in-memory and on-disk caches are disabled.
            if (!isEnabled) {
                isMemoryEnabled = false;
                isDiskEnabled   = false;
            }
            // If isEnabled is true, then check if in-memory or on-disk caches are disabled.
            else {
                const auto isMemoryEnabledVal = getSetting<kodachi::IntAttribute>(globalSettingsAttr, localSettingsAttr, memory_enabled_str);
                if (isMemoryEnabledVal.first == true) {
                    isMemoryEnabled = (isMemoryEnabledVal.second == 1);
                }
                else {
                    // NOTE: env variable KODACHI_DISABLE_MEM_CACHE is set to '1' if in-memory cache
                    //       is DISABLED, and 0 if it is ENABLED.
                    //
                    const char* isMemoryDisabledCStr = ::getenv("KODACHI_DISABLE_MEM_CACHE");
                    if (isMemoryDisabledCStr != nullptr) {
                        isMemoryEnabled = (isMemoryDisabledCStr[0] != '1');
                    }
                }

                const auto isDiskEnabledVal = getSetting<kodachi::IntAttribute>(globalSettingsAttr, localSettingsAttr, disk_enabled_str);
                if (isDiskEnabledVal.first == true) {
                    isDiskEnabled = (isDiskEnabledVal.second == 1);
                }
                else {
                    // NOTE: env variable KODACHI_DISABLE_DISK_CACHE is set to '1' if on-disk cache
                    //       is DISABLED, and 0 if it is ENABLED.
                    //
                    const char* isDiskDisabledCStr = ::getenv("KODACHI_DISABLE_DISK_CACHE");
                    if (isDiskDisabledCStr != nullptr) {
                        isDiskEnabled = (isDiskDisabledCStr[0] != '1');
                    }
                }
            }

            const bool regenerateCache = getSetting<kodachi::IntAttribute>(globalSettingsAttr, localSettingsAttr, regenerate_str).second == 1;
            const bool printDebug      = getSetting<kodachi::IntAttribute>(globalSettingsAttr, localSettingsAttr, debug_messages_str).second == 1;

            const bool forcePermanent  = getSetting<kodachi::IntAttribute>(globalSettingsAttr, force_permanent_str).second == 1; // Only a global setting
            const bool forceTemporary  = (forcePermanent ? false : getSetting<kodachi::IntAttribute>(globalSettingsAttr, force_temporary_str).second == 1); // Only a global setting

            const bool isPermanent = (forcePermanent ? true :
                    (forceTemporary ? false : getSetting<kodachi::IntAttribute>(localSettingsAttr, is_permanent_str).second == 1)); // Only a local setting

            float maxInMemorySizeGB = 0.0f;
            auto maxInMemorySizeGBVal = getSetting<kodachi::FloatAttribute>(globalSettingsAttr, localSettingsAttr, max_size_gb_str);
            if (maxInMemorySizeGBVal.first) {
                maxInMemorySizeGB = maxInMemorySizeGBVal.second;
            }
            else {
                maxInMemorySizeGB = 1000.0f; // 1,000 GB effectively means "unlimited" here
            }

            std::string cacheRootPath;
            if (isDiskEnabled && (isPermanent || forcePermanent)) {
                cacheRootPath = getSetting<kodachi::StringAttribute>(globalSettingsAttr, localSettingsAttr, permanent_cache_loc_str).second;

                // If kodachi.cache.SCOPE.permanent_cache_loc attribute is not set, try to read env variable KODACHI_PERM_CACHE.
                if (cacheRootPath.empty()) {
                    const char* cacheRootPathCStr = ::getenv("KODACHI_PERM_CACHE");
                    if (cacheRootPathCStr != nullptr && cacheRootPathCStr[0] != '\0') {
                        cacheRootPath = std::string(cacheRootPathCStr);
                    }
                }
            }
            else if (isDiskEnabled && (!isPermanent || forceTemporary)) {
                cacheRootPath = getSetting<kodachi::StringAttribute>(globalSettingsAttr, localSettingsAttr, temporary_cache_loc_str).second;

                // If kodachi.cache.SCOPE.temporary_cache_loc attribute is not set, try to read env variable KODACHI_PERM_CACHE.
                if (cacheRootPath.empty()) {
                    const char* cacheRootPathCStr = ::getenv("KODACHI_TEMP_CACHE");
                    if (cacheRootPathCStr != nullptr && cacheRootPathCStr[0] != '\0') {
                        cacheRootPath = std::string(cacheRootPathCStr);
                    }
                }
            }

            bool evictionEnabled = true;
            auto evictionEnabledVal = getSetting<kodachi::IntAttribute>(globalSettingsAttr, localSettingsAttr, enable_eviction_str);
            if (evictionEnabledVal.first) {
                 evictionEnabled = evictionEnabledVal.second;
            }

            KodachiCache::Ptr_t newCache {
                new KodachiCache(scope,
                                 isMemoryEnabled,
                                 isDiskEnabled,
                                 isPermanent,
                                 std::move(cacheRootPath),
                                 regenerateCache,
                                 printDebug,
                                 evictionEnabled,
                                 static_cast<std::size_t>(maxInMemorySizeGB * 1024.0f * 1024.0f * 1024.0f)),
                internal::CacheBase::deleter };

            CacheRegistry::registerCache(std::dynamic_pointer_cast<internal::CacheBase>(newCache));
            return newCache;
        }

        const std::string& getRootPath() const override { return mDiskCacheRootPath; }
        const std::string& getCachePath() const override { return mDiskCachePath; }

        void clear(Cache::ClearAction action) override
        {
            if ((action & Cache::ClearAction::MEMORY) == Cache::ClearAction::MEMORY) {
                // Clearing the map like this should still be OK since any thread
                // waiting on a std::shared_future will continue to have access to
                // the shared state.
                // So there is a chance this doesn't *immediately* release all the
                // memory occupied by this cache.
                //

                std::lock_guard<std::mutex> lock(mEvictionMutex);

                mEntries.clear();
                mLRUTable.clear();
                mCurrentSizeInMemory.store( 0 );
            }

            if (mIsInitialized && mIsDiskCacheEnabled) {
                if ((action & Cache::ClearAction::DISK_CONTENTS) == Cache::ClearAction::DISK_CONTENTS) {
                    cache_utils::removeDirectoryContents(mDiskCachePath);
                }
                else if ((action & Cache::ClearAction::DISK_SCOPE_DIR) == Cache::ClearAction::DISK_SCOPE_DIR) {
                    cache_utils::removeDirectory(mDiskCachePath);
                }
                else if ((action & Cache::ClearAction::DISK_TOP_DIR) == Cache::ClearAction::DISK_TOP_DIR) {
                    cache_utils::removeDirectory(mDiskCacheRootPath);
                }
            }
        }

        value_t getValue(const key_t& key, metadata_t* metadata = nullptr)
        {
            std::call_once(mInitializedOnceFlag, &KodachiCache::initialize, this);

            //-------------------------------------------------------
            // If both in-memory and on-disk caches are disabled, then
            // create the value and return immediately.

            if (!mIsMemCacheEnabled && !mIsDiskCacheEnabled) {
                if (mPrintDebug) {
                    cache_log(mScope, "cache is disabled; creating the value...");
                }

                return CreateValueFunc(key, metadata);
            }

            //-------------------------------------------------------
            // In-memory cache query/update

            const std::uint64_t keyHash = KeyHashFunc(key);

            cache_entry cacheEntry;

            if (mIsMemCacheEnabled) {
                // Check in-memory cache first; if entry found, this thread will either
                // get to use the valid shared state immediately, or has to wait for it
                // to become valid (in case the value is being generated by another thread).
                //
                if (!mEntries.empty()) {
                    FuturesMapConstAccessor constAccessor;
                    if (mEntries.find(constAccessor, keyHash)) {
                        std::shared_future<cache_entry> sharedFuture = constAccessor->second.first;
                        value_t value = sharedFuture.get().mValue;

                        mLRUTable.update(keyHash, constAccessor->second.second);

                        if (mPrintDebug) {
                            cache_log(mScope, "cache entry found in memory.");
                        }

                        return value;
                    }
                }

                // Reaching this point means this thread is the first to ask for this particular
                // value.
                // If on-disk cache is disabled, create the value and return.
                // If on-disk cache is enabled, skip this part and try to read the value off of disk.
                //
                if (!mIsDiskCacheEnabled) {
                    if (mPrintDebug) {
                        cache_log(mScope, "cache entry not found in memory, creating the value...");
                    }

                    cacheEntry = findOrCreateValue(key, metadata);

                    if (mEvictionEnabled) {
                        evict();
                    }

                    return cacheEntry.mValue;
                }
            }

            //-------------------------------------------------------
            // On-disk cache query/update:

            if (mIsDiskCacheEnabled) {
                // First try to find the cache entry on disk.
                const std::string cacheEntryFilePath = generateCacheEntryFilePath(keyHash);

                if (cache_utils::fileOrDirExists(cacheEntryFilePath)) {
                    //----------------------------
                    // Reading the value from disk and then converting it to value_t can
                    // be expensive, so we want to prevent other threads from wasting resources
                    // while this thread is busy; therefore:
                    //
                    // If in-memory cache is enabled, create a promise here, and add a new
                    // entry to the map so once the value is ready other threads can ask for
                    // the final result through a shared_future.
                    //

                    // If "regenerate" is set, remove the cache entry if it was created prior to
                    // the start time of the current process.
                    if (mRegenerateCache) {
                        const std::uint64_t timeFileLastMod =
                                kodachi::cache_utils::getTimeLastModified<std::chrono::nanoseconds>(cacheEntryFilePath);

                        // If file was last modified (here it would be the creation time) before current process started,
                        // remove it as it is old and "regenerate" flag is set.
                        if (timeFileLastMod < mProcessCreationTime) {
                            const bool removed = std::remove(cacheEntryFilePath.c_str()) == 0 /* success */;

                            if (mPrintDebug) {
                                if (removed) {
                                    cache_log(mScope, "cache entry removed from disk (regenerate is on) [" + cacheEntryFilePath + "].");
                                }
                                else {
                                    cache_log(mScope, "failed to remove cache entry from disk (regenerate is on) [" + cacheEntryFilePath + "].");
                                }
                            }
                        }
                    }
                    else {
                        std::promise<cache_entry> promiseToGetValFromDisk; // only used if in-memory cache is enabled

                        if (mIsMemCacheEnabled) {
                            // Decide whether or not this is the first thread needing to read
                            // this value from disk
                            FuturesMapConstAccessor constAccessor;

                            // If found, get (or wait for) the value, then return
                            if (mEntries.find(constAccessor, keyHash)) {
                                if (mPrintDebug) {
                                    cache_log(mScope, "waiting for another thread to create the value...");
                                }

                                std::shared_future<cache_entry> sharedFuture = constAccessor->second.first;
                                cacheEntry = sharedFuture.get(); // wait for the value...

                                if (mPrintDebug) {
                                    cache_log(mScope, "value created by another thread.");
                                }

                                return cacheEntry.mValue;
                            }
                            // Not found in the map, then this must be the first thread; update the
                            // map, then proceed with reading the value from disk.
                            else {
                                FuturesMapAccessor accessor;
                                if (mEntries.insert(accessor, keyHash)) {
                                    accessor->second = { promiseToGetValFromDisk.get_future(), mLRUTable.update(keyHash) };
                                }
                            }
                        }

                        //----------------------------
                        // Read the value from disk into a buffer

                        // Note on exception handling:
                        //
                        // 1) In case of an exception here, we can update the shared state
                        // with the exception so that all shared_future holders receive the
                        // exception and then are forced to handle the exception (for instance,
                        // building and returning a value_t that holds an error message).
                        //
                        // 2) An alternative, and easier, approach is to catch the exception
                        // here and ignore it, but update the shared state with a default
                        // constructed value_t so that other threads waiting on this promise
                        // are not blocked forever; instead they end up returning an invalid
                        // value_t which we expect to be handled correctly by getValue() caller.
                        //
                        // To keep things simple, and avoid using try-catch blocks around all
                        // interactions with shared_future's, we are going to go with (2) until
                        // we have a good reason to go with (1).
                        //
                        try {
                            if (mPrintDebug) {
                                cache_log(mScope, "reading the value from disk [" + cacheEntryFilePath + "].");
                            }

                            cacheEntry.mValue = ReadValueFromDiskFunc(cacheEntryFilePath);
                            cacheEntry.mSize = ApproximateValueSizeFunc(cacheEntry.mValue);
                            mCurrentSizeInMemory.fetch_add(cacheEntry.mSize);

                            // If in-memory cache is enabled, update the shared state.
                            // NOTE: even if cacheEntry.mValue is an invalid value_t, we have to do
                            // this so other threads are not blocked forever waiting for this promise.
                            if (mIsMemCacheEnabled) {
                                promiseToGetValFromDisk.set_value(cacheEntry);
                            }
                        }
                        catch (std::exception& e) {
                            // Uncomment to update the shared state with the exception e
                            //promiseToGetValFromDisk.set_exception(std::make_exception_ptr(e));

                            if (mPrintDebug) {
                                cache_log(mScope, "failed to read the value from disk [" + cacheEntryFilePath + "].");
                            }

                            // Update the shared state with a default-constructed value
                            promiseToGetValFromDisk.set_value(cache_entry{ });
                            return { };
                        }

                        // We can return from here if cacheEntry.mValue is a valid value_t,
                        // otherwise, skip this part and proceed to generate the value from
                        // scratch, in which case we will also have to update the corresponding
                        // entry in the map.
                        //
                        if (cacheEntry.valid()) {
                            if (mPrintDebug) {
                                cache_log(mScope, "value successfully read from disk [" + cacheEntryFilePath + "].");
                            }

                            if (mIsMemCacheEnabled && mEvictionEnabled) {
                                evict();
                            }

                            return cacheEntry.mValue;
                        }
                    }
                }

                // So far failed to locate a valid cache entry either in memory or on disk;
                // therefore:
                //      create the value from scratch,
                //      write it to disk,
                //      write/update in-memory cache (if enabled),
                //      return the value
                //
                // If in-memory cache is enabled then:
                //      create the value, or wait for another thread to create it, then
                //      update the map
                if (mIsMemCacheEnabled) {
                    cacheEntry = findOrCreateValue(key, metadata);
                }
                // If in-memory cache is disabled, we have to create the value here
                else {
                    cacheEntry.mValue = CreateValueFunc(key, metadata);
                    cacheEntry.mSize  = ApproximateValueSizeFunc(cacheEntry.mValue);
                    mCurrentSizeInMemory.fetch_add(cacheEntry.mSize);
                }

                // Something is very wrong, just return an empty value_t!
                if (!cacheEntry.valid()) {
                    if (mPrintDebug) {
                        cache_log(mScope, "failed to create value using the provided key.");
                    }

                    return { };
                }

                if (mPrintDebug) {
                    cache_log(mScope, "write the newly created value to disk [" + cacheEntryFilePath + "]...");
                }

                WriteValueToDiskFunc(cacheEntry.mValue, cacheEntryFilePath);
            }

            //-------------------------------------------------------

            if (mPrintDebug) {
                cache_log(mScope, "value found in cache.");
            }

            if (mIsMemCacheEnabled && mEvictionEnabled) {
                evict();
            }

            return cacheEntry.mValue;
        }

        void cacheValueToDisk(const key_t& key, metadata_t* metadata = nullptr)
        {
            std::call_once(mInitializedOnceFlag, &KodachiCache::initialize, this);

            if (!mIsDiskCacheEnabled) {
                return;
            }

            const std::uint64_t keyHash = KeyHashFunc(key);
            const std::string cacheEntryFilePath = generateCacheEntryFilePath(keyHash);

            if (cache_utils::fileOrDirExists(cacheEntryFilePath)) {
                if (mRegenerateCache) {
                    const std::uint64_t timeFileLastMod =
                            kodachi::cache_utils::getTimeLastModified<std::chrono::nanoseconds>(cacheEntryFilePath);

                    // If file was last modified (here it would be the creation time) before current process started,
                    // remove it as it is old and "regenerate" flag is set.
                    if (timeFileLastMod < mProcessCreationTime) {
                        const bool removed = std::remove(cacheEntryFilePath.c_str()) == 0 /* success */;

                        if (mPrintDebug) {
                            if (removed) {
                                cache_log(mScope, "(dev note) KodachiCache::makeValue() : cache entry removed from disk (regenerate is on) [" + cacheEntryFilePath + "].");
                            }
                            else {
                                cache_log(mScope, "(dev note) KodachiCache::makeValue() : failed to remove cache entry from disk (regenerate is on) [" + cacheEntryFilePath + "].");
                            }
                        }
                    }
                }
                // If file already exists and "regenerate" is not set, return
                else {
                    return;
                }
            }

            auto newValue = CreateValueFunc(key, metadata);

            // Something is very wrong, just return an empty value_t!
            if (!newValue.isValid()) {
                if (mPrintDebug) {
                    cache_log(mScope, "(dev note) KodachiCache::makeValue() : failed to create value using the provided key.");
                }

                return;
            }

            if (mPrintDebug) {
                cache_log(mScope, "(dev note) KodachiCache::makeValue() : write the newly created value to disk [" + cacheEntryFilePath + "]...");
            }

            WriteValueToDiskFunc(newValue, cacheEntryFilePath);
        }

    protected:
        KodachiCache(const std::string& scope,
                     bool isMemCacheEnabled,
                     bool isDiskCacheEnabled,
                     bool isPermanent,
                     std::string && diskCacheRootPath,
                     bool regenerateCache,
                     bool printDebug,
                     bool enableEviction,
                     std::uint64_t maxInMemorySize)
            : CacheBase(scope)
            , mIsMemCacheEnabled(isMemCacheEnabled)
            , mIsDiskCacheEnabled(isDiskCacheEnabled)
            , mIsPermanent(isPermanent)
            , mDiskCacheRootPath(std::move(diskCacheRootPath))
            , mRegenerateCache(regenerateCache)
            , mPrintDebug(printDebug)
            , mEvictionEnabled(enableEviction)
            , mProcessCreationTime(kodachi::cache_utils::getTimeThisProcessStarted<std::chrono::nanoseconds>())
            , mMaxSizeInMemory(maxInMemorySize)
            , mLRUTable(printDebug)
            , mRNG((std::time(nullptr)))
        {
        }

        ~KodachiCache() { }

        // Adding these to enable more control through Python;
        // A mutex is not required since these are not supposed
        // to be called *while* the cache is in use (e.g., while rendering).
        void enableMemoryCache() override { mIsMemCacheEnabled = true; }
        void disableMemoryCache() override { mIsMemCacheEnabled = false; }

        void enableDiskCache() override { mIsDiskCacheEnabled = true; }
        void disableDiskCache() override { mIsDiskCacheEnabled = false; }

        std::size_t getInMemoryEntryCount() const override { return mEntries.size(); }
        std::size_t getCurrentSizeInMemory() const override { return mCurrentSizeInMemory.load(); }

        static void
        cache_log(const std::string& msg, KdLoggingSeverity severity = kKdLoggingSeverityDebug)
        {
            get_logging_client().log(msg, severity);
        }

        static void
        cache_log(const std::string& scope, const std::string& msg, KdLoggingSeverity severity = kKdLoggingSeverityDebug)
        {
            const std::string logmsg = "[" + scope + " cache] " + msg;
            get_logging_client().log(logmsg, severity);
        }

    private:
        static kodachi::KodachiLogging &
        get_logging_client()
        {
            static kodachi::KodachiLogging sKdLoggingClient("KodachiCache");
            return sKdLoggingClient;
        }

        void initialize()
        {
            if (mIsDiskCacheEnabled) {
                if (mDiskCacheRootPath.empty()) {
                    mDiskCacheRootPath = getTempDirPath();
                }
                else {
                    static const std::string sCacheDirName { "/kodachi_cache" };
                    mDiskCacheRootPath += sCacheDirName;
                }

                std::string rezPackages;
                generateDiskCacheDirPath(rezPackages); // Sets mDiskCachePath using mDiskCacheRootPath

                mIsDiskCacheEnabled = cache_utils::initializeCacheOnDisk(mDiskCachePath, rezPackages);
            }

            //------------------------------------

            constexpr double bytes_to_gb = 1.0 / (1024.0 * 1024.0 * 1024.0);
            std::ostringstream oss;

            cache_log("", kKdLoggingSeverityInfo);
            cache_log("----- Kodachi Cache Settings  ------", kKdLoggingSeverityInfo);
            cache_log("", kKdLoggingSeverityInfo);

            oss << "Initializing " << mScope << " cache:";
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            cache_log("", kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Enabled?          " << (mIsMemCacheEnabled || mIsDiskCacheEnabled ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Memory enabled?   " << (mIsMemCacheEnabled ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Disk enabled?     " << ( mIsDiskCacheEnabled ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Permanent?        " << (mIsPermanent ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Regenerate?       " << (mRegenerateCache ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Debug logs?       " << (mPrintDebug ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Eviction Enabled? " << (mEvictionEnabled ? "Yes" : "No");
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Max memory size   " << static_cast<double>(mMaxSizeInMemory) * bytes_to_gb << " GB";
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            oss.str(""); oss.clear();
            oss << "     Location          " << mDiskCachePath;
            cache_log(oss.str(), kKdLoggingSeverityInfo);

            cache_log("", kKdLoggingSeverityInfo);
            cache_log("--------------------------------------", kKdLoggingSeverityInfo);
            cache_log("", kKdLoggingSeverityInfo);

            //------------------------------------

            mIsInitialized = true;
        }

        cache_entry findOrCreateValue(const key_t& key, metadata_t* metadata_ptr = nullptr)
        {
            const std::uint64_t keyHash = KeyHashFunc(key);

            cache_entry entry;

            // Check if another thread is already working on creating this value.
            FuturesMapConstAccessor constAccessor;

            // If found, get the value from the shared_future; if the value
            // is currently being generated, wait for the shared state to become
            // valid; otherwise, the future already holds the value.
            if (mEntries.find(constAccessor, keyHash)) {
                std::shared_future<cache_entry> future = constAccessor->second.first;
                entry = future.get();

                // Update LRU table
                mLRUTable.update(keyHash, constAccessor->second.second);
            }
            // If not found, this must be the first thread to ask for this value.
            // Make a promise and add a shared_future to the map for access by other
            // threads.
            else {
                std::promise<cache_entry> promise;
                bool insertionSuccess = false;
                {
                    FuturesMapAccessor accessor;
                    insertionSuccess = mEntries.insert(accessor, keyHash);

                    // a) If insertion succeeds, then this is the first thread to ask
                    // for this value to be created and now has a write lock on the entry;
                    // hold the lock and create the shared_future, then release the lock
                    // and proceed to value creation.
                    if (insertionSuccess) {
                        // Update LRU table
                        accessor->second = { promise.get_future(), mLRUTable.update(keyHash) };
                    }
                }

                // This is the first thread; create the value and set it through
                // the promise
                if (insertionSuccess) {
                    try {
                        entry.mValue = CreateValueFunc(key, metadata_ptr);
                        entry.mSize  = ApproximateValueSizeFunc(entry.mValue);
                        mCurrentSizeInMemory.fetch_add(entry.mSize);
                        promise.set_value(entry);
                    }
                    catch (std::exception& e) {
                        // Uncomment to update the shared state with exception e
                        //promise.set_exception(std::make_exception_ptr(e));

                        cache_log(mScope, "failed to create and insert value.", kKdLoggingSeverityError);

                        // Update the shared state with a default-constructed value
                        promise.set_value(cache_entry{ });
                        return { };
                    }
                }
                // b) If insertion fails, then another thread must have already
                // committed to fulfilling the promise; drop the write lock, and
                // access the shared state through a const accessor, then wait
                // for the first thread to update the shared state.
                else if (mEntries.find(constAccessor, keyHash)) {
                    std::shared_future<cache_entry> future = constAccessor->second.first;
                    entry = future.get();

                    mLRUTable.update(keyHash, constAccessor->second.second);
                }
                // Something's wrong!
                else {
                    cache_log(mScope, "failed to insert/retrieve value.", kKdLoggingSeverityError);
                }
            }

            return entry;
        }

        std::string generateCacheEntryFilePath(std::uint64_t hash) const
        {
            std::ostringstream oss;
            oss << mDiskCachePath << "/" << hash;
            return oss.str();
        }

        // Based on input string [ path ], environment variable REZ_RESOLVE,
        // and string [ mScope ], builds the path to the cache location on disk,
        // Example:
        //      path                          == "/some_dir"
        //      hash of env variable value(s) == 2013815268070794411
        //      scope                         == "ScatterPointsOp"
        //
        //      Result == "/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp/"
        //
        void generateDiskCacheDirPath(std::string& rezResolve)
        {
            const char* katanaVerCStr = getenv("REZ_KODACHI_VERSIONS_VERSION");
            if (katanaVerCStr != nullptr && katanaVerCStr[0] != '\0') {
                rezResolve += ("kodachi_versions-" + std::string(katanaVerCStr) + "\n");
            }

            const char* kodachiVerCStr = getenv("REZ_KODACHI_VERSION");
            if (kodachiVerCStr != nullptr && kodachiVerCStr[0] != '\0') {
                rezResolve += ("kodachi-" + std::string(kodachiVerCStr) + "\n");
            }

            const char* kodachiMoonrayVerCStr = getenv("REZ_KODACHI_MOONRAY_VERSION");
            if (kodachiMoonrayVerCStr != nullptr && kodachiMoonrayVerCStr[0] != '\0') {
                rezResolve += ("kodachi_moonray-" + std::string(kodachiMoonrayVerCStr) + "\n");
            }

            const char* moonshineVerCStr = getenv("REZ_MOONSHINE_VERSION");
            if (moonshineVerCStr != nullptr && moonshineVerCStr[0] != '\0') {
                rezResolve += ("moonshine-" + std::string(moonshineVerCStr) + "\n");
            }

            const char* usdCoreVerCStr = getenv("REZ_USD_CORE_VERSION");
            if (usdCoreVerCStr != nullptr && usdCoreVerCStr[0] != '\0') {
                rezResolve += ("usd_core-" + std::string(usdCoreVerCStr) + "\n");
            }

            const std::string hashStr =
                    std::to_string(std::hash<std::string>{}(rezResolve));

            mDiskCachePath = mDiskCacheRootPath + "/" + hashStr + "/" + mScope;
        }

        // Randomly free ~[pct] of memory used (default pct == 0.25f)
        void
        entryEvictionRandom(float pct = 0.25f)
        {
            constexpr float bytes_to_gb = 1.0f / (1024.0f * 1024.0f * 1024.0f);

            if (mEntries.empty()) {
                return;
            }

            float currSizeGB = static_cast<float>(mCurrentSizeInMemory.load()) * bytes_to_gb;

            if (mPrintDebug) {
                cache_log(mScope, "Random eviction running. Current cache size is " + std::to_string(currSizeGB) + " GB.");
            }

            const std::size_t desiredSize =
                    static_cast<std::size_t>(mCurrentSizeInMemory.load() * (1.0f - pct));

            std::uint32_t iterCounter = 0;
            const std::uint32_t entryCount = mEntries.size();
            while (!mEntries.empty() &&
                    iterCounter < entryCount &&
                        mCurrentSizeInMemory.load() > desiredSize)
            {
                ++iterCounter;

                auto it = mEntries.begin();
                std::advance(it, mRNG() % mEntries.size()); // select a random entry

                FuturesMapAccessor accessor;
                if (mEntries.find(accessor, it->first)) {
                    if (is_shared_future_ready(accessor->second.first)) {
                        const auto size = accessor->second.first.get().mSize;
                        mCurrentSizeInMemory.fetch_sub(size); // update current size

                        mLRUTable.erase(accessor->second.second);

                        mEntries.erase(accessor); // remove cache entry
                    }
                }
            }

            if (mPrintDebug) {
                currSizeGB = static_cast<float>(mCurrentSizeInMemory.load()) * bytes_to_gb;
                cache_log(mScope, "Random eviction finished. Current cache size is " + std::to_string(currSizeGB) + " GB.");
            }
        }

        // LRU eviction
        void
        entryEvictionLRU(float pct = 0.25f)
        {
            constexpr float bytes_to_gb = 1.0f / (1024.0f * 1024.0f * 1024.0f);

            if (mEntries.empty()) {
                return;
            }

            float currSizeGB = static_cast<float>(mCurrentSizeInMemory.load()) * bytes_to_gb;

            if (mPrintDebug) {
                cache_log(mScope, "LRU eviction running. Current cache size is " + std::to_string(currSizeGB) + " GB.");
            }

            const std::uint64_t desiredSize =
                    static_cast<std::uint64_t>(mCurrentSizeInMemory.load() * (1.0f - pct));

            std::uint32_t iterCounter = 0;
            const std::uint32_t entryCount = mEntries.size();
            while (!mEntries.empty() &&
                    iterCounter < entryCount &&
                        mCurrentSizeInMemory.load() > desiredSize)
            {
                ++iterCounter;

                const std::uint64_t keyHash = mLRUTable.back();

                FuturesMapAccessor accessor;
                if (mEntries.find(accessor, keyHash)) {
                    if (is_shared_future_ready(accessor->second.first)) {
                        const auto size = accessor->second.first.get().mSize;
                        mCurrentSizeInMemory.fetch_sub(size); // update current size

                        mEntries.erase(accessor); // remove cache entry
                    }
                }
                // Remove from LRU table if unable to locate this key among cache entries.
                else {
                    mLRUTable.pop(); // remove LRU table entry
                }
            }

            if (mPrintDebug) {
                currSizeGB = static_cast<float>(mCurrentSizeInMemory.load()) * bytes_to_gb;
                cache_log(mScope, "LRU eviction finished. Current cache size is " + std::to_string(currSizeGB) + " GB.");
            }
        }

        void
        evict(float pct = 0.25f)
        {
            std::lock_guard<std::mutex> lock(mEvictionMutex);

            // Evict 25% of cache entries if needed
            if (mCurrentSizeInMemory.load() >= mMaxSizeInMemory) {
                entryEvictionLRU(0.25f);
            }

            if (mCurrentSizeInMemory.load() >= mMaxSizeInMemory) {
                entryEvictionRandom(0.25f);
            }
        }

        //-----------------------------------------------------------------

        KodachiCache(const KodachiCache&) = delete;
        KodachiCache& operator=(const KodachiCache&) = delete;

        KodachiCache(KodachiCache&&) = delete;
        KodachiCache& operator=(KodachiCache&&) = delete;

        //-----------------------------------------------------------------

        class LRUTable {
        public:
            static constexpr std::uint64_t sInvalidKey = std::numeric_limits<std::uint64_t>::max();

            explicit LRUTable(bool printDebug) : mPrintDebug(printDebug) { }

            LRUTable()                = delete;
            LRUTable(const LRUTable&) = delete;
            LRUTable(LRUTable&&)      = delete;

            // Assume this is only called when a *new entry* is added to the cache,
            // which means we can avoid looking for duplicates.
            //
            LRUIter_t
            update(std::uint64_t keyHash)
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mList.emplace_front(keyHash);
                return mList.begin();
            }

            LRUIter_t
            update(std::uint64_t keyHash, LRUIter_t LRUIter)
            {
                // Make sure the iterator points to the correct key
                if (*LRUIter != keyHash) {

                    if (mPrintDebug) {
                        std::ostringstream oss;
                        oss << "(dev note) LRU iterator points to the wrong key; LRU iterator points to ["
                            << *LRUIter << "], but the key is expected to be [" << keyHash << "]";

                        cache_log(oss.str(), kKdLoggingSeverityError);
                    }

                    return mList.end();
                }

                std::lock_guard<std::mutex> lock(mMutex);
                mList.splice(mList.begin(), mList, LRUIter); // move to front
                return LRUIter;
            }

            std::uint64_t /* key hash */
            back() const
            {
                std::lock_guard<std::mutex> lock(mMutex);

                if (mList.empty()) {
                    return sInvalidKey;
                }

                const std::uint64_t keyHash = *(--mList.end());
                return keyHash;
            }

            std::uint64_t /* key hash */
            pop()
            {
                std::lock_guard<std::mutex> lock(mMutex);

                if (mList.empty()) {
                    return sInvalidKey;
                }

                const std::uint64_t keyHash = *(--mList.end());
                mList.pop_back();

                return keyHash;
            }

            void
            erase(LRUIter_t LRUIter)
            {
                std::lock_guard<std::mutex> lock(mMutex);
                if (!mList.empty()) {
                    mList.erase(LRUIter);
                }
            }

            void
            clear()
            {
                std::lock_guard<std::mutex> lock(mMutex);
                mList.clear();
            }

            std::size_t
            size() const { return mList.size(); }

        private:
            const bool mPrintDebug = false;

            mutable std::mutex mMutex;

            LRU_t mList;
        };

        //-----------------------------------------------------------------

        std::once_flag mInitializedOnceFlag;
        bool mIsInitialized      = false;
        bool mIsMemCacheEnabled  = true;
        bool mIsDiskCacheEnabled = true;
        bool mIsPermanent        = false;
        bool mRegenerateCache    = false;
        bool mPrintDebug         = false;
        bool mEvictionEnabled    = true;

        std::uint64_t mProcessCreationTime;

        const std::size_t mMaxSizeInMemory;
        std::atomic<std::uint64_t> mCurrentSizeInMemory { 0ul };

        std::string mDiskCacheRootPath;
        std::string mDiskCachePath;

        std::mutex mEvictionMutex;

        LRUTable   mLRUTable;
        FuturesMap mEntries;

        std::mt19937 mRNG;
    };
} // namespace kodachi

