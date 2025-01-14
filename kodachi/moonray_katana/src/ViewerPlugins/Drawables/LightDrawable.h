// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Drawable.h"
#include <FnAttribute/FnAttribute.h>

namespace MoonrayKatana {

class LightFilterDrawable;
class MeshLightImpl;

// Reusable object for new Viewer api
class LightDrawable : public Drawable
{
public:
    LightDrawable(const std::string& location) : Drawable(location) {}
    ~LightDrawable();

    enum Type {
        POINT = 0, // not supported by moonray
        SPHERE,
        CYLINDER,
        SPOT,
        RECT,
        DISK,
        DISTANT,
        ENV,
        MESH
    };

    virtual void setup(const FnAttribute::GroupAttribute& root) override;
    virtual void draw() override;

    void getBBox(double bounds[6], bool extent = true) const; // for old viewer
    FnAttribute::DoubleAttribute getBounds() const override;
    FnAttribute::DoubleAttribute getExtent() const override;

    Type mType = POINT;
    // these dimensions are from origin to furthest point, ie 1/2 the diameter:
    float mXsize = 0.5f;
    float mYsize = 0.5f;
    float mZsize = 0.5f;
    float mSlope = 0.0f; // used by spot
    float mInnerSlope = 0.0f; // used by spot
    float mSlope2 = 0.0f; // used by spot
    float mFocalPlane = 10.0f; // used by spot
    float mCenterOfInterest = 20.0f;

    MeshLightImpl* mesh = nullptr; // contains the mesh light data
    float getScale() const;

private:
    void drawLightCircle(float z, float s) const;
    void drawLightCircleConnectingLines(float z0, float z1, float s0 = 1, float s1 = 1) const;
    void drawArrow(float len, float x, float y, float z) const;
    bool drawCenterOfInterest() const;
    void drawPointLight() const;
    void drawRectLight() const;
    void drawDiskLight() const;
    void drawSpotCircles(float z) const;
    void drawSpotLight() const;
    void drawCylinderLight() const;
    void drawDistantLight() const;
    void drawSphereLight() const;
    void drawEnvLight() const;
};

}

