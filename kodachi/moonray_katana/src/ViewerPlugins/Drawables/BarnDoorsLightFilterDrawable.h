// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "LightFilterDrawable.h"
#include "VAO.h"
#include <common/math/ColorSpace.h>

namespace MoonrayKatana {

class BarnDoorsLightFilterDrawable : public LightFilterDrawable
{
public:
    BarnDoorsLightFilterDrawable(LightDrawable* parent, const std::string& location = std::string())
        : LightFilterDrawable(parent, location) {}

    virtual void setup(const FnAttribute::GroupAttribute& root) override;
    virtual void draw() override;
    virtual void ancestorChanged(Drawable* drawable) override;

protected:
    VAO vao;
    bool update = true;
};

}

