// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once


#include <kodachi/logging/suite/KodachiLoggingSuite.h>
#include <kodachi/plugin_system/PluginManager.h>

#include <memory>

namespace kodachi {

class KodachiLoggingPlugin
{
public:
    KodachiLoggingPlugin();
    virtual ~KodachiLoggingPlugin();


    static void log(const char* message,
                    KdLoggingSeverity severity,
                    const char* module,
                    const char* file,
                    int line);

    static void* registerHandler(KdLogHandler handler,
                                 void* context,
                                 KdLoggingSeverity severityThreshold,
                                 const char* module);

    static int unregisterHandler(void* handlerToken);

    static int isSeverityEnabled(const char* module,
                                 KdLoggingSeverity severity);
    static int getSeverity();
    static void setSeverity(KdLoggingSeverity severity);

    static void flush();

    static KdPluginStatus setHost(KdPluginHost* host);
    static KdPluginHost* getHost();

    static KodachiLoggingSuite_v1 createSuite();

    static KdThreadLogPoolHandle createThreadLogPool(int bracket, const char* label);
    static void releaseThreadLogPool(KdThreadLogPoolHandle);

    static constexpr unsigned int _apiVersion = 1;
    static constexpr const char*  _apiName = "KodachiLoggingPlugin";

private:
    friend KodachiThreadLogPool;
    static KdPluginHost* sHost;

    struct HandlerData;
    static std::vector<std::unique_ptr<HandlerData>> mHandlers;

    static void logInternal(const char* message,
                            KdLoggingSeverity severity,
                            const char* module,
                            const char* file,
                            int line,
                            int indent = 0);
};

} // namespace kodachi

