// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <internal/FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>
#include <kodachi/attribute/Attribute.h>

namespace kodachi {

using CookInterfaceUtils = FnGeolibServices::FnGeolibCookInterfaceUtils;

// Thread safe function that puts a global mutex around calls to
// material cookDaps.
kodachi::GroupAttribute ThreadSafeCookDaps(const kodachi::GeolibCookInterface& interface,
                                           const std::string& attrRoot,
                                           const std::string& inputLocationPath = "",
                                           int inputIndex = -1,
                                           const kodachi::Attribute& cookOrderAttr = kodachi::Attribute{});

} // namespace kodachi

