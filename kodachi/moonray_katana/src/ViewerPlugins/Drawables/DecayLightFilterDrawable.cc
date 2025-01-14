// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "DecayLightFilterDrawable.h"
#include "../Drawables/LightDrawable.h"
#include <GL/gl.h>
#include <GL/glu.h>

#include <kodachi_moonray/light_util/LightUtil.h>

namespace MoonrayKatana
{
    using namespace FnAttribute;

    const float DecayLightFilterDrawable::sColors[4][3] = {
        { 0,0,0 }, {1,1,1}, {1,1,1}, {0,0,0}
    };

void
DecayLightFilterDrawable::setup(const FnAttribute::GroupAttribute& root)
{
    LightFilterDrawable::setup(root);

    GroupAttribute params = kodachi_moonray::light_util::getShaderParams(root.getChildByName("material"), "moonrayLightfilter");
    falloff_near = IntAttribute(params.getChildByName("falloff_near")).getValue(false, false);
    falloff_far = IntAttribute(params.getChildByName("falloff_far")).getValue(false, false);
    mRadius[0] = FloatAttribute(params.getChildByName("near_start")).getValue(-1.0f, false);
    mRadius[1] = FloatAttribute(params.getChildByName("near_end")).getValue(-1.0f, false);
    mRadius[2] = FloatAttribute(params.getChildByName("far_start")).getValue(-1.0f, false);
    mRadius[3] = FloatAttribute(params.getChildByName("far_end")).getValue(-1.0f, false);
}

void
DecayLightFilterDrawable::draw()
{
    if (!mParent) {
        return;
    }

    if (mParent->mLookThrough) return;

    LightFilterDrawable::draw();

    if (!mPicking) {
        glLineWidth(0.3f);
    }

    // Undo scale transformation. Coordinates will be manually scaled.
    GLfloat matrix[16];
    glGetFloatv(GL_MODELVIEW_MATRIX, matrix);
    const float scale[3] = {
        sqrtf(matrix[0] * matrix[0] + matrix[1] * matrix[1] + matrix[2] * matrix[2]),
        sqrtf(matrix[4] * matrix[4] + matrix[5] * matrix[5] + matrix[6] * matrix[6]),
        sqrtf(matrix[8] * matrix[8] + matrix[9] * matrix[9] + matrix[10] * matrix[10])
    };

    LightDrawable::Type type = mParent->mType;

    glPushMatrix();
        glScalef(1.0f / scale[0], 1.0f / scale[1], 1.0f / scale[2]);

        float prev = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if (not (i<2 ? falloff_near : falloff_far)) continue;
            if (mRadius[i] > prev) {
                prev = mRadius[i];
                if (i == 0 && mRadius[1] <= prev) i = 1; // prefer white over black
                if (!mPicking) {
                    if (i == 0 || i == 3 || showSelected()) {
                        glColor3f(sColors[i][0], sColors[i][1], sColors[i][2]);
                    } else { // draw white ones colored when not selected
                        glColor4fv(mColor);
                    }
                }

                // Draw each type differently.
                switch(type) {
                case LightDrawable::POINT:
                default:
                    drawPointFilter(mRadius[i], scale);
                    break;
                case LightDrawable::SPHERE:
                    drawSphereFilter(mRadius[i], scale);
                    break;
                case LightDrawable::CYLINDER:
                    drawCylinderFilter(mRadius[i], scale);
                    break;
                case LightDrawable::SPOT:
                    drawSpotFilter(mRadius[i], scale);
                    break;
                case LightDrawable::RECT:
                    drawRectFilter(mRadius[i], scale);
                    break;
                case LightDrawable::DISK:
                    drawDiskFilter(mRadius[i], scale);
                    break;
                case LightDrawable::DISTANT:
                    break;
                case LightDrawable::ENV:
                    break;
//                {
//                    if (mRadius[i] < mParent->mXsize * scale[0]
//                                      && mRadius[i] < mParent->mYsize * scale[1]
//                                      && mRadius[i] < mParent->mZsize * scale[2]) {
//                        glColor3f(sColors[i].r, sColors[i].g, sColors[i].b);
//                        glPushMatrix();
//                        glScalef(scale[0] - mRadius[i] / mParent->mXsize,
//                                 scale[1] - mRadius[i] / mParent->mYsize,
//                                 scale[2] - mRadius[i] / mParent->mZsize);
//                        GLUquadric* quad;
//                        quad = gluNewQuadric();
//                        gluQuadricDrawStyle(quad, GLU_LINE);
//                        gluSphere(quad, mParent->mXsize, 15.0f, 15.0f);
//                        gluDeleteQuadric(quad);
//                        glPopMatrix();
//                    }
//                    break;
//                }
                }
            }
        }

    glPopMatrix();
}

void
DecayLightFilterDrawable::drawPointFilter(float radius, const float (&scale)[3]) const
{
    glPushMatrix();
        glScalef(scale[0] + radius,
                 scale[1] + radius,
                 scale[2] + radius);
        GLUquadric* quad = gluNewQuadric();
        gluQuadricDrawStyle(quad, GLU_LINE);
        gluSphere(quad, 1.0, 15, 15);
        gluDeleteQuadric(quad);
    glPopMatrix();
}

void
DecayLightFilterDrawable::drawSphereFilter(float radius, const float (&scale)[3]) const
{
    glPushMatrix();
        // Since decay filter is a constant number of units away from the light,
        // the default scaling matrix will scale the number of units away too. To
        // prevent this, the scaling transformation is rewritten such that the
        // scale is only applied to mXsize but not radius.
        glScalef(scale[0] + radius / mParent->mXsize,
                 scale[1] + radius / mParent->mYsize,
                 scale[2] + radius / mParent->mZsize);
        GLUquadric* quad = gluNewQuadric();
        gluQuadricDrawStyle(quad, GLU_LINE);
        gluSphere(quad, mParent->mXsize, 15, 15);
        gluDeleteQuadric(quad);
    glPopMatrix();
}

void
DecayLightFilterDrawable::drawCylinderFilter(float radius, const float (&scale)[3]) const
{
    // To make it easier to use the existing drawCircle functions, apply a
    // fixed 90-degree X rotation to mimic Moonray's default orientation.
    // However, doing this causes the scale indices to change such that
    // scale[2] is now Y and scale[1] is now Z.
    glPushMatrix();
        glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        float r1 = mParent->mXsize;
        float r2 = mParent->mYsize;
        float z = scale[1] * mParent->mZsize;
        drawCylinder(scale[0] * r1 + radius, scale[2] * r2 + radius, z);

        // Draw the end-caps on each side of the capsule.
        for (int side = 0; side < 2; ++side) {
            static const float N = 6;
            for (int i = 0; i < N; ++i) {
                const float a = i * (2 * M_PI / N);

                glPushMatrix();
                glTranslatef(r1 * sinf(a), r2 * cosf(a), side == 0 ? z : -z);
                    glRotatef(side == 0 ? -90.0f : 90.0f, 0.0f, 1.0f, 0.0f);
                    glRotatef((side == 0 ? -i : i) * 360.0f / N, 1.0f, 0.0f, 0.0f);
                    drawCircle(radius, radius, 0, 0.25f);
                glPopMatrix();
            }
            drawCircle(r1 * scale[0], r2 * scale[2], (side == 0 ? -1 : 1) * (z + radius));
        }
    glPopMatrix();
}

void
DecayLightFilterDrawable::drawSpotFilter(float radius, const float (&scale)[3]) const
{
    // Parallel circle to spot light's light source
    const float r1 = mParent->mXsize * scale[0];
    const float r2 = mParent->mYsize * scale[1];

    glBegin(GL_LINES);
        glVertex3f(-r1, 0.0f, -radius);
        glVertex3f(r1, 0.0f, -radius);
        glVertex3f(0.0f, -r2, -radius);
        glVertex3f(0.0f, r2, -radius);
    glEnd();
    drawCircle(r1, r2, -radius);

    // Intersection of cone and radius
    const float s0 = r1 * mParent->mSlope / scale[2]; // slope in world space
    float x = radius / sqrt(s0 * s0 + 1);
    float y = s0 * x + r1;
    // same calculation for steeper part of cone
    // much more complex quadratic due to radius being from different point than slope
    const float s = r1 * mParent->mSlope2 / scale[2];
    const float x2 = (sqrt(radius*radius*(s*s+1)-4*r1) + 2*s*r1) / (s*s+1);
    const float y2 = s * x2 - r1;
    // use which ever is larger
    if (y2 > y) { y = y2; x = x2; }

    drawCircle(y, y * r2 / r1, -x);

    const float angle = atan2(y - r1, x) / (2 * M_PI);

    // Connection lines from parallel circle to cone intersection
    const int N = 4;
    for (int j = 0; j < N; ++j) {
        glPushMatrix();
            glScalef(1, r2/r1, 1);
            glRotatef(-360.0f / N * j + 90.0f, 0.0f, 0.0f, 1.0f);
            glTranslatef(r1, 0, 0);
            glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
            drawCircle(radius, radius, 0.0f, angle);
        glPopMatrix();
    }
}

void
DecayLightFilterDrawable::drawRectFilter(float radius, const float (&scale)[3]) const
{
    const float r1 = mParent->mXsize * scale[0];
    const float r2 = mParent->mYsize * scale[1];

    // Draw outline on same plane as light
    glBegin(GL_LINES);
    glVertex3f(r1 + radius, -r2, 0.0f);
    glVertex3f(r1 + radius, r2, 0.0f);
    glVertex3f(-r1, r2 + radius, 0.0f);
    glVertex3f(r1, r2 + radius, 0.0f);
    glVertex3f(-r1 - radius, -r2, 0.0f);
    glVertex3f(-r1 - radius, r2, 0.0f);
    glVertex3f(-r1, -r2 - radius, 0.0f);
    glVertex3f(r1, -r2 - radius, 0.0f);
    glEnd();

    // Rect light has rounded edges on same plane
    glPushMatrix();
    glTranslatef(r1, r2, 0);
    drawCircle(radius, radius, 0, 0.25f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-r1, r2, 0);
    glRotatef(90.0f, 0.0f, 0.0f, 1.0f);
    drawCircle(radius, radius, 0, 0.25f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-r1, -r2, 0);
    glRotatef(180.0f, 0.0f, 0.0f, 1.0f);
    drawCircle(radius, radius, 0, 0.25f);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(r1, -r2, 0);
    glRotatef(270.0f, 0.0f, 0.0f, 1.0f);
    drawCircle(radius, radius, 0, 0.25f);
    glPopMatrix();

    // Draw N lines both vertically and horizontally across
    // the rect light, rounded at the edges
    static const int N = 4;
    for (int j = 0; j < N; ++j) {
        // Vertical
        const float pos1 = r1 - j * 2 * r1 / (N - 1);
        glBegin(GL_LINES);
        glVertex3f(pos1, -r2, -radius);
        glVertex3f(pos1, r2, -radius);
        glEnd();

        glPushMatrix();
        glTranslatef(pos1, -r2, 0.0f);
        glRotatef(-90.0f, 1.0f, 0.0f, 0.0f);
        glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        drawCircle(radius, radius, 0, 0.25f);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(pos1, r2, 0.0f);
        glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        drawCircle(radius, radius, 0, 0.25f);
        glPopMatrix();

        // Horizontal
        const float pos2 = r2 - j * 2 * r2 / (N - 1);
        glBegin(GL_LINES);
        glVertex3f(-r1, pos2, -radius);
        glVertex3f(r1, pos2, -radius);
        glEnd();
        glPushMatrix();
        glTranslatef(-r1, pos2, 0.0f);
        glRotatef(180.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        drawCircle(radius, radius, 0, 0.25f);
        glPopMatrix();
        glPushMatrix();
        glTranslatef(r1, pos2, 0.0f);
        glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
        drawCircle(radius, radius, 0, 0.25f);
        glPopMatrix();
    }
}

void
DecayLightFilterDrawable::drawDiskFilter(float radius, const float (&scale)[3]) const
{
    const float r1 = mParent->mXsize * scale[0];
    const float r2 = mParent->mYsize * scale[1];
    // One circle on same plane as light, one circle parallel to light
    drawCircle(r1 + radius, r2 + radius, 0);
    drawCircle(r1, r2, -radius);

    static const float N = 6;
    for (int circle = 0; circle < N; ++circle) {
        const float a = circle * (2 * M_PI / N);

        // Draw arcs to connect them
        glPushMatrix();
        glTranslatef(r1 * sinf(a), r2 * cosf(a), 0);
        glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
        glRotatef(360.0f / N * circle, 1.0f, 0.0f, 0.0f);
        drawCircle(radius, radius, 0, 0.25f);
        glPopMatrix();

        // Extra lines inside parallel circle
        glBegin(GL_LINES);
        glVertex3f(r1 * sinf(a), r2 * cosf(a), -radius);
        glVertex3f(0.0f, 0.0f, -radius);
        glEnd();
    }
}

}

