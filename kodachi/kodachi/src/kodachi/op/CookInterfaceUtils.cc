// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/op/CookInterfaceUtils.h>

#include <mutex>

namespace kodachi {

std::mutex sCookDapsMutex;

// Thread safe function that puts a global mutex around calls to
// material cookDaps.
kodachi::GroupAttribute
ThreadSafeCookDaps(const kodachi::GeolibCookInterface& interface,
                   const std::string& attrRoot,
                   const std::string& inputLocationPath,
                   int inputIndex,
                   const kodachi::Attribute& cookOrderAttr)
{
    std::lock_guard<std::mutex> g(sCookDapsMutex);
    kodachi::GroupAttribute cookedDaps =
            CookInterfaceUtils::cookDaps(interface, attrRoot, inputLocationPath, inputIndex, cookOrderAttr);

    return cookedDaps;
}

} // namespace kodachi

