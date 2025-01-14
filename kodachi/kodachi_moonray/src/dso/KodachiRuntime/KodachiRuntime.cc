// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "kodachi_moonray/kodachi_runtime_wrapper/KodachiRuntimeWrapper.h"

#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>

#include "attributes.cc"

using namespace arras;

RDL2_DSO_CLASS_BEGIN(KodachiRuntime, kodachi_moonray::KodachiRuntimeWrapper)

public:
RDL2_DSO_DEFAULT_CTOR(KodachiRuntime)

RDL2_DSO_CLASS_END(KodachiRuntime)

