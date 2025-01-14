// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <FnAttribute/FnAttribute.h>
#include <algorithm>
#include <math.h>

namespace MoonrayKatana {

// Reusable object for new Viewer api
class Drawable
{
public:
    Drawable(const std::string& location);
    virtual ~Drawable() {}

    virtual void setup(const FnAttribute::GroupAttribute& root);
    virtual void draw();
    virtual void ancestorChanged(Drawable* drawable) {}
    virtual FnAttribute::DoubleAttribute getBounds() const;
    virtual FnAttribute::DoubleAttribute getExtent() const;

    virtual bool isVisible() const { return !mHidden; }

    bool mLookThrough = false;
    bool mPicking = false;
    bool mSelected = false;
    bool mAncestorSelected = false;
    bool mChildSelected = false;
    bool mHidden = false;
    bool mMuted = false;
    bool mAllLightCones = false;
    float mColor[4] = {1,1,0,1};
    FnAttribute::GroupAttribute mRootAttr;
    // Store a single string attr called 'location' with the
    // full location path. This is used by Foundry's default
    // picker implementation.
    FnAttribute::GroupAttribute mLocationAttr;

    static float selectionColor[4]; // LightLayer updates this with preference
    float scaleFactor = 1; // set by LightLayer to scale to apply to icons so 1 unit ~= 1 pixel

protected:
    bool showSelected() const { return mSelected | mAncestorSelected; }
    bool showFrustum() const {
        return mSelected | mChildSelected | ((mAncestorSelected | mAllLightCones) && !mMuted); }
    // Set color for icon depending on selection+muting, and line width
    void setColorAndLineWidth() const;
    // Set color and line width for frustum
    void setFrustumColorAndLineWidth() const;
    // Draw a 2*r1,2*r2 square (it will surround circle of same size)
    static void drawRect(float r1, float r2, float z);
    // Draw a circle perpendicular to the Z plane.
    // Percent of 0 draws nothing while Percent of 1 draws a full circle.
    static void drawCircle(float r1, float r2, float z, float percent = 1);
    // Draw a higher-resolution circle
    static void drawCircle128(float r1, float r2, float z, float percent = 1);
    // Draw a cylinder centered at origin from -z to +z
    static void drawCylinder(float r1, float r2, float z);
};

}

