// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "../Drawables/LightFilterDrawable.h"
#include <common/math/ColorSpace.h>

namespace MoonrayKatana {

class DecayLightFilterDrawable : public LightFilterDrawable
{
public:
    DecayLightFilterDrawable(LightDrawable* parent, const std::string& location = std::string())
        : LightFilterDrawable(parent, location) {}

    virtual void setup(const FnAttribute::GroupAttribute& root) override;
    virtual void draw() override;

    static const float sColors[4][3];

protected:
    void drawPointFilter(float radius, const float (&scale)[3]) const;
    void drawSphereFilter(float radius, const float (&scale)[3]) const;
    void drawCylinderFilter(float radius, const float (&scale)[3]) const;
    void drawSpotFilter(float radius, const float (&scale)[3]) const;
    void drawRectFilter(float radius, const float (&scale)[3]) const;
    void drawDiskFilter(float radius, const float (&scale)[3]) const;
    bool falloff_near = false;
    bool falloff_far = false;
    float mRadius[4] = { 0.0f };
};

}

