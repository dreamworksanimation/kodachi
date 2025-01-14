// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnViewerModifier/plugin/FnViewerModifier.h>
#include <FnViewerModifier/plugin/FnViewerModifierInput.h>
#include <FnAttribute/FnGroupBuilder.h>
#include <FnAttribute/FnAttribute.h>

#include "Drawables/CameraDrawable.h"

class CameraViewerModifier : public FnKat::ViewerModifier
{
public:
    MoonrayKatana::CameraDrawable mCamDrawable;

    CameraViewerModifier(FnKat::GroupAttribute args)
        : FnKat::ViewerModifier(args) {}

    static FnKat::ViewerModifier* create(FnKat::GroupAttribute args)
    { return new CameraViewerModifier(args); }

    static FnKat::GroupAttribute getArgumentTemplate()
    { return FnAttribute::GroupAttribute(true); }

    static const char* getLocationType() { return "camera"; }

    void deepSetup(FnKat::ViewerModifierInput& input) override;
    void setup(FnKat::ViewerModifierInput& input) override;
    void draw(FnKat::ViewerModifierInput& input) override;
    FnKat::DoubleAttribute getLocalSpaceBoundingBox(FnKat::ViewerModifierInput& input) override;

    void cleanup(FnKat::ViewerModifierInput& input) override { }

    void deepCleanup(FnKat::ViewerModifierInput& input) override { }

    static void flush() {}

    static void onFrameEnd() {}

    static void onFrameBegin() {}
};

