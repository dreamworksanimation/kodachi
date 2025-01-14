// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/plugin_system/PluginManager.h>

#include <string>

namespace kodachi {

/**
 * Initializes the Geolib3 runtime and the plugin manager. Loads all ops
 * and plugins found in the KATANA_RESOURCES path
 */
bool bootstrap(const std::string& kodachiRoot=std::string{});

/**
 * Get the PluginHost to call setHost on all plugin clients being used in a SO.
 * For ops, attribute functions, etc. that are registered with the plugin system
 * the host is already passed to their setHost() function.
 */
KdPluginHost* getHost();

KdPluginStatus setHost(KdPluginHost* host);

/**
 * Set the number of threads that TBB can use.
 */
void setNumberOfThreads(int numThreads);
int getNumberOfThreads();

} // namespace kodachi

