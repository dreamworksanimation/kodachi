// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <internal/FnGeolib/op/FnGeolibOp.h>
#include <internal/FnGeolib/op/FnOpDescriptionBuilder.h>

namespace kodachi {

using namespace FnGeolibOp;

class Op : public GeolibOp
{
public:
    static FnPlugStatus setHost(FnPluginHost *host);
};

using OpSetupInterface = GeolibSetupInterface;
using OpCookInterface = GeolibCookInterface;

using namespace FnOpDescription;
using OpDescriptionBuilder = FnOpDescriptionBuilder;

enum class ErrorSeverity
{
    CRITICAL,
    NONCRITICAL
};

/**
 * Sets the 'errorMessage' attribute on the location.
 * Specify CRITICAL ErrorSeverity to change the type of the location to 'error',
 */
void ReportError(OpCookInterface& interface,
                 const std::string& message,
                 ErrorSeverity severity = ErrorSeverity::CRITICAL);

/**
 * Convenience function for calling ReportError with a NONCRITICAL ErrorSeverity
 */
void ReportNonCriticalError(OpCookInterface& interface,
                            const std::string& message);

} // namespace kodachi

#define DEFINE_KODACHIOP_PLUGIN DEFINE_GEOLIBOP_PLUGIN

