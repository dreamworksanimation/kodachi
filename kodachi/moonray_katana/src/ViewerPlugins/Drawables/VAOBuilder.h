// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "VAO.h"

// Taken from Foundry example code. Just some small
// helper functions to create meshes for VAO.
// Fixme: this should be updated with a new Builder object that can put multiple
// pieces of geometry into a single VAO.

namespace MoonrayKatana
{

class VAOBuilder {
public:
    static void generateCylinder(
        const Vec3f& origin, float base, float top, float height, VAO& mesh);

    // Note: This mesh will only work with GL_LINES.
    static void generateCircle(const Vec3f& origin, float radius, int N, VAO& mesh);

    static void generateSquare(const Vec3f& origin, float length, VAO& mesh);

    static void generateCube(const Vec3f& origin, float length, VAO& mesh);

    static void generateTorus(
        const Vec3f& origin, float centerRadius, float tubeRadius, VAO& mesh);

    static void generateTriangle(
        const Vec3f& origin, float cathetusLength, float thickness, VAO& mesh);
};

}

