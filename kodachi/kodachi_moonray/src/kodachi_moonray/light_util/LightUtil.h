// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/attribute/Attribute.h>

namespace kodachi_moonray {
namespace light_util {

bool isNetworkMaterial(const kodachi::GroupAttribute& material);
std::string getShaderParamsPath(const FnAttribute::GroupAttribute& material,
                                const std::string& terminal = "moonrayLight");
kodachi::GroupAttribute getShaderParams(const kodachi::GroupAttribute& material,
                                        const std::string& terminal = "moonrayLight");
kodachi::GroupAttribute getShaderConns(const kodachi::GroupAttribute& material,
                                       const std::string& terminal = "moonrayLight");
std::string getShaderName(const kodachi::GroupAttribute& material,
                          const std::string& terminal = "moonrayLight");

void getSpotLightSlopes(const kodachi::GroupAttribute& params,
        float& outerSlope1, float& outerSlope2, float& innerSlope);

// Enough information about a barn door so that geometry can be constructed
// and manipulation done without any info other than xform of parent light
// Constructor assumes light and light filter DAPS have been cooked, so
// some additional error checking will need to be done if that is no longer
// the case in the future.
struct BarnDoor {
    BarnDoor(const kodachi::GroupAttribute& lightParams,
             const kodachi::GroupAttribute& lightFilterParams,
             float forcedDistance = -1.0f);
    float radiusX, radiusY; // size at the light
    float distance; // z value of corners
    float outerRadiusX, outerRadiusY; // scale for corners due to distance
    float topLeft[2];
    float topRight[2];
    float bottomLeft[2];
    float bottomRight[2];
    // fill buffers with resulting geometry
    // outVertices must be initialized to 24 floats (8 points),
    // while outIndices requires 16 ints (4 quads).
    void populateBuffers(float* outVertices, int* outIndices) const;
};

// back-compatability:
inline void
populateBarnDoorBuffers(const kodachi::GroupAttribute& lightParams,
                        const kodachi::GroupAttribute& lightFilterParams,
                        float* outVertices, int* outIndices, float forcedDistance = -1.0f)
{
    BarnDoor(lightParams, lightFilterParams, forcedDistance).populateBuffers(
        outVertices, outIndices);
}

}
}

