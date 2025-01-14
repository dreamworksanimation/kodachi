// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <memory>
#include <FnViewerModifier/plugin/FnViewerModifier.h>
#include <FnViewerModifier/plugin/FnViewerModifierInput.h>
#include <FnAttribute/FnGroupBuilder.h>

#include "Drawables/LightDrawable.h"
#include "Drawables/LightFilterDrawable.h"

/**
 * The LightViewerModifier controls how lights objects are displayed within the
 * viewer. Due to the inability to register multiple viewer modifiers for the same
 * location type ("light" locations in this case), this class must be able to draw
 * all types of light that may be encountered. This version can only draw lights with
 * a MoonrayLightShader, all other types are drawn like a point light.
 */
class LightViewerModifier : public FnKat::ViewerModifier
{
public:
    std::unique_ptr<MoonrayKatana::LightDrawable> mDrawable;

    LightViewerModifier(FnAttribute::GroupAttribute args) : FnKat::ViewerModifier(args) { }
    virtual ~LightViewerModifier();

    static FnKat::ViewerModifier* create(FnAttribute::GroupAttribute args)
    { return new LightViewerModifier(args); }

    static FnAttribute::GroupAttribute getArgumentTemplate()
    { return FnAttribute::GroupAttribute(true); }

    // This is the type of SceneGraph location this viewer modifier runs on.
    static const char* getLocationType() { return "light"; }

    /**
     * Called per instance before each draw
     */
    virtual void deepSetup(FnKat::ViewerModifierInput& input) override {}

    /**
     * Called once per VMP instance when constructed
     */
    virtual void setup(FnKat::ViewerModifierInput& input) override;

    // Draw the light, also used during hit detection. During hit detection
    // you should not adjust the assigned color.
    virtual void draw(FnKat::ViewerModifierInput& input) override;

    /// Called when the location is removed/refreshed.
    virtual void cleanup(FnKat::ViewerModifierInput& input) override { }

    /// Called per instance after each draw
    virtual void deepCleanup(FnKat::ViewerModifierInput& input) override { }

    /**
     * Returns a bounding box for the current location for use with the viewer
     * scene graph. Unfortunatly used for both "frame" and for culling, so we
     * have to include all the ray lines of the spotlight cone. And it seems
     * to be called with selected set randomly, so always return full box.
     */
    virtual FnAttribute::DoubleAttribute getLocalSpaceBoundingBox(FnKat::ViewerModifierInput& input) override;

    static void flush() {}
    static void onFrameBegin() {}
    static void onFrameEnd() {}

private:
    std::string mLocation;
};

class LightFilterViewerModifier : public FnKat::ViewerModifier
{
public:
    std::unique_ptr<MoonrayKatana::LightFilterDrawable> mDrawable;

    LightFilterViewerModifier(FnAttribute::GroupAttribute args) : FnKat::ViewerModifier(args) { }

    static FnKat::ViewerModifier* create(FnAttribute::GroupAttribute args)
    { return new LightFilterViewerModifier(args); }

    static FnAttribute::GroupAttribute getArgumentTemplate()
    { return FnAttribute::GroupAttribute(true); }

    // This is the type of SceneGraph location this viewer modifier runs on.
    static const char* getLocationType() { return "light filter"; }

    /**
     * Called per instance before each draw
     */
    virtual void deepSetup(FnKat::ViewerModifierInput& input) override {}

    /**
     * Called once per VMP instance when constructed
     */
    virtual void setup(FnKat::ViewerModifierInput& input) override;

    // Draw the light, also used during hit detection. During hit detection
    // you should not adjust the assigned color.
    virtual void draw(FnKat::ViewerModifierInput& input) override;

    /// Called when the location is removed/refreshed.
    virtual void cleanup(FnKat::ViewerModifierInput& input) override { }

    /// Called per instance after each draw
    virtual void deepCleanup(FnKat::ViewerModifierInput& input) override { }

    /**
     * Returns a bounding box for the current location for use with the viewer
     * scene graph. Unfortunatly used for both "frame" and for culling, so we
     * have to include all the ray lines of the spotlight cone. And it seems
     * to be called with selected set randomly, so always return full box.
     */
    virtual FnAttribute::DoubleAttribute getLocalSpaceBoundingBox(FnKat::ViewerModifierInput& input) override;

    static void flush() {}
    static void onFrameBegin() {}
    static void onFrameEnd() {}
};

