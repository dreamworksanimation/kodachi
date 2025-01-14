// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi_moonray/moonray_util/MoonrayUtil.h>

#include <atomic>

namespace {
std::atomic<bool> sGlobalDriverInit(false);
}

namespace kodachi_moonray {
namespace moonray_util {

bool
initGlobalRenderDriver(const arras::rndr::RenderOptions& renderOptions)
{
    if (!sGlobalDriverInit.exchange(true)) {
        arras::rndr::initGlobalDriver(renderOptions);
        return true;
    }

    return false;
}

} // namespace moonray_util
} // namespace kodachi_moonray

