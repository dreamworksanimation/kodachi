// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "Drawable.h"
#include "VAOBuilder.h"
#include <FnAttribute/FnGroupBuilder.h>

namespace MoonrayKatana
{

Drawable::Drawable(const std::string& location)
{
    if (!location.empty()) {
        FnAttribute::GroupBuilder locationBuilder;
        locationBuilder.set("location", FnAttribute::StringAttribute(location));
        mLocationAttr = locationBuilder.build();
    }
}

void
Drawable::setup(const FnAttribute::GroupAttribute& root)
{
    mRootAttr = root;
    // Set the color to use for drawing the light's representation
    // Katana used "geometry.previewColor" but this setting is alterable
    // by the ViewerObjectSettings and thus seems to make more sense:
    const FnAttribute::FloatAttribute colorAttr = root.getChildByName("viewer.default.drawOptions.color");
    if (colorAttr.isValid()) {
        const auto value = colorAttr.getNearestSample(0);
        if (value.size() >= 3) {
            mColor[0] = value[0];
            mColor[1] = value[1];
            mColor[2] = value[2];
        }
    }
}

void
Drawable::draw()
{
    setColorAndLineWidth();
}

float Drawable::selectionColor[4] = {1,1,1,1};

void
Drawable::setColorAndLineWidth() const
{
    if (mPicking) {
        // use thick lines for picking
        glLineWidth(10);
        // don't override pick color
    } else {
        glLineWidth(2);
        if (showSelected())
            glColor4fv(selectionColor);
        else if (mMuted)
            glColor3f(0.3f, 0.3f, 0.3f);
        else
            glColor4fv(mColor);
    }
}

void
Drawable::setFrustumColorAndLineWidth() const
{
    if (mPicking) {
        // use thick lines for picking
        glLineWidth(10);
        // don't override pick color
    } else {
        glLineWidth(1);
        if (mMuted)
            glColor3f(0.3f, 0.3f, 0.3f);
        else
            glColor4fv(mColor);
    }
}

// draw a circle (actually an ellipse of r1,r2 radius) about 0,0,z.
// Percent if fraction of arc starting at r1,0,z, 1 draws entire circle
void
Drawable::drawCircle(float r1, float r2, float z, float percent)
{
    glPushMatrix();
    glScalef(r1, r2, 1);
    glTranslatef(0, 0, z);
    static const int N = 64;
    static VAO vao;
    if (not vao.isReady())
        VAOBuilder::generateCircle(Vec3f(0,0,0), 1.0f, N, vao);
    vao.draw(GL_LINES, unsigned(2*N*percent));
    glPopMatrix();
}

// draw a higher-rez circle (actually an ellipse of r1,r2 radius) about 0,0,z.
// Percent if fraction of arc starting at r1,0,z, 1 draws entire circle
void
Drawable::drawCircle128(float r1, float r2, float z, float percent)
{
    glPushMatrix();
    glScalef(r1, r2, 1);
    glTranslatef(0, 0, z);
    static const int N = 128;
    static VAO vao;
    if (not vao.isReady())
        VAOBuilder::generateCircle(Vec3f(0,0,0), 1.0f, N, vao);
    vao.draw(GL_LINES, unsigned(2*N*percent));
    glPopMatrix();
}

// draw a rectangle of 2*r1,2*r2 about 0,0,z
void
Drawable::drawRect(float r1, float r2, float z)
{
    glPushMatrix();
    glScalef(r1, r2, 1);
    glTranslatef(0, 0, z);
    static VAO vao;
    if (not vao.isReady()) {
        static const std::vector<Vec3f> vertices {{-1,-1,0}, {1,-1,0}, {1,1,0}, {-1,1,0}};
        static const std::vector<unsigned> indices {0,1, 1,2, 2,3, 3,0};
        vao.setup(vertices, indices);
    }
    vao.drawLines();
    glPopMatrix();
}

// cylinder has ends of circle(r1,r2,-z) and circle(r1,r2,z)
void
Drawable::drawCylinder(float r1, float r2, float z)
{
    glPushMatrix();
    glScalef(r1, r2, z);
    static VAO vao;
    if (not vao.isReady()) {
        const unsigned N = 60; // must be multiple of 6
        std::vector<Vec3f> vertices; vertices.reserve(N*2);
        std::vector<unsigned> indices; indices.reserve(N*4+(N/10)*2);
        for (int i = 0; i < N; i++) {
            float a = i * (2 * M_PI / N);
            float x = sinf(a);
            float y = cosf(a);
            unsigned i0 = vertices.size();
            unsigned i1 = i < N-1 ? i0+2 : 0;
            vertices.emplace_back(x, y, 1);
            indices.push_back(i0);
            indices.push_back(i1);
            vertices.emplace_back(x, y, -1);
            indices.push_back(i0+1);
            indices.push_back(i1+1);
            if (not (i%10)) {
                indices.push_back(i0);
                indices.push_back(i0+1);
            }
        }
        vao.setup(vertices, indices);
    }
    vao.drawLines();
    glPopMatrix();
}

FnAttribute::DoubleAttribute
Drawable::getBounds() const { return {}; }

FnAttribute::DoubleAttribute
Drawable::getExtent() const { return {}; }

}
