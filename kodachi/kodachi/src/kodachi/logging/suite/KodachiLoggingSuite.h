// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <stdint.h>

extern "C" {

class KodachiThreadLogPool;
using KdThreadLogPoolHandle = KodachiThreadLogPool*;

/** @brief Defines for message severity
*/
enum
{
    kKdLoggingSeverityDebug,
    kKdLoggingSeverityInfo,
    kKdLoggingSeverityWarning,
    kKdLoggingSeverityError,
    kKdLoggingSeverityFatal,
};
typedef int KdLoggingSeverity;

#define KodachiLoggingSuite_version 1

typedef void (*KdLogHandler)(const char* message,
                             KdLoggingSeverity severity,
                             const char* module,
                             const char* file,
                             int line,
                             int indentDepth,
                             void* userdata);

struct KodachiLoggingSuite_v1
{
    KdThreadLogPoolHandle(*createThreadLogPool)(int bracket, const char* label);
    void (*releaseThreadLogPool)(KdThreadLogPoolHandle handle);

    // log a message
    void (*log)(const char* message,
                KdLoggingSeverity severity,
                const char* module,
                const char* file,
                int line);

    // Returns an opaque token that can be passed to unregisterHandler, or NULL
    // on error.
    void* (*registerHandler)(KdLogHandler handler,
                             void* context,
                             KdLoggingSeverity severityThreshold,
                             const char* module);
    // Returns true iff a handler was unregistered.
    int (*unregisterHandler)(void* handlerToken);

    int (*isSeverityEnabled)(const char* module,
                             KdLoggingSeverity severity);
    int (*getSeverity)();
    void (*setSeverity)(KdLoggingSeverity severity);
};


}

