// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightDrawable.h"

#include <GL/gl.h>
#include "VAO.h"
#include <kodachi_moonray/light_util/LightUtil.h>
#include <kodachi/op/XFormUtil.h>
#include <OpenEXR/ImathMatrix.h>

#include <iostream>

namespace MoonrayKatana
{
using namespace FnAttribute;

static void drawStar()
{
    static VAO vao;
    if (not vao.isReady()) {
        static const std::vector<Vec3f> vertices{
            { -1,  0,  0},             {  1,  0,  0},
            {  0, -1,  0},             {  0,  1,  0},
            {  0,  0, -1},             {  0,  0,  1},
            { -0.707, -0.707,  0    }, { 0.707,  0.707,  0    },
            {  0,     -0.707, -0.707}, { 0,      0.707,  0.707},
            { -0.707,  0,     -0.707}, { 0.707,  0,      0.707},
            { -0.707,  0.707,  0    }, { 0.707, -0.707,  0    },
            {  0,     -0.707,  0.707}, { 0,      0.707, -0.707},
            { -0.707,  0,      0.707}, { 0.707,  0,     -0.707}
        };
        std::vector<unsigned int> indices;
        indices.resize(vertices.size());
        for (unsigned i = 0; i < vertices.size(); ++i) indices[i] = i;
        vao.setup(vertices, std::vector<Vec3f>(), indices);
    }
    vao.drawLines();
}

///////////////////////////////////////////////////////////////////////////////////////////////

struct Line {
    unsigned a,b;
    Line(int A, int B) { if (A<B) {a=A; b=B;} else {a=B; b=A;}}
    Line(): a(0),b(0) {} // needed for I.resize() to compile
    bool operator<(const Line& x) const { return a<x.a || (a == x.a && b < x.b); }
    bool operator==(const Line& x) const { return a == x.a && b == x.b; }
};

class
MeshLightImpl {
public:
    VAO vao;
    DoubleAttribute bounds;
    DoubleAttribute xform;
    bool noMesh = true;
    bool noPrevMesh = true;
    Foundry::Katana::Hash hash;

    // data used to update the vao in draw.
    FloatConstVector P;
    std::vector<Line> I;

    void setup(const GroupAttribute& root) {

        xform = kodachi::XFormUtil::CalcTransformMatrixAtTime(
            root.getChildByName("mesh_xform"), 0.0f).first;

        noMesh = true; // this is turned off if everything works
        GroupAttribute mesh(root.getChildByName("mesh"));
        if (not mesh.isValid()) return;
        Foundry::Katana::Hash newHash(mesh.getHash());
        if (newHash == hash) { noMesh = noPrevMesh; return; }
        hash = newHash;
        noPrevMesh = true;

        FloatAttribute Pattr(mesh.getChildByName("point.P"));
        P = Pattr.getNearestSample(0.0f);
        size_t numP = P.size() / 3;
        if (numP < 2) return;

        double bounds[6];
        bounds[0] = bounds[1] = P[0];
        bounds[2] = bounds[3] = P[1];
        bounds[4] = bounds[5] = P[2];
        for (size_t i = 3; i < numP; i += 3) {
            bounds[0] = std::min(bounds[0], double(P[i+0]));
            bounds[1] = std::max(bounds[1], double(P[i+0]));
            bounds[2] = std::min(bounds[2], double(P[i+1]));
            bounds[3] = std::max(bounds[3], double(P[i+1]));
            bounds[4] = std::min(bounds[4], double(P[i+2]));
            bounds[5] = std::max(bounds[5], double(P[i+2]));
        }
        this->bounds = DoubleAttribute(bounds, 6, 2);

        IntAttribute sIattr(mesh.getChildByName("poly.startIndex"));
        auto&& sI(sIattr.getNearestSample(0.0f));
        if (sI.size() < 2) return;
        IntAttribute vLattr(mesh.getChildByName("poly.vertexList"));
        auto&& vL(vLattr.getNearestSample(0.0f));
        if (vL.size() < 2) return;

        I.clear(); I.reserve(vL.size());
        auto&& faces(IntAttribute(mesh.getChildByName("faces")).getNearestSample(0.0f));
        size_t n = faces.empty() ? sI.size()-1 : faces.size();
        for (size_t i = 0; i < n; ++i) {
            int face = faces.empty() ? i : faces[i];
            size_t i0 = sI[face];
            size_t i1 = sI[face+1];
            if (i1 <= i0) continue;
            I.emplace_back(vL[i0], vL[i1-1]);
            for (size_t j = i0+1; j < i1; ++j)
                I.emplace_back(vL[j], vL[j-1]);
        }

        auto start = I.begin();
        auto end = I.end();
        std::sort(start, end);
        I.resize(std::unique(start, end) - start);
        noMesh = noPrevMesh = false;
    }

    bool draw() {
        glPushMatrix();
        auto&& array(xform.getNearestSample(0.0f));
        glMultMatrixd(&array[0]);
        if (noMesh) {
            drawStar();
        } else {
            if (not I.empty()) { // VAO update needed
                vao.setup(&P[0], P.size()/3, &I[0].a, 2*I.size());
                I.clear();
            }
            vao.drawLines();
        }
        glPopMatrix();
        return true;
    }

    DoubleAttribute getBounds() const {
        return kodachi::XFormUtil::CalcTransformedBoundsAtExistingTimes(xform, bounds);
    }

};

///////////////////////////////////////////////////////////////////////////////////////////////

// Because the shader can be changed in the gaffer this object must be able to change
// from one type to another, that is why case statements are used rather than a
// different class for each type of light.
void
LightDrawable::setup(const GroupAttribute& root)
{
    Drawable::setup(root);

    FloatAttribute a1, a2;

    auto muteAttr = StringAttribute(root.getChildByName("info.light.muteState"));
    mMuted = muteAttr.isValid() && muteAttr != "muteEmpty";
    auto oldType = mType;

    const GroupAttribute materialAttr = root.getChildByName("material");
    if (materialAttr.isValid()) {
        std::string shaderName = kodachi_moonray::light_util::getShaderName(materialAttr);
        const auto params = kodachi_moonray::light_util::getShaderParams(materialAttr);
        if (params.isValid()) {
            if (shaderName == "SpotLight") {
                a2 = params.getChildByName("aspect_ratio");
                const float aspect_ratio = a2.getValue(1.0f, false);
                a2 = params.getChildByName("lens_radius");
                const float radius = a2.getValue(1.0f, false);
                a2 = params.getChildByName("focal_plane_distance");
                mFocalPlane = a2.getValue(10.0f, false);

                mType = SPOT;
                mXsize = radius;
                mYsize = radius / aspect_ratio;

                kodachi_moonray::light_util::getSpotLightSlopes(params, mSlope, mSlope2, mInnerSlope);

                mZsize = std::min(1 / mSlope, radius * 2); // truncate long cones
                mZsize = std::max(mZsize, 2.0f); // make tiny lenses into tubes

            } else if (shaderName == "RectLight") {
                a1 = params.getChildByName("width");
                a2 = params.getChildByName("height");
                mType = RECT;
                mXsize = a1.getValue(1.0f, false) / 2;
                mYsize = a2.getValue(1.0f, false) / 2;
                mZsize = (mXsize + mYsize) / 4;

            } else if (shaderName == "CylinderLight") {
                a1 = params.getChildByName("radius");
                a2 = params.getChildByName("height");
                mType = CYLINDER;
                mXsize = a1.getValue(1.0f, false);
                mYsize = mXsize;
                mZsize = a2.getValue(1.0f, false) / 2;

            } else if (shaderName == "DiskLight") {
                a1 = params.getChildByName("radius");
                mType = DISK;
                mXsize = mYsize = mZsize = a1.getValue(1.0f, false);

            } else if (shaderName == "SphereLight") {
                a1 = params.getChildByName("radius");
                mType = SPHERE;
                mXsize = mYsize = mZsize = a1.getValue(1.0f, false);

            } else if (shaderName == "DistantLight") {
                //a1 = params.getChildByName("angular_extent"); default=0.53
                mType = DISTANT;
                mXsize = mYsize = 0.2f;
                mZsize = 1;

            } else if (shaderName == "EnvLight") {
                mType = ENV;
                mXsize = mYsize = mZsize = 1.0f;

            } else if (shaderName == "MeshLight") {
                mType = MESH;
                mXsize = mYsize = mZsize = 0.5f;
                if (not mesh) mesh = new MeshLightImpl;
                mesh->setup(root);

            } else {
                // Use point light if we can't figure out anything
                mType = POINT;
                mXsize = mYsize = mZsize = 0.5f;
            }
        }
    }
}

LightDrawable::~LightDrawable() {
    delete mesh;
}

void
LightDrawable::draw()
{
    Drawable::draw();

    // Draw light depending on light type
    switch (mType) {
    case POINT:
        drawPointLight();
        break;
    case SPHERE:
        drawSphereLight();
        break;
    case CYLINDER:
        drawCylinderLight();
        break;
    case SPOT:
        drawSpotLight();
        break;
    case RECT:
        drawRectLight();
        break;
    case DISK:
        drawDiskLight();
        break;
    case DISTANT:
        drawDistantLight();
        break;
    case ENV:
        drawEnvLight();
        break;
    case MESH:
        mesh->draw();
        break;
    };
}

float
LightDrawable::getScale() const
{
    float maxSize = std::max(fabsf(mXsize), std::max(fabsf(mYsize), fabsf(mZsize)));
    return std::max(scaleFactor * 3 / maxSize, 1.0f);
}

// draw an N-segment "circle" around z axis, based on roundness and xysize, s = scale
void
LightDrawable::drawLightCircle(float z, float s) const
{
    drawCircle(mXsize*s, mYsize*s, z, 1);
}

// draw 6 lines connecting the circle(z0,s0) to circle(z1,s1)
void
LightDrawable::drawLightCircleConnectingLines(float z0, float z1, float s0, float s1) const
{
    const float r1 = mXsize;
    const float r2 = mYsize;
    const int N = 6;
    glBegin(GL_LINES);
    for (int i = 0; i < N; ++i) {
        float a = i * (2 * M_PI / N);
        float x = r1 * sinf(a);
        float y = r2 * cosf(a);
        glVertex3f(x * s0, y * s0, z0);
        glVertex3f(x * s1, y * s1, z1);
    }
    glEnd();
}

// This used to be an arrow, but was simplified to a single line segment
// tail is at xyz and far end is len along the -z axis
// draw an arrow pointing at -z with tail at x,y,z
void
LightDrawable::drawArrow(float len, float x, float y, float z) const
{
    static VAO vao;
    if (not vao.isReady()) {
        std::vector<Vec3f> vertices;
        std::vector<unsigned> indices;
        vertices.emplace_back(0,0,0);
        vertices.emplace_back(0,0,-1);
        indices.emplace_back(0);
        indices.emplace_back(1);
#if 0
        const float ax = 0.125;
        const float az = 0.75f;
        static const int N = 6;
        for (int i = 0; i < N; ++i) {
            const float a = i * (2 * M_PI / N);
            indices.emplace_back(1);
            indices.emplace_back(vertices.size());
            vertices.emplace_back(ax*sinf(a), ax*cosf(a), -az);
        }
#endif
        vao.setup(vertices, indices);
    }
    glPushMatrix();
    glTranslatef(x, y, z);
    glScalef(len, len, len);
    vao.drawLines();
    glPopMatrix();
}

// draw line connecting light to it's centerOfInterest
// returns true if it should
bool
LightDrawable::drawCenterOfInterest() const
{
    if (showFrustum()) {
        setFrustumColorAndLineWidth();
        glBegin(GL_LINES);
        glVertex3f(0,0,0);
        glVertex3f(0, 0, -mCenterOfInterest);
        glEnd();
        return true;
    } else {
        return false;
    }
}

void
LightDrawable::drawPointLight() const
{
    if (mLookThrough) return;
    glPushMatrix();
    float s = getScale();
    glScalef(mXsize*s, mYsize*s, mZsize*s);
    drawStar();
    glPopMatrix();
    drawCenterOfInterest();
}

void
LightDrawable::drawRectLight() const
{
    if (mLookThrough) return;
    float s = getScale();
    drawRect(mXsize*s, mYsize*s, 0);
    drawArrow(2*mZsize*s, 0, 0, 0);
    // draw crosshairs
    if (!mPicking) {
        glLineWidth(1);
    }
    glBegin(GL_LINES);
    glVertex3f(-mXsize*s, 0, 0);
    glVertex3f(+mXsize*s, 0, 0);
    glVertex3f(0, -mYsize*s, 0);
    glVertex3f(0, +mYsize*s, 0);
    glEnd();
    drawCenterOfInterest();
}

void
LightDrawable::drawDiskLight() const
{
    if (mLookThrough) return;
    float s = getScale();
    drawLightCircle(0, s);
    drawArrow(mZsize*s, 0, 0, 0);
    // draw crosshairs
    if (!mPicking) {
        glLineWidth(1);
    }
    glBegin(GL_LINES);
    glVertex3f(-mXsize*s, 0, 0);
    glVertex3f(+mXsize*s, 0, 0);
    glVertex3f(0, -mYsize*s, 0);
    glVertex3f(0, +mYsize*s, 0);
    glEnd();
    drawCenterOfInterest();
}

void
LightDrawable::drawSpotCircles(float z) const
{
    // outer circle:
    const float rOuter = z < mFocalPlane ? 1 + z * mSlope : z * mSlope2 - 1;
    drawLightCircle(-z, rOuter);
    // inner circle, mInnerSlope / mSlope is size of circle at lens
    float rInner = mInnerSlope / mSlope + z * mInnerSlope;
    // reduce by lens defocusing
    rInner *= 1 - 2 * fabsf(1 - z / mFocalPlane) / rOuter;
    if (rInner > 0) drawLightCircle(-z, rInner);
}

void
LightDrawable::drawSpotLight() const
{
    if (mLookThrough) {
        // Need to draw the circle at some interesting distance, as the camera is not
        // at the focus of the light.
        glDisable(GL_DEPTH_TEST); // this only works for Hydra viewer
        drawSpotCircles(mCenterOfInterest);
        glEnable(GL_DEPTH_TEST);
        return;
    }

    float s = getScale();

    // the lens shape, with a minimum size so it doesn't turn into nothingness
    // the connecting lines will still use the real radius
    static const float kMinCircleSize = 1.0f/32.0f;
    drawCircle(s*std::max(mXsize, kMinCircleSize), s*std::max(mYsize, kMinCircleSize), 0, 1);

    // draw the other end
    float s1 = 1 - mZsize * mSlope;
    if (s1 > 0.01f) {
        drawLightCircle(s*mZsize, s*s1); // top of fez
    } else if (s1 < 0) {
        s1 = 0; // move point out to produce a more tubular shape
    }
    // connect with conical lines
    drawLightCircleConnectingLines(0, s*mZsize, s*1, s*s1);

    if (drawCenterOfInterest()) {

        float s1 = 1;
        float z = 0;
        if (mFocalPlane > 0) {
            // draw circle at center of interest
            drawSpotCircles(mCenterOfInterest);
            // draw circle at focal plane if visible
            if (mFocalPlane < mCenterOfInterest) drawSpotCircles(mFocalPlane);
            // draw cone from light to focal plane
            z = std::min(mFocalPlane, mCenterOfInterest);
            s1 = 1 + z * mSlope;
            drawLightCircleConnectingLines(0, -z, 1, s1);
        }

        // draw penumbra cone from focal plane to distance
        if (mFocalPlane < mCenterOfInterest) {
            const float s2 = mCenterOfInterest * mSlope2 - 1;
            drawLightCircleConnectingLines(-z, -mCenterOfInterest, s1, s2);
        }
    }
}

void
LightDrawable::drawCylinderLight() const
{
    if (mLookThrough) return;

    // deal with the axis being along y rather than z by rotating so the
    // same drawing code can be reused.
    glPushMatrix();
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    float s = getScale();
    drawCylinder(mXsize*s, mYsize*s, mZsize*s);
    glPopMatrix();
    drawCenterOfInterest();
}

void
LightDrawable::drawDistantLight() const
{
    if (mLookThrough) return;

    float s = getScale();
    drawLightCircle(0, s);
    drawArrow(s*mZsize,  0, 0, 0);
    drawArrow(s*mZsize,  s*mXsize/2, 0, 0);
    drawArrow(s*mZsize, -s*mXsize/2, 0, 0);
    drawCenterOfInterest();
}

void
LightDrawable::drawSphereLight() const
{
    if (mLookThrough) return;

    float s = getScale();
    // draw longitude lines
    glPushMatrix();
    static const int N = 6;
    for (int i = 0; i < N/2; ++i) {
        drawLightCircle(0, s);
        glRotatef(360.0/N, 0.0f, 1.0f, 0.0f);
    }
    glPopMatrix();
    // draw latitude lines
    glPushMatrix();
    glRotatef(90.0, 1.0f, 0.0f, 0.0f);
    static const int M = 4; // number of spaces, it draws M-1 rings
    for (int i = 1; i < M; ++i) {
        const float a = i * (M_PI / M);
        drawLightCircle(s * mZsize * cosf(a), s * sinf(a));
    }
    glPopMatrix();
    drawCenterOfInterest();
}

// this draws a dome, which imho seems better
void
LightDrawable::drawEnvLight() const
{
    glPushMatrix();
    float s = getScale();
    // draw bottom edge
    glRotatef(90.0f, 1.0f, 0.0f, 0.0f);
    drawCircle128(s*mXsize, s*mYsize, 0, 1);
    // and one middle line
    const float t = s * M_SQRT2/2;
    drawCircle128(mXsize*t, mYsize*t, -mZsize*t);
    // longitude lines
    glRotatef(90.0f, 0.0f, 1.0f, 0.0f);
    static const int N = 6;
    for (int i = 0; i < N/2; ++i) {
        drawCircle128(s, s, 0.0f, .5f);
        glRotatef(360.0f/N, 1.0f, 0.0f, 0.0f);
    }
    glPopMatrix();
}

// Return bounding box around light. This is used both for clipping (extent=true) and
// for framing (extent=false). Old viewer does not distinguish between these, and also
// does not call this when selection or look-through changes so it is sometimes wrong.
void
LightDrawable::getBBox(double bounds[6], bool extent) const
{
    if (mType == MESH) {
        DoubleAttribute globalBounds(mesh->getBounds());
        if (globalBounds.isValid()) {
            auto&& array(globalBounds.getNearestSample(0.0));
            memcpy(bounds, &array[0], 6*sizeof(double));
            return;
        }
    }
    bounds[0] = -mXsize;
    bounds[1] =  mXsize;
    bounds[2] = -mYsize;
    bounds[3] =  mYsize;
    bounds[4] = -mZsize;
    bounds[5] =  mZsize;
    switch (mType) {
    case CYLINDER:
        // deal with the axis being along y rather than z
        std::swap(bounds[2], bounds[4]);
        std::swap(bounds[3], bounds[5]);
        break;
    case ENV:
        bounds[2] = 0; // only half dome
        break;
    default:
        break;
    }
    if (extent && showFrustum() && mType != ENV && !mLookThrough)
        bounds[4] = -mCenterOfInterest;
}

DoubleAttribute
LightDrawable::getBounds() const
{
    if (mType == MESH)
        return mesh->getBounds();
    double bounds[6];
    getBBox(bounds, false);
    return DoubleAttribute(bounds, 6, 2);
}

DoubleAttribute
LightDrawable::getExtent() const
{
    if (mType == MESH)
        return mesh->getBounds();
    double bounds[6];
    getBBox(bounds, true);
    return DoubleAttribute(bounds, 6, 2);
}

}

