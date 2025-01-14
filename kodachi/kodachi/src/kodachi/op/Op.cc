// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/op/Op.h>
#include <kodachi/logging/KodachiLogging.h>

namespace kodachi {

FnPlugStatus
Op::setHost(FnPluginHost *host)
{
    FnPlugStatus ret = GeolibOp::setHost(host);
    if (ret == FnPluginStatusOK) {
        ret = KodachiLogging::setHost(host);
    }

    return ret;
}

void
ReportError(OpCookInterface& interface,
            const std::string& message,
            ErrorSeverity severity)
{
    if (severity == ErrorSeverity::CRITICAL) {
        FnGeolibOp::ReportError(interface, message);
    } else {
        interface.setAttr("errorMessage", kodachi::StringAttribute(message));
    }
}

void ReportNonCriticalError(OpCookInterface& interface,
                            const std::string& message)
{
    ReportError(interface, message, ErrorSeverity::NONCRITICAL);
}

} // namespace kodachi

