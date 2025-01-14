// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "CacheRegistry.h"

namespace kodachi
{
    internal::TempDirManager internal::CacheBase::mTempDirMngr;

    tbb::concurrent_unordered_map<std::string, CacheRegistry::CacheSet> CacheRegistry::kTable;

    // If this instance of TempDirManager turns out to be the directory owner, it should set
    // KODACHI_CACHE_REUSABLE_PARENT_PROCESS_DIR environment variable to signal the existence
    // of a temp cache to child processes forked from the current process.
    //
    // For instance, katanaBin sets the environment variable and creates the directory on disk,
    // then it sets KODACHI_CACHE_REUSABLE_PARENT_PROCESS_DIR to the newly created directory
    // so that renderboot runs can access and reuse/update it.
    //
    internal::TempDirManager::TempDirManager() : mIsOwner(false)
    {
        static const std::string sCacheDirName { "/kodachi_cache" };
        const char* tmp_parent_proc_cache_dir =
                std::getenv("KODACHI_CACHE_REUSABLE_PARENT_PROCESS_DIR");

        if (tmp_parent_proc_cache_dir != nullptr) {
            mPath = tmp_parent_proc_cache_dir;
        }
        else {
            // First check if KODACHI_TEMP_CACHE is set,
            // if not, check if KATANA_TMPDIR is set,
            // if not, use the default location "/usr/render_tmp/"
            const char* tmp_dir = std::getenv("KODACHI_TEMP_CACHE");
            if (tmp_dir == nullptr || tmp_dir[0] == '\0') {
                // try to use Katana's temp directory
                // since it is always cleaned (even when Katana crashes).
                tmp_dir = std::getenv("KATANA_TMPDIR");
            }

            if (tmp_dir == nullptr || tmp_dir[0] == '\0') {
                mPath = "/usr/render_tmp/";
                ::setenv("KODACHI_TEMP_CACHE",
                         mPath.c_str(),
                         1 /* if variable is already set, overwrite the value */);
            }
            else {
                mPath = tmp_dir;

                // If multiple paths are set (separated by ':'), pick the first one.
                mPath = mPath.substr(0, mPath.find(':'));
            }

            // Following line is commented out to let processes share the same cache
            // directory.
            //
            //mPath += "/kodachi_cache_" + std::to_string(::getpid());
            mPath += sCacheDirName;
        }

        if (!cache_utils::fileOrDirExists(mPath)) {
            mIsOwner = cache_utils::recursiveMkdir(mPath);
            if (mIsOwner) {
                ::setenv("KODACHI_CACHE_REUSABLE_PARENT_PROCESS_DIR",
                         mPath.c_str(),
                         1 /* if variable is already set, overwrite the value */);
            }
        }
    }

    internal::TempDirManager::~TempDirManager()
    {
        if (mIsOwner) {
            cache_utils::removeDirectory(mPath);
        }
    }

    bool
    CacheRegistry::registerCache(internal::CacheBase::Ptr_t newCache)
    {
        if (newCache == nullptr) {
            return false;
        }

        const std::string& scope = newCache->getScope();
        auto iter = kTable.find(scope);
        if (iter != kTable.end()) {
            iter->second.insert(newCache);
        }
        else {
            kTable.insert(
                    {scope, std::unordered_set<internal::CacheBase::Ptr_t>{ newCache }});
        }

        return true;
    }

    void
    CacheRegistry::enableDiskCache(const std::string& scope)
    {
        if (scope.empty()) {
            for (auto& entry : kTable) {
                for (auto& cache : entry.second) {
                    cache->enableDiskCache();
                }
            }
        }
        else {
            auto iter = kTable.find(scope);
            if (iter != kTable.end()) {
                for (auto& cache : iter->second) {
                    cache->enableDiskCache();
                }
            }
        }
    }

    void CacheRegistry::disableDiskCache(const std::string& scope)
    {
        if (scope.empty()) {
            for (auto& entry : kTable) {
                for (auto& cache : entry.second) {
                    cache->disableDiskCache();
                }
            }
        }
        else {
            auto iter = kTable.find(scope);
            if (iter != kTable.end()) {
                for (auto& cache : iter->second) {
                    cache->disableDiskCache();
                }
            }
        }
    }

    void CacheRegistry::enableMemoryCache(const std::string& scope)
    {
        if (scope.empty()) {
            for (auto& entry : kTable) {
                for (auto& cache : entry.second) {
                    cache->enableMemoryCache();
                }
            }
        }
        else {
            auto iter = kTable.find(scope);
            if (iter != kTable.end()) {
                for (auto& cache : iter->second) {
                    cache->enableMemoryCache();
                }
            }
        }
    }

    void CacheRegistry::disableMemoryCache(const std::string& scope)
    {
        if (scope.empty()) {
            for (auto& entry : kTable) {
                for (auto& cache : entry.second) {
                    cache->disableMemoryCache();
                }
            }
        }
        else {
            auto iter = kTable.find(scope);
            if (iter != kTable.end()) {
                for (auto& cache : iter->second) {
                    cache->disableMemoryCache();
                }
            }
        }
    }

    void
    CacheRegistry::clear(Cache::ClearAction action, const std::string& scope)
    {
        // Empty string -> clear all caches.
        if (scope.empty()) {
            for (auto& sameScopeCaches : kTable) {
                for (auto& cache : sameScopeCaches.second) {
                    cache->clear(action);
                }
            }
        }
        else {
            auto cacheIter = kTable.find(scope);
            if (cacheIter != kTable.end()) {
                for (auto& cache : cacheIter->second) {
                    cache->clear(action);
                }
            }
        }
    }

    std::vector<std::string>
    CacheRegistry::getRegisteredScopes()
    {
        std::vector<std::string> scopeList;

        for (const auto& entry : kTable) {
            scopeList.push_back(entry.first);
        }

        return scopeList;
    }

    std::size_t
    CacheRegistry::getInMemoryEntryCount(const std::string& scope)
    {
        std::size_t count = 0;
        if (scope.empty()) {
            for (const auto& entry : kTable) {
                for (const auto& cache : entry.second) {
                    count += cache->getInMemoryEntryCount();
                }
            }
        }
        else {
            auto iter = kTable.find(scope);
            if (iter != kTable.end()) {
                for (const auto& cache : iter->second) {
                    count += cache->getInMemoryEntryCount();
                }
            }
        }

        return count;
    }

    std::size_t
    CacheRegistry::getInMemoryCacheSize(const std::string& scope)
    {
        std::size_t bytes = 0;
        if (scope.empty()) {
            for (const auto& entry : kTable) {
                for (const auto& cache : entry.second) {
                    bytes += cache->getCurrentSizeInMemory();
                }
            }
        }
        else {
            auto iter = kTable.find(scope);
            if (iter != kTable.end()) {
                for (const auto& cache : iter->second) {
                    bytes += cache->getCurrentSizeInMemory();
                }
            }
        }

        return bytes;
    }

    std::string
    CacheRegistry::getPathToScope(const std::string& scope)
    {
        auto iter = kTable.find(scope);
        if (iter == kTable.end()) {
            return { };
        }

        const auto cachePtr = *(iter->second.begin());
        return cachePtr->getCachePath();
    }

} // namespace kodachi


