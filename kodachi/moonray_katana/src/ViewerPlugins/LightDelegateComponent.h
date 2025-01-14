// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <FnViewer/plugin/FnViewerDelegateComponent.h>
#include <map>

#include "Drawables/Drawable.h"

class LightDelegateComponent : public FnKat::ViewerAPI::ViewerDelegateComponent
{
public:
    static const char NAME[];
    
    void setup() override {}
    void cleanup() override {}

    static ViewerDelegateComponent* create() { return new LightDelegateComponent(); }

    bool locationEvent(const FnKat::ViewerAPI::ViewerLocationEvent& event, bool handled) override;
    void locationsSelected(const std::vector<std::string>& locations) override;
    FnAttribute::DoubleAttribute getBounds(const std::string& location) override;
    FnAttribute::DoubleAttribute computeExtent(const std::string& location) override;

    static void flush() {}

    std::map<std::string, std::unique_ptr<MoonrayKatana::Drawable>> mDrawables;
private:
    LightDelegateComponent() {}

    void dirtyAllViewports();
};

