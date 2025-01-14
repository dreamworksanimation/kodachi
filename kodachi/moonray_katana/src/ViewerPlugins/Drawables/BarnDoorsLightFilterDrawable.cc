// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "BarnDoorsLightFilterDrawable.h"

#include "../Drawables/LightDrawable.h"

#include <kodachi_moonray/light_util/LightUtil.h>

#include <GL/gl.h>

namespace MoonrayKatana
{
    using namespace FnAttribute;

void
BarnDoorsLightFilterDrawable::setup(const FnAttribute::GroupAttribute& root)
{
    LightFilterDrawable::setup(root);
    update = true;
}

void
BarnDoorsLightFilterDrawable::ancestorChanged(Drawable* drawable)
{
    if (drawable == mParent) update = true;
}

void
BarnDoorsLightFilterDrawable::draw()
{
    if (!mParent) return;

    LightFilterDrawable::draw();

    if (update) {
        float vertices[3*8];
        unsigned indices[24];
        auto&& spotShader = kodachi_moonray::light_util::getShaderParams(mParent->mRootAttr.getChildByName("material"));
        auto&& lightShader = kodachi_moonray::light_util::getShaderParams(mRootAttr.getChildByName("material"), "moonrayLightfilter");
        kodachi_moonray::light_util::populateBarnDoorBuffers(spotShader, lightShader,
            vertices, (int*)indices);
        if (vao.isReady()) {
            vao.updateVertices(vertices, 8);
        } else {
            // rearrange vertices to make GL_LINES work
            unsigned int* p = &indices[0];
            *p++ = 0; *p++ = 1;
            *p++ = 1; *p++ = 2;
            *p++ = 2; *p++ = 3;
            *p++ = 3; *p++ = 0;
            *p++ = 4; *p++ = 5;
            *p++ = 5; *p++ = 6;
            *p++ = 6; *p++ = 7;
            *p++ = 7; *p++ = 4;
            *p++ = 0; *p++ = 4;
            *p++ = 1; *p++ = 5;
            *p++ = 2; *p++ = 6;
            *p++ = 3; *p++ = 7;
            vao.setup(vertices, 8, indices, 24);
        }
        update = false;
    }
    vao.drawLines();
}

}

