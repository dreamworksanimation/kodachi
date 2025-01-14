// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <FnViewer/plugin/FnViewportLayer.h>
#include <FnViewer/plugin/FnEventWrapper.h>

class LightDelegateComponent;

class LightLayer : public FnKat::ViewerAPI::ViewportLayer
{
public:
    LightLayer() { }
    static ViewportLayer* create() { return new LightLayer(); }

    void setup() override;

    ~LightLayer();

    void draw() override;
    void pickerDraw(unsigned int x, unsigned int y,
                    unsigned int w, unsigned int h,
                    const FnKat::ViewerAPI::PickedAttrsMap& ignoreAttrs) override;

    void setOption(FnKat::ViewerAPI::OptionIdGenerator::value_type,
                   FnAttribute::Attribute) override;

    /// Freezes the layer state when not visible.
    void freeze() override {}
    /// Thaws the layer state when made visible.
    void thaw() override {}
    void resize(unsigned w, unsigned h) override {}
    void cleanup() override {}

private:
    void genericDraw(const FnKat::ViewerAPI::PickedAttrsMap* const ignoreAttrs = nullptr);
    bool mAllLightCones = false;
    LightDelegateComponent* mLightDelegateComponent = nullptr;
};

