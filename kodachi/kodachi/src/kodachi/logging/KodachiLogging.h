// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/logging/suite/KodachiLoggingSuite.h>
#include <kodachi/plugin_system/PluginManager.h>

#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <iostream>
#include <mutex>

namespace kodachi {

template <typename SuiteT>
class LazyPluginSuite
{
public:
    LazyPluginSuite(const char* pluginName,
                    const char* apiName,
                    unsigned int apiVersion)
    : pluginName_(pluginName)
    , apiName_(apiName)
    , apiVersion_(apiVersion)
    { }

    const SuiteT* get()
    {
        if (!suite_) {
            KdPluginHandle handle =
                        kodachi::PluginManager::getPlugin(pluginName_,
                                                          apiName_, apiVersion_);
            if (!handle) {
                std::cerr << "Unable to get plugin '" << pluginName_ << "'\n";
                return nullptr;
            }

            const SuiteT* suite = static_cast<const SuiteT*>(
                    kodachi::PluginManager::getPluginSuite(handle));

            if (!suite) {
                std::cerr << "Unable to get PluginSuite for plugin '" << pluginName_ << "\n";
                return nullptr;
            }

            std::call_once(onceFlag_, [&]() { suite_ = suite; });
        }

        return suite_;
    }

    const SuiteT* operator->()
    {
        return get();
    }

    operator bool() { return get() != nullptr; }

    const char* pluginName_;
    const char* apiName_;
    unsigned int apiVersion_;
    const SuiteT* suite_ = nullptr;
    std::once_flag onceFlag_;
};

class KodachiLogging
{
public:
    explicit KodachiLogging(const std::string& module = "");
    ~KodachiLogging();

    void log(const std::string& message, KdLoggingSeverity severity) const;

    void debug(const std::string& message) const
    {
        log(message, kKdLoggingSeverityDebug);
    }

    void info(const std::string& message) const
    {
        log(message, kKdLoggingSeverityInfo);
    }

    void warning(const std::string& message) const
    {
        log(message, kKdLoggingSeverityWarning);
    }

    void error(const std::string& message) const
    {
        log(message, kKdLoggingSeverityError);
    }

    void critical(const std::string& message) const
    {
        log(message, kKdLoggingSeverityFatal);
    }

    bool isSeverityEnabled(KdLoggingSeverity severity) const;
    static int getSeverity();
    static void setSeverity(KdLoggingSeverity severity);

    static KdPluginStatus setHost(KdPluginHost* host);
    static const KodachiLoggingSuite_v1* getSuite();

    static void* registerHandler(KdLogHandler handler,
                                 void* context,
                                 KdLoggingSeverity severityThreshold,
                                 const char* module);

    // ThreadLogPool
    using HandleUniquePtr = std::unique_ptr<std::remove_pointer<KdThreadLogPoolHandle>::type,
                                            std::function<void(KdThreadLogPoolHandle)>>;

    class ThreadLogPool
    {
    public:
        ThreadLogPool(int bracket, const std::string& label)
        {
            if (sLoggingSuite) {
                mHandle = HandleUniquePtr(
                        sLoggingSuite->createThreadLogPool(bracket, label.c_str()),
                        [&](KdThreadLogPoolHandle h) { sLoggingSuite->releaseThreadLogPool(h); });
            }
        }

        HandleUniquePtr mHandle;
    };


private:
    // no copy/assign
    KodachiLogging(const KodachiLogging& rhs);
    KodachiLogging& operator=(const KodachiLogging& rhs);

    std::string mModule;

    static LazyPluginSuite<KodachiLoggingSuite_v1> sLoggingSuite;
};

} // namespace kodachi


#define KdLogSetup(name) static kodachi::KodachiLogging sKdLoggingClient(name);

#define KdLogInternal(logEvent, severity)                       \
    do                                                          \
    {                                                           \
        if (sKdLoggingClient.isSeverityEnabled(severity)) {     \
            std::ostringstream _log_buf;                        \
            _log_buf << logEvent;                               \
            sKdLoggingClient.log(_log_buf.str(), severity);     \
        }                                                       \
    } while (0);

// and now, wrappers for all the levels
#define KdLogFatal(logEvent) KdLogInternal(logEvent, kKdLoggingSeverityFatal)
#define KdLogError(logEvent) KdLogInternal(logEvent, kKdLoggingSeverityError)
#define KdLogWarn(logEvent) KdLogInternal(logEvent, kKdLoggingSeverityWarning)
#define KdLogInfo(logEvent) KdLogInternal(logEvent, kKdLoggingSeverityInfo)
#define KdLogDebug(logEvent) KdLogInternal(logEvent, kKdLoggingSeverityDebug)


