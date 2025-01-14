// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightLayer.h"

#include <GL/glew.h>

#include "LightDelegateComponent.h"

using namespace FnKat::ViewerAPI;

void
LightLayer::setup()
{
    glewInit();
    mLightDelegateComponent = getViewport()->getViewerDelegate()->getComponent(
        LightDelegateComponent::NAME)->getPluginInstance<LightDelegateComponent>();
}

LightLayer::~LightLayer()
{
}

void
LightLayer::draw()
{
    // Retrieve the selection color from the preferences. This is shared by all
    // MoonrayKatana::Drawable, even ones for other layers and manipulators.
    static const auto kSelectionColor(OptionIdGenerator::GenerateId("ViewerDelegate.SelectionColor"));
    FnKat::FloatAttribute attr(getViewport()->getViewerDelegate()->getOption(kSelectionColor));
    if (attr.isValid()) {
        auto a(attr.getNearestSample(0));
        for (int i = 0; i < a.size() && i < 4; ++i)
            MoonrayKatana::Drawable::selectionColor[i] = a[i];
    }
    genericDraw();
}

void
LightLayer::pickerDraw(unsigned int x, unsigned int y,
                       unsigned int w, unsigned int h,
                       const PickedAttrsMap& ignoreAttrs)
{
    genericDraw(&ignoreAttrs);
}

void
LightLayer::genericDraw(const PickedAttrsMap* const ignoreAttrs)
{
    if (mLightDelegateComponent->mDrawables.empty())
        return;

    const bool picking = ignoreAttrs != nullptr;
    ViewportWrapperPtr viewport = getViewport();
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixd(viewport->getProjectionMatrix());
    glMatrixMode(GL_MODELVIEW);
    std::string cameraLocation;
    {ViewportCameraWrapperPtr camera(viewport->getActiveCamera());
     if (camera) cameraLocation = camera->getLocationPath();}

    for (const auto& i : mLightDelegateComponent->mDrawables) {
        MoonrayKatana::Drawable* drawable = i.second.get();
        if (!drawable || !drawable->isVisible()) continue;

        drawable->mLookThrough = (i.first == cameraLocation);
        drawable->mAllLightCones = mAllLightCones;

        if (!picking) {
            // Only anti-alias in regular drawing mode. The picker
            // buffer MUST fill all pixels with the same exact color
            // for an object.
            glEnable(GL_MULTISAMPLE);
        } else {
            FnPickId id = addPickableObject(drawable->mLocationAttr);
            // Ignore attrs already in this map, as per pickerDraw docs
            if (ignoreAttrs->find(id) != ignoreAttrs->end()) continue;

            drawable->mPicking = true;
            Vec4f color;
            pickIdToColor(id, color);
            glColor4f(color.x, color.y, color.z, color.w);
        }

        const double* const vM(viewport->getViewMatrix());
        glLoadMatrixd(vM);
        const double* const xM(viewport->getViewerDelegate()->getWorldXform(i.first).data);
        glMultMatrixd(xM);
        // compute size of one pixel if object was in center of viewport
        double dist = vM[2]*xM[12] + vM[6]*xM[13] + vM[10]*xM[14] + vM[14];
        double psize = std::min(viewport->getWidth(), viewport->getHeight()) / (1 - 2 * dist);
        // If power is 1 then it stays the same size on screen at all times, if 0 then
        // it stays the same size in 3D space. Other values weigh these two scales.
        drawable->scaleFactor = pow(std::max(1e-6, 1 / psize), .75);

        drawable->draw();

        if (picking) {
            drawable->mPicking = false;
        }
    }

    glLineWidth(1);
}

void LightLayer::setOption(FnKat::ViewerAPI::OptionIdGenerator::value_type id,
                           FnAttribute::Attribute attr)
{
    static const auto kAllLightCones(OptionIdGenerator::GenerateId("allLightCones"));
    if (id == kAllLightCones) {
        mAllLightCones = FnAttribute::IntAttribute(attr).getValue(0, false);
        getViewport()->setDirty(true);
    } else {
        FnKat::ViewerAPI::ViewportLayer::setOption(id, attr);
    }
}

