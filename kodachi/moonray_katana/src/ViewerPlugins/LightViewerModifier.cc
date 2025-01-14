// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <map>
#include <GL/gl.h>
#include <FnGeolib/util/Path.h>

#include "LightViewerModifier.h"

namespace {
std::map<std::string, MoonrayKatana::LightDrawable*> sLightMap;
}

LightViewerModifier::~LightViewerModifier()
{
    sLightMap.erase(mLocation);
}

void
LightViewerModifier::setup(FnKat::ViewerModifierInput& input)
{
    if (not mDrawable) {
        mDrawable.reset(new MoonrayKatana::LightDrawable(std::string()));
        mLocation = input.getFullName();
        sLightMap[mLocation] = mDrawable.get();
    }
    mDrawable->setup(input.getAttribute(""));
}

void
LightViewerModifier::draw(FnKat::ViewerModifierInput& input)
{
    mDrawable->mLookThrough = input.isLookedThrough();
    // Read attributes that change without causing setup() to be called:
    mDrawable->mSelected = input.isSelected();
    mDrawable->mPicking = input.getDrawOption("isPicking");
    mDrawable->mCenterOfInterest = FnAttribute::DoubleAttribute(
        input.getLiveAttribute("geometry.centerOfInterest")).getValue(20.0, false);
    // Correct centerOfInterest length to be in local space
    FnAttribute::DoubleAttribute ctmAttr(input.getLiveWorldSpaceXform());
    if (ctmAttr.isValid()) {
        FnAttribute::DoubleAttribute::array_type value(ctmAttr.getNearestSample(0));
        double a = value[8];
        double b = value[9];
        double c = value[10];
        double s = sqrt(a*a+b*b+c*c);
        mDrawable->mCenterOfInterest /= s;
    }
    glDisable(GL_LIGHTING);
    mDrawable->draw();
}

FnAttribute::DoubleAttribute
LightViewerModifier::getLocalSpaceBoundingBox(FnKat::ViewerModifierInput& input)
{
    // unfortunately it does not call this again when these change, but this sort of
    // works in many cases:
    mDrawable->mLookThrough = input.isLookedThrough();
    mDrawable->mSelected = input.isSelected();
    double bounds[6];
    mDrawable->getBBox(bounds);
    return FnAttribute::DoubleAttribute(bounds, 6, 1);
}

void
LightFilterViewerModifier::setup(FnKat::ViewerModifierInput& input)
{
    if (not mDrawable) {
        MoonrayKatana::LightDrawable* light = nullptr;
        auto iter = sLightMap.find(FnKat::Util::Path::GetLocationParent(input.getFullName()));
        if (iter != sLightMap.end())
            light = iter->second;
        mDrawable.reset(MoonrayKatana::LightFilterDrawable::create(light, input.getFullName(),
                                                                   input.getAttribute("")));
    }
}

void
LightFilterViewerModifier::draw(FnKat::ViewerModifierInput& input)
{
    // Read attributes that change without causing setup() to be called:
    mDrawable->mSelected = input.isSelected();
    mDrawable->mPicking = input.getDrawOption("isPicking");
    glDisable(GL_LIGHTING);
    mDrawable->draw();
}

FnAttribute::DoubleAttribute
LightFilterViewerModifier::getLocalSpaceBoundingBox(FnKat::ViewerModifierInput& input)
{
    double bounds[6] = {-1.0, 1.0, -1.0, 1.0, -1.0, 1.0};
    return FnAttribute::DoubleAttribute(bounds, 6, 1);
}

