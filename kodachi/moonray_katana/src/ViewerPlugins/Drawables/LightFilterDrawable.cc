// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightFilterDrawable.h"
#include "LightDrawable.h"
#include "BarnDoorsLightFilterDrawable.h"
#include "DecayLightFilterDrawable.h"
#include <GL/gl.h>
#include <FnAPI/FnAPI.h>

#include <kodachi_moonray/light_util/LightUtil.h>

namespace MoonrayKatana
{
using namespace FnAttribute;

LightFilterDrawable*
LightFilterDrawable::create(LightDrawable* parent,
                            const std::string& location,
                            const FnAttribute::GroupAttribute& filterAttr)
{
    LightFilterDrawable* filter = nullptr;

    const GroupAttribute material = filterAttr.getChildByName("material");
    const std::string filterType = kodachi_moonray::light_util::getShaderName(
            material, "moonrayLightfilter");

    if (filterType == "DecayLightFilter") {
        filter = new DecayLightFilterDrawable(parent, location);
    } else if (filterType == "BarnDoorsLightFilter") {
        filter = new BarnDoorsLightFilterDrawable(parent, location);
    }

    if (filter) {
        filter->setup(filterAttr);
    }

    return filter;
}

void
LightFilterDrawable::setup(const FnAttribute::GroupAttribute& root)
{
    Drawable::setup(root);

    auto muteAttr = StringAttribute(root.getChildByName("info.light.muteState"));
    mMuted = muteAttr.isValid() && muteAttr != "muteEmpty";
}

}

