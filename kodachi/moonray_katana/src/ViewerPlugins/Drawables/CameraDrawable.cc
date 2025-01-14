// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "CameraDrawable.h"

#include <FnViewer/utils/FnImathHelpers.h>

#include <GL/gl.h>

namespace FnKatView = FnKat::ViewerAPI;

namespace // anonymous
{

constexpr float kReelRadius = 0.16f;
constexpr float kReelWidth  = 0.0384879f;
constexpr std::size_t kReelVertexCount    = 36;
constexpr GLfloat kFrontReelCenter[3] = { 0.0f, 0.34484216366544295784f, -0.065f };

const GLfloat kCameraPoints[] {
    -0.12829292822512414207f, -0.16449490026472496362f, 0.33061468055945064659f, // P1
    0.12829292822512450289f, -0.16449490026472496362f, 0.33061468055945048006f, // P2
    0.12829292822512425309f, -0.16449490026472496362f, -0.13296185655022860206f, // P3
    -0.12829292822512439187f, -0.16449490026472496362f, -0.13296185655022846328f, // P4
    -0.12829292822512414207f, 0.16449490026472482485f, 0.33061468055945064659f, // P5
    0.12829292822512453065f, 0.16449490026472479709f, 0.33061468055945048006f, // P6
    0.12829292822512425309f, 0.16449490026472482485f, -0.13296185655022860206f, // P7
    -0.12829292822512439187f, 0.16449490026472482485f, -0.13296185655022846328f, // P8
    -0.05233928644786718654f, -0.05233928644786695755f, -0.13296185655022835226f, // P9
    0.05233928644786679796f, -0.05233928644786695755f, -0.13296185655022835226f, // P10
    0.05233928644786679796f, -0.05233928644786695755f, -0.31241083865720137291f, // P11
    -0.05233928644786718654f, -0.05233928644786695755f, -0.31241083865720137291f, // P12
    -0.05233928644786718654f, 0.05233928644786708245f, -0.13296185655022835226f, // P13
    0.05233928644786679796f, 0.05233928644786708245f, -0.13296185655022835226f, // P14
    0.05233928644786679796f, 0.05233928644786708245f, -0.31241083865720137291f, // P15
    -0.05233928644786718654f, 0.05233928644786708245f, -0.31241083865720137291f, // P16
    -0.05233928644786722817f, 0.05233928644786706164f, -0.32055101789302681281f, // P17
    0.05233928644786679796f, 0.05233928644786706164f, -0.32055101789302681281f, // P18
    0.09738311938480052887f, 0.09738311938480100072f, -0.50000000000000011102f, // P19
    -0.09738311938480112562f, 0.09738311938480086194f, -0.49999999999999994449f, // P20
    -0.05233928644786724205f, -0.05233928644786695755f, -0.32055101789302681281f, // P21
    0.05233928644786679796f, -0.05233928644786695755f, -0.32055101789302681281f, // P22
    0.09738311938480048724f, -0.09738311938480068153f, -0.50000000000000011102f, // P23
    -0.09738311938480112562f, -0.09738311938480068153f, -0.49999999999999994449f // P24
};
constexpr unsigned kCameraVertexCount = sizeof(kCameraPoints)/(3*sizeof(*kCameraPoints));

// GL_LINES
const GLuint kCameraIndexArray[] {
    // Main body
    0, 4, 0, 1, 0, 3,
    5, 4, 5, 6, 5, 1,
    7, 4, 7, 6, 7, 3,
    2, 3, 2, 6, 2, 1,

    // Box connecting lens to body
     8,  9,  8, 12,  8, 11,
    13, 12, 13, 14, 13,  9,
    15, 12, 15, 11, 15, 14,
    10, 11, 10, 14, 10, 9,

    // Lens
    20, 16, 20, 23, 20, 21,
    17, 16, 17, 21, 17, 18,
    19, 16, 19, 18, 19, 23,
    22, 18, 22, 23, 22, 21
};
constexpr unsigned kCameraIdxSize = sizeof(kCameraIndexArray)/sizeof(*kCameraIndexArray);

} // namespace anonymous

namespace MoonrayKatana
{

CameraDrawable::CameraDrawable(const std::string& location)
    : Drawable(location)
{
    mColor[0] = 0;
    mColor[1] = 0;
    mColor[2] = 1;
}

void
CameraDrawable::buildCamera()
{
    std::vector<Vec3f> vertices;
    std::vector<unsigned int> indices;

    vertices.reserve(kCameraVertexCount + 4*kReelVertexCount);
    indices.reserve(kCameraIdxSize + 8*kReelVertexCount + 4*(kReelVertexCount/2));

    // copy the camera body model
    for (size_t i = 0; i < kCameraVertexCount; ++i)
        vertices.emplace_back(kCameraPoints[i*3], kCameraPoints[i*3+1], kCameraPoints[i*3+2]);

    for (size_t i = 0; i < kCameraIdxSize; ++i)
        indices.emplace_back(kCameraIndexArray[i]);

    // Make 2 cylinders that are reels
    for (unsigned reel = 0; reel < 2; ++reel) {
        Vec3f center(0.0f, 0.34484216366544295784f, -0.065f);
        if (reel) center.z += 2.1f * kReelRadius;
        unsigned p0 = vertices.size();
        for (unsigned i = 0; i < kReelVertexCount; i++) {
            float a = (2*M_PI) * i / kReelVertexCount;
            float y = sinf(a) * kReelRadius;
            float z = cosf(a) * kReelRadius;
            vertices.emplace_back(center + Vec3f(kReelWidth, y, z));
            vertices.emplace_back(center + Vec3f(-kReelWidth, y, z));
            indices.emplace_back(p0 + 2*i);
            indices.emplace_back(i ? p0+2*i-2 : p0+2*kReelVertexCount-2);
            indices.emplace_back(p0 + 2*i + 1);
            indices.emplace_back(i ? p0+2*i-1 : p0+2*kReelVertexCount-1);
            if (not (i%4)) {
                indices.emplace_back(p0 + 2*i);
                indices.emplace_back(p0 + 2*i + 1);
            }
        }
    }

    mCameraMesh.setup(vertices, indices);
}

CameraDrawable::~CameraDrawable()
{ }

void
CameraDrawable::setup(const FnAttribute::GroupAttribute& root)
{
    Drawable::setup(root);

    const FnAttribute::GroupAttribute geometryAttr = root.getChildByName("geometry");
    if (!geometryAttr.isValid())
        return;

    const FnKat::DoubleAttribute nearAttr = geometryAttr.getChildByName("near");
    const float near = nearAttr.getValue();
    const FnKat::DoubleAttribute farAttr = geometryAttr.getChildByName("far");
    const float far = farAttr.getValue();
    const FnKat::DoubleAttribute leftAttr = geometryAttr.getChildByName("left");
    const float left = leftAttr.getValue();
    const FnKat::DoubleAttribute rightAttr = geometryAttr.getChildByName("right");
    const float right = rightAttr.getValue();
    const FnKat::DoubleAttribute bottomAttr = geometryAttr.getChildByName("bottom");
    const float bottom = bottomAttr.getValue();
    const FnKat::DoubleAttribute topAttr = geometryAttr.getChildByName("top");
    const float top = topAttr.getValue();

    float slope, scale;
    const FnKat::StringAttribute projectionAttr = geometryAttr.getChildByName("projection");
    bool ortho = projectionAttr.getValueCStr()[0] == 'o';
    if (ortho) {
        const FnKat::DoubleAttribute orthoWidthAttr =
            geometryAttr.getChildByName("orthographicWidth");
        slope = 0;
        scale = orthoWidthAttr.getValue() / fabsf(right - left);
    } else {
        const FnKat::DoubleAttribute fovAttr    = geometryAttr.getChildByName("fov");
        const float fov          = fovAttr.getValue() * M_PI / 180;
        slope = tanf(fov / 2);
        scale = 1;
    }

    const FnKat::DoubleAttribute coiAttr = geometryAttr.getChildByName("centerOfInterest");
    if (coiAttr.isValid()) {
        mHasCenterOfInterest = true;
        mCenterOfInterest = static_cast<float>(coiAttr.getValue());
    } else {
        mHasCenterOfInterest = false;
        mCenterOfInterest = far;
    }

    if (not ortho) scale = near*slope;
    mFrustumVertices[0] = Vec3f(scale*left, scale*bottom, -near);
    mFrustumVertices[1] = Vec3f(scale*right, scale*bottom, -near);
    mFrustumVertices[2] = Vec3f(scale*right, scale*top, -near);
    mFrustumVertices[3] = Vec3f(scale*left, scale*top, -near);

    const float d = mCenterOfInterest;
    if (not ortho) scale = d*slope;
    mFrustumVertices[4] = Vec3f(scale*left, scale*bottom, -d);
    mFrustumVertices[5] = Vec3f(scale*right, scale*bottom, -d);
    mFrustumVertices[6] = Vec3f(scale*right, scale*top, -d);
    mFrustumVertices[7] = Vec3f(scale*left, scale*top, -d);

    updateVertices = true;
}

void
CameraDrawable::getBBox(double bounds[6]) const
{
    bounds[0] = -1.0;
    bounds[1] =  1.0;
    bounds[2] = -1.0;
    bounds[3] =  1.0;
    bounds[4] = -1.0;
    bounds[5] =  1.0;
}

void
CameraDrawable::draw()
{
    if (mLookThrough) return;
    if (not mCameraMesh.isReady())
        buildCamera();

    Drawable::draw();
    unsigned char lighting; glGetBooleanv(GL_LIGHTING, &lighting);
    glDisable(GL_LIGHTING);

    glPushMatrix();

    glScalef(scaleFactor*24, scaleFactor*24, scaleFactor*24);

    mCameraMesh.drawLines();
    glPopMatrix();

    // Draw frustum and center of interest only when
    // camera is selected.
    if (showFrustum()) {
        if (not mFrustumMesh.isReady()) {
            std::vector<unsigned int> indices;
            indices.resize(2*12);
            for (unsigned i = 0; i < 4; ++i) {
                unsigned j = i ? i-1 : 3;
                indices[2*i] = i; // near square edge
                indices[2*i+1] = j;
                indices[2*(4+i)] = 4+i; // far square edge
                indices[2*(4+i)+1] = 4+j;
                indices[2*(8+i)] = i; // connect near and far
                indices[2*(8+i)+1] = 4+i;
            }
            mFrustumMesh.setup(mFrustumVertices, indices);
            updateVertices = false;
        } else if (updateVertices) {
            mFrustumMesh.updateVertices(mFrustumVertices);
            updateVertices = false;
        }
        setFrustumColorAndLineWidth();
        mFrustumMesh.drawLines();
        if (mHasCenterOfInterest) {
            glBegin(GL_LINES);
            glVertex3f(0.0f, 0.0f, 0.0f);
            glVertex3f(0.0f, 0.0f, -mCenterOfInterest);
            glEnd();
        }
    }
    if (lighting) glEnable(GL_LIGHTING);
}

} // namespace KatanaPlugins

