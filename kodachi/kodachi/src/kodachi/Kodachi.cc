// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "Kodachi.h"

// kodachi
#include "internal/internal_utils.h"
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/OpTreeBuilder.h>
#include <kodachi/KodachiRuntime.h>

// tbb
#include <tbb/task_scheduler_init.h>

// std
#include <iostream>

// System
#include <dlfcn.h>

#define TBB_PREVIEW_GLOBAL_CONTROL 1
#include <tbb/global_control.h>

namespace {
int sNumThreads = tbb::task_scheduler_init::automatic;
std::unique_ptr<tbb::global_control> sGC;

// Function to validate and sanitize the path
bool 
validateAndSanitizePath(const std::string& path) {
    // Normalize the path by removing trailing slashes
    std::filesystem::path normalizedPath = std::filesystem::path(path).lexically_normal();

    // Check if the path exists
    if (!std::filesystem::exists(normalizedPath)) {
        std::cerr << "Error: Path does not exist: " << path << "\n";
        return false;
    }

    // Check if the path is a directory
    if (!std::filesystem::is_directory(normalizedPath)) {
        std::cerr << "Error: Path is not a directory: " << path << "\n";
        return false;
    }

    // Check for invalid characters or sequences
    std::regex invalidPattern(R"([<>:\"|?*])");
    if (std::regex_search(normalizedPath.string(), invalidPattern)) {
        std::cerr << "Error: Path contains invalid characters: " << normalizedPath << "\n";
        return false;
    }

    // Check for path traversal sequences
    if (normalizedPath.string().find("..") != std::string::npos) {
        std::cerr << "Error: Path contains invalid sequences: " << normalizedPath << "\n";
        return false;
    }

    // Check for symbolic links
    if (std::filesystem::is_symlink(normalizedPath)) {
        std::cerr << "Error: Path contains symbolic links: " << normalizedPath << "\n";
        return false;
    }

    return true;
}

} // anonymous namespace

namespace kodachi {
bool
bootstrap(const std::string& kodachiRoot)
{
    std::string kodachiPath;
    if (kodachiRoot.empty()) {
        const char* katanaRootEnv = ::getenv("KODACHI_ROOT");
        if (!katanaRootEnv) {
            std::cerr << "KodachiRuntime::bootstrap - "
                      << "KODACHI_ROOT environment variable not set, "
                      << "and kodachiRoot was not provided\n";
            return false;
        }
        kodachiPath = katanaRootEnv;
    } else {
        kodachiPath = kodachiRoot;
    }

    // Validate and sanitize the kodachiPath
    if (!validateAndSanitizePath(kodachiPath)) {
        std::cerr << "Invalid path " << kodachiPath << "\n";
        return false;
    }

    // The kodachi::MonitoringTraversal currently requires geolib runtimes
    // to be running in SYNC mode. In the case that kodachi is being run inside
    // of renderboot, this variable is already set. Not setting this in the
    // package.yaml because it causes the Katana UI to lock up when opening
    // the scenegraph.
    ::setenv("RUNTIME_USE_SYNC", "1", true);

    const std::string kodachiPathAbsolute = internal::absolutePath(kodachiPath);

    using FnGeolibSessionStatus = int;
    constexpr int kFnGeolibSessionOK = 0;
    constexpr int kFnGeolibSessionLoadError = 1;
    constexpr int kFnGeolibSessionConfigurationError = 2;
    constexpr int kFnGeolibSessionLicensingError = 3;

    // Type definitions for libFnGeolib3.
    using FnGeolib3InitializeFn = FnGeolibSessionStatus(void* reserved);
    using FnGeolib3GetPluginManagerFn = struct FnPluginManagerHostSuite_v1*();

    void* dso = ::dlopen((kodachiPathAbsolute / "bin/libFnGeolib3.so").c_str(),
                         RTLD_LOCAL | RTLD_LAZY);

    if (!dso)
    {
        std::cerr << "KodachiRuntime::bootstrap - Could not open libFnGeolib3.so\n";
        return false;
    }

    auto* FnGeolib3Initialize = reinterpret_cast<FnGeolib3InitializeFn*>(
                                          ::dlsym(dso, "FnGeolib3Initialize"));;
    if (!FnGeolib3Initialize) {
        std::cerr << "KodachiRuntime::bootstrap - Could not find symbol FnGeolib3Initialize\n";
        ::dlclose(dso);
        return false;
    }

    FnGeolibSessionStatus status = FnGeolib3Initialize(nullptr);
    if (status != kFnGeolibSessionOK)
    {
        const char* errorType = nullptr;
        switch (status) {
        case kFnGeolibSessionLoadError: errorType = "Load"; break;
        case kFnGeolibSessionConfigurationError: errorType = "Configuration"; break;
        case kFnGeolibSessionLicensingError: errorType = "Licensing"; break;
        }

        std::cerr << "Error of type '" << errorType << "' while bootstrapping runtime\n";
        ::dlclose(dso);
        return false;
    }

    auto* FnGeolib3GetPluginManager = reinterpret_cast<FnGeolib3GetPluginManagerFn*>(
                      ::dlsym(dso, "FnGeolib3GetPluginManager"));
    if (!FnGeolib3GetPluginManager) {
        std::cerr << "KodachiRuntime::bootstrap - Could not find symbol FnGeolib3GetPluginManager\n";
        ::dlclose(dso);
        return false;
    }

    FnPluginHost* host = FnGeolib3GetPluginManager()->getHost();

    if (!host) {
        std::cerr << "Failed to get PluginManager host";
        ::dlclose(dso);
        return false;
    }

    if (kodachi::PluginManager::setHost(host) == FnPluginStatusError) {
        std::cerr << "KodachiRuntime::bootstrap - error getting plugin manager host\n";
        ::dlclose(dso);
        return false;
    }

    std::vector<std::string> searchPath;
    searchPath.emplace_back(kodachiPathAbsolute / "core_plugins/Libs");
    searchPath.emplace_back(kodachiPathAbsolute / "core_plugins/Ops");

    const char* katanaResourcesEnv = ::getenv("KATANA_RESOURCES");
    if (katanaResourcesEnv) {
        const std::string katanaResources(katanaResourcesEnv);

        // Validate and sanitize the path
        if (!validateAndSanitizePath(katanaResources)) {
            std::cerr << "KATANA_RESOURCES path set but it is invalid: " << katanaResources << "\n";
        } else {
            std::vector<std::string> resourceDirs =
                    internal::splitString(katanaResources, ':');

            for (const auto& resourceDir : resourceDirs) {
                const std::string opsDir = resourceDir / "Ops";
                if (internal::fileOrDirExists(opsDir)) {
                    searchPath.push_back(opsDir);
                }

                const std::string libsDir = resourceDir / "Libs";
                if (internal::fileOrDirExists(libsDir)) {
                    searchPath.push_back(libsDir);
                }
            }
        }
    }

    kodachi::PluginManager::addSearchPath(searchPath);

    // Discover plug-ins.
    kodachi::PluginManager::findPlugins();

    return setHost(host) == KdPluginStatus::FnPluginStatusOK;
}

KdPluginHost*
getHost()
{
    return kodachi::PluginManager::getHost();
}

KdPluginStatus
setHost(KdPluginHost* host)
{
    auto status = kodachi::Attribute::setHost(host);
    if (status == KdPluginStatus::FnPluginStatusOK) {
        status = kodachi::GroupBuilder::setHost(host);
    }
    if (status == KdPluginStatus::FnPluginStatusOK) {
        status = kodachi::KodachiRuntime::setHost(host);
    }
    if (status == KdPluginStatus::FnPluginStatusOK) {
        status = kodachi::OpTreeBuilder::setHost(host);
    }

    return status;
}

void
setNumberOfThreads(int numThreads)
{
    if (numThreads == 0) {
        sNumThreads = tbb::task_scheduler_init::automatic;
        sGC.reset();
        return;
    }

    sNumThreads = numThreads;
    sGC.reset(new tbb::global_control(tbb::global_control::max_allowed_parallelism, sNumThreads));
}

int
getNumberOfThreads()
{
    if (sNumThreads == tbb::task_scheduler_init::automatic) {
        return tbb::task_scheduler_init::default_num_threads();
    }

    return sNumThreads;
}

} // namespace kodachi

