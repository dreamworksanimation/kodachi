// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayRendererInfo.h"

namespace // anonymous
{
DEFINE_RENDERERINFO_PLUGIN(MoonrayRendererInfo)
} // namespace anonymous

void registerPlugins()
{
  REGISTER_PLUGIN(MoonrayRendererInfo, "MoonrayRendererInfo", 0, 1);
}
