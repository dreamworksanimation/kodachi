// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi_moonray/light_util/LightUtil.h>

#include <array>
#include <math.h>

namespace kodachi_moonray {
namespace light_util {

using namespace kodachi;

    bool
    isNetworkMaterial(const GroupAttribute& material)
    {
        if (!material.isValid()) return false;
        const StringAttribute style = material.getChildByName("style");
        return style.isValid() && style == "network";
    }

    std::string
    getShaderParamsPath(const GroupAttribute& material, const std::string& terminal)
    {
        if (material.isValid()) {
            if (isNetworkMaterial(material)) {
                const StringAttribute nodeName = material.getChildByName("terminals." + terminal);
                if (nodeName.isValid()) {
                    return "nodes." + nodeName.getValue() + ".parameters";
                }
            } else {
                return terminal + "Params";
            }
        }

        return std::string();
    }

    GroupAttribute
    getShaderParams(const GroupAttribute& material, const std::string& terminal)
    {
        if (material.isValid()) {
            const std::string path = getShaderParamsPath(material, terminal);
            if (!path.empty()) {
                return material.getChildByName(path);
            }
        }

        return GroupAttribute();
    }

    GroupAttribute
    getShaderConns(const GroupAttribute& material, const std::string& terminal)
    {
        if (isNetworkMaterial(material)) {
            const StringAttribute nodeName = material.getChildByName("terminals." + terminal);
            if (nodeName.isValid()) {
                return material.getChildByName("nodes." + nodeName.getValue() + ".connections");
            }
        }

        return GroupAttribute();
    }

    std::string
    getShaderName(const GroupAttribute& material, const std::string& terminal)
    {
        if (material.isValid()) {
            if (isNetworkMaterial(material)) {
                const StringAttribute nodeName = material.getChildByName("terminals." + terminal);
                if (nodeName.isValid()) {
                    return StringAttribute(material.getChildByName(
                            "nodes." + nodeName.getValue() + ".type")).getValue("", false);
                }
            } else {
                return StringAttribute(material.getChildByName(
                        terminal + "Shader")).getValue("", false);
            }
        }

        return "";
    }

    void
    getSpotLightSlopes(const GroupAttribute& params,
            float& outerSlope1, float& outerSlope2, float& innerSlope)
    {
        if (params.isValid()) {
            // moonray angle is converted from degrees and also divided by 2
            const float angle = FloatAttribute(
                    params.getChildByName("outer_cone_angle")).getValue(60.0f, false) * (M_PI_2 / 180);
            const float innerAngle = FloatAttribute(
                    params.getChildByName("inner_cone_angle")).getValue(30.0f, false) * (M_PI_2 / 180);
            const float radius = FloatAttribute(
                    params.getChildByName("lens_radius")).getValue(1.0f, false);
            const float focalPlane = FloatAttribute(
                    params.getChildByName("focal_plane_distance")).getValue(10000.0f, false);

            static const float kBigValue = 1e7f;

            if (angle <= 0) {
                outerSlope1 = 0; // this is what moonray does with negative angles
            } else if (angle >= 1.570796f) {
                outerSlope1 = kBigValue; // avoid math errors and match moonray for huge values
            } else {
                outerSlope1 = tanf(angle) / radius;
            }

            if (innerAngle <= 0) {
                innerSlope = 0;
            } else if (innerAngle >= 1.570796f) {
                innerSlope = kBigValue;
            } else {
                innerSlope = tanf(innerAngle) / radius;
            }
            if (innerSlope > outerSlope1) innerSlope = outerSlope1;

            outerSlope2 = 2 / focalPlane + outerSlope1;
            if (focalPlane <= 0 || outerSlope2 > kBigValue) outerSlope2 = kBigValue;
        }
    }

    // Helper function for BarnDoor constructor
    void getVecWithDefault(const GroupAttribute& attr, const std::string& child, float* out)
    {
        auto vec = FloatAttribute(attr.getChildByName(child)).getNearestSample(0);
        if (vec.size() == 2) {
            out[0] = vec[0];
            out[1] = vec[1];
        } else {
            out[0] = 0;
            out[1] = 0;
        }
    }

    // Read necessary information from light and BarnDoorLightFilter
    BarnDoor::BarnDoor(const GroupAttribute& lightParams,
                       const GroupAttribute& lightFilterParams,
                       float forcedDistance)
    {
        getVecWithDefault(lightFilterParams, "top_left", topLeft);
        getVecWithDefault(lightFilterParams, "top_right", topRight);
        getVecWithDefault(lightFilterParams, "bottom_left", bottomLeft);
        getVecWithDefault(lightFilterParams, "bottom_right", bottomRight);
        distance = FloatAttribute(
            lightFilterParams.getChildByName("distance_from_light")).getValue(0.05f, false);
        if (forcedDistance >= 0)
            distance = forcedDistance;
        else if (distance < 0.05f)
            distance = 0.05f;

        radiusX = radiusY = 1;
        float outerScale = 1;

        FloatAttribute radiusAttr = lightParams.getChildByName("lens_radius");
        if (!radiusAttr.isValid())
            radiusAttr = lightParams.getChildByName("radius");
        if (radiusAttr.isValid()) {
            radiusX = radiusAttr.getValue(1.0f, false);
            FloatAttribute aspectRatioAttr = lightParams.getChildByName("aspect_ratio");
            radiusY = radiusX * aspectRatioAttr.getValue(1.0f, false);
            if (lightParams.getChildByName("outer_cone_angle").isValid()) {
                float outerSlope1 = 1.0f, outerSlope2 = 1.0f, innerSlope = 1.0f;
                getSpotLightSlopes(lightParams, outerSlope1, outerSlope2, innerSlope);
                outerScale = 1 + distance * outerSlope1;
            }
        } else {
            radiusAttr = lightParams.getChildByName("width");
            if (radiusAttr.isValid()) {
                // rect lights
                radiusX = FloatAttribute(radiusAttr).getValue(1.0f, false) / 2;
                FloatAttribute widthAttribute(lightParams.getChildByName("height"));
                radiusY = widthAttribute.getValue(1.0f, 2 * radiusX) / 2;
                // for back-compatability with barn doors, the far end is
                // exactly twice the size of the light for non-spotlight.
                outerScale = 2;
            }
        }
        outerRadiusX = outerScale * radiusX;
        outerRadiusY = outerScale * radiusY;
    }

    // Calculate the vertices for barn door geometry
    void
    BarnDoor::populateBuffers(float* outVertices, int* outIndices) const
    {
        // Initialize all z values. The near edge is put slightly behind the
        // light to prevent light bleeding through the door. A better solution
        // would be nice.
        for (int i = 0; i < 8; ++i) {
            if (i < 4) {
                outVertices[i*3+2] = radiusX * 0.05;
            } else {
                outVertices[i*3+2] = -distance;
            }
        }

        // Outer rectangle for blocker, extended a little bit to prevent
        // any edge bleeding.
        static const float kBlockerExtension = 0.5f;
        const float extendedRadiusX = radiusX * 1.05f;
        const float extendedRadiusY = radiusY * 1.05f;
        outVertices[0] = extendedRadiusX; // tr
        outVertices[1] = extendedRadiusY;
        outVertices[3] = -extendedRadiusX; // tl
        outVertices[4] = extendedRadiusY;
        outVertices[6] = -extendedRadiusX; // bl
        outVertices[7] = -extendedRadiusY;
        outVertices[9] = extendedRadiusX; // br
        outVertices[10] = -extendedRadiusY;

        // Inner quad that light can pass through
        outVertices[12] = outerRadiusX * (1.0f - topRight[0]); // tr
        outVertices[13] = outerRadiusY * (1.0f - topRight[1]);
        outVertices[15] = -outerRadiusX * (1.0f - topLeft[0]); // tl
        outVertices[16] = outerRadiusY * (1.0f - topLeft[1]);
        outVertices[18] = -outerRadiusX * (1.0f - bottomLeft[0]); // bl
        outVertices[19] = -outerRadiusY * (1.0f - bottomLeft[1]);
        outVertices[21] = outerRadiusX * (1.0f - bottomRight[0]); // br
        outVertices[22] = -outerRadiusY * (1.0f - bottomRight[1]);

        outIndices[0] = 0; // right
        outIndices[1] = 4;
        outIndices[2] = 7;
        outIndices[3] = 3;

        outIndices[4] = 0; // top
        outIndices[5] = 1;
        outIndices[6] = 5;
        outIndices[7] = 4;

        outIndices[8] = 1; // left
        outIndices[9] = 2;
        outIndices[10] = 6;
        outIndices[11] = 5;

        outIndices[12] = 2; // bottom
        outIndices[13] = 3;
        outIndices[14] = 7;
        outIndices[15] = 6;
    }
}
}

