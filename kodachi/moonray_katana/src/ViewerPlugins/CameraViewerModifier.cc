// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "CameraViewerModifier.h"

#include <GL/gl.h>
#include <GL/glu.h>

void
CameraViewerModifier::deepSetup(FnKat::ViewerModifierInput& input)
{
    input.overrideHostGeometry();
}

void
CameraViewerModifier::setup(FnKat::ViewerModifierInput& input)
{
    const FnAttribute::GroupAttribute geomAttrs = input.getAttribute("");
    mCamDrawable.setup(geomAttrs);
}

void
CameraViewerModifier::draw(FnKat::ViewerModifierInput& input)
{
    // Don't draw the camera representation if we're being looked through
    if (input.isLookedThrough()) {
        return;
    }

    mCamDrawable.mSelected = input.isSelected();

    const FnAttribute::DoubleAttribute coiAttr = input.getLiveAttribute("geometry.centerOfInterest");
    if (coiAttr.isValid()) {
        mCamDrawable.mHasCenterOfInterest = true;
        mCamDrawable.mCenterOfInterest =
                static_cast<float>(coiAttr.getValue(20.0, false));

        // Correct centerOfInterest length to be in local space
        const FnAttribute::DoubleAttribute ctmAttr(input.getLiveWorldSpaceXform());
        if (ctmAttr.isValid()) {
            const FnAttribute::DoubleAttribute::array_type value(ctmAttr.getNearestSample(0));
            const double a = value[8];
            const double b = value[9];
            const double c = value[10];
            const float  s = static_cast<float>(std::sqrt(a * a + b * b + c * c));
            mCamDrawable.mCenterOfInterest /= s;
        }
    }
    else {
        mCamDrawable.mHasCenterOfInterest = false;
    }

    mCamDrawable.draw();
}

FnKat::DoubleAttribute
CameraViewerModifier::getLocalSpaceBoundingBox(FnKat::ViewerModifierInput& input)
{
    double bounds[6];
    mCamDrawable.getBBox(bounds);
    return FnKat::DoubleAttribute(bounds, 6, 1);
}

