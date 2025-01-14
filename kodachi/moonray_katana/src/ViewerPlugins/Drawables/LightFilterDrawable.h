// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Drawable.h"
#include <FnAttribute/FnAttribute.h>

namespace MoonrayKatana {

class LightDrawable;

// Reusable object for new Viewer api
class LightFilterDrawable : public Drawable
{
public:
    LightFilterDrawable(LightDrawable* parent, const std::string& location = std::string())
        : Drawable(location), mParent(parent) {}

    static LightFilterDrawable* create(LightDrawable* parent,
                                       const std::string& location,
                                       const FnAttribute::GroupAttribute& filterAttr = FnAttribute::GroupAttribute());
    virtual void setup(const FnAttribute::GroupAttribute& root);

    LightDrawable* mParent;
};

}

