// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/logging/KodachiLogging.h>

#include <kodachi/plugin_system/PluginManager.h>

#include <iostream>

namespace kodachi {

KodachiLogging::KodachiLogging(const std::string& module)
{
    mModule.append(module);

}

KodachiLogging::~KodachiLogging()
{
}

void
KodachiLogging::log(const std::string& message, KdLoggingSeverity severity) const
{
    const char* module = nullptr;
    if (!mModule.empty()) {
        module = mModule.c_str();
    }

    if (sLoggingSuite) {
        sLoggingSuite->log(message.c_str(), severity, module, NULL, -1);
    }
}

bool
KodachiLogging::isSeverityEnabled(KdLoggingSeverity severity) const
{
    if (sLoggingSuite) {
        const char* module = nullptr;
        if (!mModule.empty()) {
            module = mModule.c_str();
        }

        return sLoggingSuite->isSeverityEnabled(module, severity);
    }

    return false;
}

void
KodachiLogging::setSeverity(KdLoggingSeverity severity)
{
    if (sLoggingSuite) {
        sLoggingSuite->setSeverity(severity);
    }
}

int
KodachiLogging::getSeverity()
{
    if (sLoggingSuite) {
        return sLoggingSuite->getSeverity();
    }

    return 0;
}

KdPluginStatus
KodachiLogging::setHost(KdPluginHost* host)
{
    return kodachi::PluginManager::setHost(host);
}

const KodachiLoggingSuite_v1*
KodachiLogging::getSuite()
{
    return sLoggingSuite.get();
}

void*
KodachiLogging::registerHandler(KdLogHandler handler,
                                void* context,
                                KdLoggingSeverity severityThreshold,
                                const char* module)
{
    if (sLoggingSuite) {
        return sLoggingSuite->registerHandler(handler, context,
                                              severityThreshold, module);
    }

    return nullptr;
}

LazyPluginSuite<KodachiLoggingSuite_v1> KodachiLogging::sLoggingSuite{"KodachiLogging", "KodachiLoggingPlugin", 1};

} // namespace kodachi


