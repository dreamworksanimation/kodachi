// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/cache/CacheRegistry.h>

#include "Helpers.h"

namespace pykodachi
{
    void
    registerKodachiCacheRegistry(pybind11::module& module)
    {
        pybind11::module submod_CacheRegistry = module.def_submodule("CacheRegistry");

        pybind11::enum_<kodachi::Cache::ClearAction>(submod_CacheRegistry, "ClearAction")
                .value("MEMORY",
                       kodachi::Cache::ClearAction::MEMORY,
                       "\n"
                       "\t Only clear cache entries stored in main memory (RAM).")

                .value("DISK_CONTENTS",
                       kodachi::Cache::ClearAction::DISK_CONTENTS,
                       "\n"
                       "\t Remove all on-disk cache entries (files) *without* removing cache top level and scope directories. \n"
                       "\t For instance, assuming entries of \"ScatterPointsOp\" cache are stored here: \n"
                       "\t      /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp \n"
                       "\t Clearing DISK_CONTENTS is equivalent to following bash command: \n"
                       "\t      % rm /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp/*")

                .value("DISK_SCOPE_DIR",
                       kodachi::Cache::ClearAction::DISK_SCOPE_DIR,
                       "\n"
                       "\t Recursively remove the scope directory (and its contents) from disk. \n"
                       "\t For instance, assuming entries of \"ScatterPointsOp\" cache are stored here: \n"
                       "\t      /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp \n"
                       "\t Clearing DISK_SCOPE_DIR is equivalent to following bash command: \n"
                       "\t      % rm -rf /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp")

                .value("DISK_TOP_DIR",
                       kodachi::Cache::ClearAction::DISK_TOP_DIR,
                       "\n"
                       "\t Recursively remove the top-level cache directory (and its contents) from disk. \n"
                       "\t For instance, assuming entries of \"ScatterPointsOp\" cache are stored here: \n"
                       "\t      /usr/pic1/some_dir/kodachi_cache/2013815268070794411/ScatterPointsOp \n"
                       "\t Clearing DISK_TOP_DIR is equivalent to following bash command: \n"
                       "\t      % rm -rf /usr/pic1/some_dir/kodachi_cache");

        submod_CacheRegistry
                .def("enableDiskCache", &kodachi::CacheRegistry::enableDiskCache, pybind11::arg("scope") = std::string())

                .def("disableDiskCache", &kodachi::CacheRegistry::disableDiskCache, pybind11::arg("scope") = std::string())

                .def("enableMemoryCache", &kodachi::CacheRegistry::enableMemoryCache, pybind11::arg("scope") = std::string())

                .def("disableMemoryCache", &kodachi::CacheRegistry::disableMemoryCache, pybind11::arg("scope") = std::string())

                .def("count", &kodachi::CacheRegistry::count)

                .def("getRegisteredScopes", &kodachi::CacheRegistry::getRegisteredScopes)

                .def("getInMemoryCacheSize", &kodachi::CacheRegistry::getInMemoryCacheSize, pybind11::arg("scope") = std::string())

                .def("getInMemoryEntryCount", &kodachi::CacheRegistry::getInMemoryEntryCount, pybind11::arg("scope") = std::string())

                .def("getPathToScope", &kodachi::CacheRegistry::getPathToScope, pybind11::arg("scope"))

                .def("clear",
                     &kodachi::CacheRegistry::clear,
                     pybind11::arg("action"),
                     pybind11::arg("scope") = std::string());
    }

} // namespace pykodachi

