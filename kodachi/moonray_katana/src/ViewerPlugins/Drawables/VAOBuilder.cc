// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "VAOBuilder.h"

#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathMath.h>

namespace MoonrayKatana
{

void VAOBuilder::generateCylinder(
    const Vec3f& origin, float base, float top, float height, VAO& mesh)
{
    std::vector<Vec3f> vertices;
    std::vector<Vec3f> normals;
    std::vector<unsigned int> indices;

    const int slices = 16;
    const float angleDelta = (2.f * M_PI) / static_cast<float>(slices);

    bool invert = top > base;

    // side faces
    for (int i = 0; i < slices; ++i)
    {
        float theta = angleDelta * i;
        float x = std::cos(theta);
        float y = std::sin(theta);

        vertices.push_back(Vec3f(x * base, y * base, 0.f) + origin);
        normals.push_back(Vec3f(x, y, 0.f));

        vertices.push_back(Vec3f(x * top, y * top, height) + origin);
        normals.push_back(Vec3f(x, y, 0.f));

        int idx = i * 2;
        int next = ((i + 1) == slices) ? 0 : (idx + 2);

        indices.push_back(idx);
        indices.push_back(next + 1);
        indices.push_back(idx + 1);

        indices.push_back(next);
        indices.push_back(next + 1);
        indices.push_back(idx);
    }

    // base central vertex
    int baseIdx = vertices.size();
    vertices.push_back(Vec3f(0.f, 0.f, invert ? height : 0.f) + origin);

    Vec3f baseNormal(0.f, 0.f, invert ? 1.f : -1.f);
    normals.push_back(baseNormal);

    // base faces
    int startIdx = vertices.size();

    const float baseRadius = invert ? top : base;

    for (int i = 0; i < slices; ++i)
    {
        float theta = angleDelta * i;
        float x = std::cos(theta);
        float y = std::sin(theta);

        vertices.push_back(Vec3f(x * baseRadius, y * baseRadius, invert ? height : 0.f) + origin);
        normals.push_back(baseNormal);

        int idx = i + startIdx;
        int next = (i + 1) == slices ? startIdx : (idx + 1);

        indices.push_back(idx);
        indices.push_back(baseIdx);
        indices.push_back(next);
    }

    // setup drawable mesh
    mesh.setup(vertices, normals, indices);
}

void VAOBuilder::generateCircle(const Vec3f& origin, float radius, int N, VAO& mesh)
{
    std::vector<Vec3f> vertices, normals;
    std::vector<unsigned int> indices;
    for (int i = 0; i < N; ++i) {
        const float angle = i * (2 * M_PI / N);
        vertices.push_back(Vec3f(radius * sinf(angle), radius * cosf(angle), 0.f) + origin);
        normals.push_back(Vec3f(0.0f, 0.0f, 1.0f));
        indices.push_back(i);
        indices.push_back((i+1)%N);
    }

    // setup drawable mesh
    mesh.setup(vertices, normals, indices);
}

void VAOBuilder::generateSquare(const Vec3f& origin, float length, VAO& mesh)
{
    const float half = length * 0.5f;

    std::vector<Vec3f> vertices;
    vertices.push_back(Vec3f(-half, half, 0.f) + origin);
    vertices.push_back(Vec3f(-half, -half, 0.f) + origin);
    vertices.push_back(Vec3f(half, -half, 0.f) + origin);
    vertices.push_back(Vec3f(half, half, 0.f) + origin);

    std::vector<Vec3f> normals;
    normals.push_back(Vec3f(0.f, 0.f, 1.f));
    normals.push_back(Vec3f(0.f, 0.f, 1.f));
    normals.push_back(Vec3f(0.f, 0.f, 1.f));
    normals.push_back(Vec3f(0.f, 0.f, 1.f));

    std::vector<unsigned int> indices;
    indices.push_back(0);
    indices.push_back(3);
    indices.push_back(1);
    indices.push_back(1);
    indices.push_back(3);
    indices.push_back(2);

    // setup drawable mesh
    mesh.setup(vertices, normals, indices);
}

void VAOBuilder::generateCube(const Vec3f& origin, float length, VAO& mesh)
{
    std::vector<Vec3f> vertices;
    std::vector<Vec3f> normals;
    std::vector<unsigned int> indices;

    const float half = length * 0.5f;

    const Vec3f quad[4] = {
        Vec3f(-half, -half, half),
        Vec3f(half, -half, half),
        Vec3f(half, half, half),
        Vec3f(-half, half, half)};

    const Vec3f normal = Vec3f(0.f, 0.f, 1.f);

    // side faces
    for (int i = 0; i < 16; ++i)
    {
        const int idx = i % 4;
        const int face = static_cast<int>(i / 4);
        const float angle = (M_PI / 2) * face;
        const float cosAngle = std::cos(angle);
        const float sinAngle = std::sin(angle);

        Vec3f vertex = Vec3f(
            quad[idx].z * sinAngle + quad[idx].x * cosAngle,
            quad[idx].y,
            quad[idx].z * cosAngle - quad[idx].x * sinAngle);
        vertices.push_back(vertex + origin);

        normals.push_back(Vec3f(
            normal.z * sinAngle + normal.x * cosAngle,
            normal.y,
            normal.z * cosAngle - normal.x * sinAngle));
    }

    // top and bottom faces
    for (int i = 0; i < 8; ++i)
    {
        const int idx = i % 4;
        const int face = static_cast<int>(i / 4);
        const float sign = face == 0 ? -1.f : 1.f;
        const float angle = (M_PI / 2) * sign;
        const float cosAngle = std::cos(angle);
        const float sinAngle = std::sin(angle);

        Vec3f vertex = Vec3f(
            quad[idx].x,
            quad[idx].y * cosAngle - quad[idx].z * sinAngle,
            quad[idx].y * sinAngle + quad[idx].z * cosAngle);
        vertices.push_back(vertex + origin);

        normals.push_back(Vec3f(
            normal.x,
            normal.y * cosAngle - normal.z * sinAngle,
            normal.y * sinAngle + normal.z * cosAngle));
    }

    // indices
    for (int i = 0; i < 6; ++i)
    {
        const int idx0 = i * 4;
        const int idx1 = idx0 + 1;
        const int idx2 = idx0 + 2;
        const int idx3 = idx0 + 3;

        indices.push_back(idx0);
        indices.push_back(idx1);
        indices.push_back(idx2);

        indices.push_back(idx2);
        indices.push_back(idx3);
        indices.push_back(idx0);
    }

    // setup drawable mesh
    mesh.setup(vertices, normals, indices);
}

void VAOBuilder::generateTorus(
    const Vec3f& origin, float centerRadius, float tubeRadius, VAO& mesh)
{
    std::vector<Vec3f> vertices;
    std::vector<Vec3f> normals;
    std::vector<unsigned int> indices;

    const int slices = 64;
    const int segments = 10;
    const float deltaU = (2.f * M_PI) / static_cast<float>(slices);
    const float deltaV = (2.f * M_PI) / static_cast<float>(segments);
    const int iterations = slices * segments;

    /* Torus parametric equations
        For u,v in [0,2Pi], c the radius from the center to the tube center,
        a the radius of the tube.
        x = (c + a * cos(v)) * cos(u)
        y = (c + a * cos(v)) * sin(u)
        z = a * sin(v)
    */

    for (int i = 0; i < iterations; ++i)
    {
        int slice = i / segments;
        int segment = i % segments;

        float theta = static_cast<float>(slice) * deltaU;
        float phi = static_cast<float>(segment) * deltaV;

        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        float cosPhi = std::cos(phi);
        float sinPhi = std::sin(phi);

        float x = (centerRadius + tubeRadius * cosPhi) * cosTheta;
        float y = (centerRadius + tubeRadius * cosPhi) * sinTheta;
        float z = tubeRadius * sinPhi;

        vertices.push_back(Vec3f(x, y, z) + origin);

        float nx = cosPhi * cosTheta;
        float ny = cosPhi * sinTheta;
        float nz = sinPhi;

        normals.push_back(Vec3f(nx, ny, nz));
    }

    for (int i = 0; i < iterations; ++i)
    {
        int slice = i / segments;
        int segment = i % segments;

        bool lastSlice = (slice + 1) == slices;
        bool lastSegment = (segment + 1) == segments;

        int nextSlice = lastSlice ? 0 : slice + 1;
        int nextSegment = lastSegment ? 0 : segment + 1;

        int idx0 = segment + (slice * segments);
        int idx1 = segment + (nextSlice * segments);
        int idx2 = nextSegment + (nextSlice * segments);
        int idx3 = nextSegment + (slice * segments);

        indices.push_back(idx0);
        indices.push_back(idx1);
        indices.push_back(idx3);
        indices.push_back(idx1);
        indices.push_back(idx2);
        indices.push_back(idx3);
    }

    // setup drawable mesh
    mesh.setup(vertices, normals, indices);
}

void VAOBuilder::generateTriangle(
    const Vec3f& origin, float cathetusLength, float thickness, VAO& mesh)
{
    const float a = cathetusLength * 0.5f;
    const float b = a - cathetusLength;
    const float c = thickness * 0.5f;

    std::vector<Vec3f> vertices(18);
    // Base
    vertices[0] = Vec3f(c, a, a);
    vertices[1] = Vec3f(c, b, a);
    vertices[2] = Vec3f(c, a, b);
    // Base
    vertices[3] = Vec3f(-c, a, a);
    vertices[4] = Vec3f(-c, b, a);
    vertices[5] = Vec3f(-c, a, b);
    // Top (Y)
    vertices[6] = vertices[0];
    vertices[7] = vertices[2];
    vertices[8] = vertices[5];
    vertices[9] = vertices[3];
    // Front (Z)
    vertices[10] = vertices[0];
    vertices[11] = vertices[3];
    vertices[12] = vertices[4];
    vertices[13] = vertices[1];
    // Diagonal (pointing to origin)
    vertices[14] = vertices[1];
    vertices[15] = vertices[4];
    vertices[16] = vertices[5];
    vertices[17] = vertices[2];

    std::vector<Vec3f> normals(18);
    // Base
    normals[0] =
    normals[1] =
    normals[2] = Vec3f(1.0f, 0.0f, 0.0f);
    // Base
    normals[3] =
    normals[4] =
    normals[5] = Vec3f(-1.0f, 0.0f, 0.0f);
    // Top (Y)
    normals[6] =
    normals[7] =
    normals[8] =
    normals[9] = Vec3f(0.0f, 1.0f, 0.0f);
    // Front (Z)
    normals[10] =
    normals[11] =
    normals[12] =
    normals[13] = Vec3f(0.0f, 0.0f, 1.0f);
    // Diagonal (pointing to origin)
    normals[14] =
    normals[15] =
    normals[16] =
#if defined(KATANA__3_0v1_alpha)
    normals[17] = Vec3f(0.0f, -1.0f, -1.0f).normalize();
#else
    normals[17] = Vec3f(0.0f, -1.0f, -1.0f).normalized();
#endif

    std::vector<unsigned int> indices(24);
    // Base
    indices[0] = 0;
    indices[1] = 1;
    indices[2] = 2;
    // Base
    indices[3] = 3;
    indices[4] = 5;
    indices[5] = 4;
    // Top (Y)
    indices[6] = 6;
    indices[7] = 7;
    indices[8] = 8;
    indices[9] = 8;
    indices[10] = 9;
    indices[11] = 6;
    // Front (Z)
    indices[12] = 10;
    indices[13] = 11;
    indices[14] = 12;
    indices[15] = 12;
    indices[16] = 13;
    indices[17] = 10;
    // Diagonal (pointing to origin)
    indices[18] = 14;
    indices[19] = 15;
    indices[20] = 16;
    indices[21] = 16;
    indices[22] = 17;
    indices[23] = 14;

    mesh.setup(vertices, normals, indices);
}

}  // namespace ViewerKatana

