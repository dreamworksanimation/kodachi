// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "Drawable.h"
#include "VAO.h"

// Katana
#include <FnAttribute/FnAttribute.h>

namespace MoonrayKatana
{

class CameraDrawable : public Drawable
{
public:
    CameraDrawable(const std::string& location = std::string());
    ~CameraDrawable();

    virtual void setup(const FnAttribute::GroupAttribute& root) override;
    virtual void draw() override;
    void getBBox(double bounds[6]) const;

    //-------------------
    // Data members

    bool  mHasCenterOfInterest = true;
    float mCenterOfInterest = 20.0f;

private:
    void buildCamera();

    MoonrayKatana::VAO mCameraMesh;
    MoonrayKatana::VAO mFrustumMesh;
    std::vector<Vec3f> mFrustumVertices{8};
    bool updateVertices = false;
};

} // namespace KatanaPlugins

