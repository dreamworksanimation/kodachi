// Intel Corporation and DreamWorks Animation LLC Confidential Information.
// (c) 2025 Intel Corporation and DreamWorks Animation LLC.  All Rights Reserved.
// Reproduction in whole or in part without prior written permission of a
// duly authorized representative is prohibited.


#pragma once

#include <kodachi/attribute/AttributeUtils.h>

#include <embree3/rtcore.h>

#include <rendering/geom/Types.h>

#include <tbb/concurrent_unordered_map.h>

#include <atomic>

// Imath
#include <OpenEXR/ImathVec.h>

namespace kodachi_moonray {
namespace embree_util {

class EmbreeScene
{
public:
    using Vec3fa = arras::math::Vec3fa;

    EmbreeScene(enum RTCSceneFlags flags = RTC_SCENE_FLAG_NONE);
    ~EmbreeScene();

    // adds a geometry to the scene
    // interpolates all attrs and xform to provided sample times
    // transforms geometry points by the provided xform
    // if geometry already exists, does nothing
    // returns the geometry Id of the geometry
    uint32_t addGeometry(const kodachi::GroupAttribute& geometryAttr,
                        const kodachi::GroupAttribute& geometryXform,
                        const std::vector<float>& sampleTimes);

    struct __attribute__((aligned(16))) Ray {
        // required attributes:
        // these are attributes used by embree
        float org_x;        // x coordinate of ray origin
        float org_y;        // y coordinate of ray origin
        float org_z;        // z coordinate of ray origin
        float tnear;        // start of ray segment

        float dir_x;        // x coordinate of ray direction
        float dir_y;        // y coordinate of ray direction
        float dir_z;        // z coordinate of ray direction

        float time;         // time of this ray for motion blur
        float tfar;         // end of ray segment (set to hit distance)

        uint32_t mask;      // ray mask
        uint32_t id;        // ray ID
        uint32_t flags;     // ray flags

        uint32_t primID;    // primitive ID
        uint32_t geomID;    // geometry ID
        uint32_t instID;    // instance ID

        float Ng_x;         // x coordinate of geometry normal
        float Ng_y;         // y coordinate of geometry normal
        float Ng_z;         // z coordinate of geometry normal

        float u;            // barycentric u coordinate of hit
        float v;            // barycentric v coordinate of hit

        // ray dir should be normalized
        Ray (const Imath::V3f& rayOrigin = Imath::V3f(),
             const Imath::V3f& rayDir = Imath::V3f(),
             const float rayLength = 1.0f,
             const float iTime = 0.0f,
             const uint32_t iMask = RTC_INVALID_GEOMETRY_ID,
             const uint32_t iId = 0,
             const uint32_t iFlags = 0,
             const uint32_t iPrimId = RTC_INVALID_GEOMETRY_ID,
             const uint32_t iGeomId = RTC_INVALID_GEOMETRY_ID,
             const uint32_t iInstId = RTC_INVALID_GEOMETRY_ID)
        : org_x(rayOrigin.x)
        , org_y(rayOrigin.y)
        , org_z(rayOrigin.z)
        , tnear(0.0f)
        , dir_x(rayDir.x)
        , dir_y(rayDir.y)
        , dir_z(rayDir.z)
        , time(iTime)
        , tfar(rayLength)
        , mask(iMask)
        , id(iId)
        , flags(iFlags)
        , primID(iPrimId)
        , geomID(iGeomId)
        , instID(iInstId)
        , Ng_x(-1.0f)
        , Ng_y(-1.0f)
        , Ng_z(-1.0f)
        , u(-1.0f)
        , v(-1.0f)
        { }
    };

    // commit the scene readying it for ray queries
    // *** triggers BVH build ***
    void commit() {
        rtcCommitScene(mRtcScene);
        mSceneCommitted = true;
    }

    // Queries-----------------------------------------------------

    bool isOccluded(Ray& ray);
    uint32_t intersect(Ray& ray);
private:

    EmbreeScene(const EmbreeScene&) = delete;
    EmbreeScene& operator=(const EmbreeScene&) = delete;

    static void initDevice();

    uint32_t addTriangleMesh(const kodachi::FloatAttribute& meshPointsAttr,
                            const kodachi::IntAttribute& vertexListAttr,
                            const kodachi::IntAttribute& startIndexAttr,
                            const size_t numTimeSteps);
    uint32_t addQuadMesh(const kodachi::FloatAttribute& meshPointsAttr,
                        const kodachi::IntAttribute& vertexListAttr,
                        const kodachi::IntAttribute& startIndexAttr,
                        const size_t numTimeSteps);

    // scene must be committed if it was updated with any geometry
    // uncommitted scene results in undefined behavior for ray
    // queries
    std::atomic<bool> mSceneCommitted;

    // stores already created geometry
    // TODO: we should store altered mesh point attrs here
    // to utilize rtcSetSharedGeometryBuffer
    tbb::concurrent_unordered_map<uint64_t, uint32_t> mGeometryMap;
    tbb::concurrent_unordered_map<uint64_t, std::vector<uint32_t>> mIndicesMap;

    static RTCDevice mRtcDevice;
    RTCScene mRtcScene = nullptr;
};

} // namespace embree_util
} // namespace kodachi_moonray

// (c) 2025 Intel Corporation and DreamWorks Animation LLC.  All Rights Reserved.
// Reproduction in whole or in part without prior written permission of a
// duly authorized representative is prohibited.
