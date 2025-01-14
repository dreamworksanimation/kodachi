// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// C++
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

// TBB
#include <tbb/concurrent_unordered_map.h>

// Internal
#include "CacheUtils.h"

namespace kodachi
{
    namespace Cache
    {
        enum class ClearAction : std::uint32_t
        {
            // Only clear cache entries stored in main memory (RAM).
            MEMORY         = (1u << 0),

            // Remove all on-disk cache entries (files) *without* removing cache top level and scope directories.
            //
            // For instance, assuming entries of "ScatterPointsOp" cache are stored here:
            //      /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp
            //
            // Clearing DISK_CONTENTS is equivalent to following bash command:
            //      % rm /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp/*
            //
            DISK_CONTENTS  = (1u << 1),

            // Recursively remove the scope directory (and its contents) from disk.
            //
            // For instance, assuming entries of "ScatterPointsOp" cache are stored here:
            //      /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp
            //
            // Clearing DISK_SCOPE_DIR is equivalent to following bash command:
            //      % rm -rf /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp
            //
            DISK_SCOPE_DIR = (1u << 2),

            // Recursively remove the top-level cache directory (and its contents) from disk.
            //
            // For instance, assuming entries of "ScatterPointsOp" cache are stored here:
            //      /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp
            //
            // Clearing DISK_TOP_DIR is equivalent to following bash command:
            //      % rm -rf /usr/pic1/some_dir/kodachi_cache
            //
            DISK_TOP_DIR   = (1u << 3)
        };

        constexpr ClearAction operator | (ClearAction lhs, ClearAction rhs)
        {
            return static_cast<ClearAction>(
                    static_cast<std::underlying_type<ClearAction>::type>(lhs)
                    |
                    static_cast<std::underlying_type<ClearAction>::type>(rhs));
        }

        constexpr ClearAction operator & (ClearAction lhs, ClearAction rhs)
        {
            return static_cast<ClearAction>(
                    static_cast<std::underlying_type<ClearAction>::type>(lhs)
                    &
                    static_cast<std::underlying_type<ClearAction>::type>(rhs));
        }
    } // namespace Cache

    namespace internal
    {
        class TempDirManager
        {
        private:
            // An instance is the owner of KODACHI_TEMP_CACHE directory only if
            // it creates the directory on disk.
            bool mIsOwner;
            std::string mPath;

        public:
            TempDirManager();
            ~TempDirManager();

            const std::string& getPath() const
            {
                return mPath;
            }

            bool valid() const
            {
                return cache_utils::fileOrDirExists(mPath);
            }
        };

        class CacheBase
        {
        protected:
            static TempDirManager mTempDirMngr;
            const std::string mScope;

            // protected dtor to enforce dynamic allocation
            virtual ~CacheBase() { }
            static void deleter(CacheBase* ptr) { delete ptr; }

        public:
            using Ptr_t = std::shared_ptr<CacheBase>;

            CacheBase() = delete;

            CacheBase(const CacheBase&) = delete;
            CacheBase& operator=(const CacheBase&) = delete;

            CacheBase(CacheBase&&) = delete;
            CacheBase& operator=(CacheBase&&) = delete;

            CacheBase(const std::string& scope) : mScope(scope) { }

            const std::string& getTempDirPath() const { return mTempDirMngr.getPath(); }
            const std::string& getScope() const { return mScope; }

            virtual const std::string& getRootPath() const = 0;
            virtual const std::string& getCachePath() const = 0;

            virtual void clear(Cache::ClearAction loc) = 0;

            virtual std::size_t getInMemoryEntryCount() const = 0;
            virtual std::size_t getCurrentSizeInMemory() const = 0;

            virtual void enableMemoryCache()  = 0;
            virtual void disableMemoryCache() = 0;

            virtual void enableDiskCache()  = 0;
            virtual void disableDiskCache() = 0;
        };
    } // namespace internal

    class CacheRegistry
    {
    public:
        using CacheSet = std::unordered_set<internal::CacheBase::Ptr_t>;

        CacheRegistry() = delete;
        CacheRegistry(const CacheRegistry&) = delete;
        CacheRegistry(CacheRegistry&&) = delete;

        static bool registerCache(internal::CacheBase::Ptr_t newCache);

        static void enableDiskCache(const std::string& scope = "");
        static void disableDiskCache(const std::string& scope = "");

        static void enableMemoryCache(const std::string& scope = "");
        static void disableMemoryCache(const std::string& scope = "");

        static void clear(Cache::ClearAction action, const std::string& scope = "");

        static std::size_t count() { return kTable.size(); }

        static std::vector<std::string> getRegisteredScopes();

        static std::size_t getInMemoryEntryCount(const std::string& scope = "");
        static std::size_t getInMemoryCacheSize(const std::string& scope = "");

        static std::string getPathToScope(const std::string& scope);

    private:
        static tbb::concurrent_unordered_map<std::string, CacheSet> kTable;
    };
} // namespace kodachi

