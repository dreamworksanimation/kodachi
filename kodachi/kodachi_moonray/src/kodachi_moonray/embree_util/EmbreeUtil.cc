// Intel Corporation and DreamWorks Animation LLC Confidential Information.
// (c) 2025 Intel Corporation and DreamWorks Animation LLC.  All Rights Reserved.
// Reproduction in whole or in part without prior written permission of a
// duly authorized representative is prohibited.


#include <kodachi_moonray/embree_util/EmbreeUtil.h>

#include <kodachi_moonray/kodachi_geometry/GenerateUtil.h>

#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <string>

namespace {

// assumes points and xform has the same time samples
inline kodachi::FloatAttribute
transformPoints(const kodachi::FloatAttribute& points,
                const kodachi::DoubleAttribute& xform)
{
    const auto pointSamples = points.getSamples();

    std::vector<float> out;
    out.reserve(pointSamples.getNumberOfValues() *
            pointSamples.getNumberOfTimeSamples());

    std::vector<float> sampleTimes;
    sampleTimes.reserve(pointSamples.getNumberOfTimeSamples());

    for (const auto& sample : pointSamples) {

        sampleTimes.emplace_back(sample.getSampleTime());

        const auto& xformSample =
                xform.getNearestSample(sample.getSampleTime());

        Imath::M44d mat;
        kodachi_moonray::setXformMatrix(mat, xformSample.data());

        for (size_t i = 0; i < sample.size(); i += 3) {
            Imath::V3f pt(sample[i], sample[i+1], sample[i+2]);
            pt = pt * mat;
            out.insert(out.end(), { pt.x, pt.y, pt.z });
        }
    }
    return kodachi::ZeroCopyFloatAttribute::create(sampleTimes, out, 3);
}

} // anonymous namespace

namespace kodachi_moonray {
namespace embree_util {

KdLogSetup("EmbreeUtil");

std::atomic<bool> sRtcDeviceInit(false);

RTCDevice EmbreeScene::mRtcDevice = nullptr;

// error reporting function
void
rtcErrorHandler(void* userPtr, const RTCError code, const char* str)
{
  if (code == RTC_ERROR_NONE) {
    return;
  }

  KdLogDebug("Embree: ");
  switch (code) {
  case RTC_ERROR_UNKNOWN          : KdLogDebug("RTC_ERROR_UNKNOWN"); break;
  case RTC_ERROR_INVALID_ARGUMENT : KdLogDebug("RTC_ERROR_INVALID_ARGUMENT"); break;
  case RTC_ERROR_INVALID_OPERATION: KdLogDebug("RTC_ERROR_INVALID_OPERATION"); break;
  case RTC_ERROR_OUT_OF_MEMORY    : KdLogDebug("RTC_ERROR_OUT_OF_MEMORY"); break;
  case RTC_ERROR_UNSUPPORTED_CPU  : KdLogDebug("RTC_ERROR_UNSUPPORTED_CPU"); break;
  case RTC_ERROR_CANCELLED        : KdLogDebug("RTC_ERROR_CANCELLED"); break;
  default                         : KdLogDebug("invalid error code"); break;
  }
  if (str) {
      KdLogDebug(str);
  }
}

// in general one device is created per application
/*
 * When creating the device, Embree reads configurations for the device
 *  from the following locations in order:
 *  1) config string passed to the rtcNewDevice function
 *  2) .embree3 file in the application folder
 *  3) .embree3 file in the home folder
 */
void
EmbreeScene::initDevice()
{
    const std::string cfg("threads=0"
                          ",verbose=0");
    mRtcDevice = rtcNewDevice(cfg.c_str());
    /* set error handler */
    rtcSetDeviceErrorFunction(mRtcDevice, rtcErrorHandler, nullptr);
}

EmbreeScene::EmbreeScene(enum RTCSceneFlags flags)
    : mSceneCommitted(false)
{
    // init device if we haven't already done so
    if (!sRtcDeviceInit.exchange(true)) {
        // creates a new device, ref counted at 1
        initDevice();
    }

    // increments the reference to the device
    rtcRetainDevice(mRtcDevice);

    // create the scene
    mRtcScene = rtcNewScene(mRtcDevice);
    rtcSetSceneFlags(mRtcScene, flags);
}

EmbreeScene::~EmbreeScene()
{
    // decrements ref count to this scene
    // all attached geometries will in turn be detached
    // and have their ref counts decremented
    rtcReleaseScene (mRtcScene);
    mRtcScene = nullptr;

    // decrements the reference to the device
    rtcReleaseDevice(mRtcDevice);
}

uint32_t
EmbreeScene::addTriangleMesh(
        const kodachi::FloatAttribute& meshPointsAttr,
        const kodachi::IntAttribute& vertexListAttr,
        const kodachi::IntAttribute& startIndexAttr,
        const size_t numTimeSteps)
{
    // prevent duplicate geometry from being created and added
    const kodachi::GroupAttribute hashGroup("type", kodachi::StringAttribute("tri"),
                                            "point.P", meshPointsAttr,
                                            "poly.vertexList", vertexListAttr,
                                            "poly.startIndex", startIndexAttr,
                                            false);
    const uint64_t hash = hashGroup.getHash().uint64();
    const auto it = mGeometryMap.find(hash);
    if (it != mGeometryMap.end()) {
        KdLogDebug("Embree addGeometry Error: geometry already exists.");
        return it->second;
    }

    const auto pointSamples = meshPointsAttr.getSamples();

    RTCGeometry mesh = rtcNewGeometry(mRtcDevice, RTC_GEOMETRY_TYPE_TRIANGLE);
    rtcSetGeometryTimeStepCount(mesh, numTimeSteps);

    // points
    for (size_t t = 0; t < numTimeSteps; ++t)
    {
        const auto& points = pointSamples[t];
        const size_t numVerts = pointSamples.getNumberOfValues() / 3;

        // need to use new buffer here because we created a new attribute
        // with interpolated and transformed points
        Vec3fa* buffer = (Vec3fa*)rtcSetNewGeometryBuffer(
                mesh, RTC_BUFFER_TYPE_VERTEX, t,
                RTC_FORMAT_FLOAT3, sizeof(Vec3fa),
                numVerts);

        for (size_t i = 0; i < numVerts; ++i) {
            size_t idx = i*3;
            buffer[i] = Vec3fa(points[idx], points[idx+1], points[idx+2], 1);
        }
    }

    // vertex information
    const auto startIndexSamples = startIndexAttr.getSamples();
    const auto& startIndices     = startIndexSamples.front();
    const auto vertexListSamples = vertexListAttr.getSamples();
    const auto& vertexList       = vertexListSamples.front();

    // need to update the indices list for any non-tri faces
    std::vector<uint32_t>& newIndexList = mIndicesMap[hash];
    newIndexList.reserve((startIndices.size() - 1)*3);

    const auto vertexIt = vertexList.begin();
    auto currIt = vertexIt;

    for (size_t i = 1; i < startIndices.size(); ++i) {
        const int32_t nVerts = startIndices[i] - startIndices[i-1];

        if (nVerts != 4 && nVerts != 3) {
            KdLogDebug("Embree addTriMesh Warning: "
                       "Unsupported face type encountered, skipping.");
            continue;
        }

        currIt = vertexIt + startIndices[i-1];

        //  0 -- 1
        //  | \  |
        //  |  \ |
        //  3 -- 2
        // insert verts 0, 1, 2
        newIndexList.insert(newIndexList.end(),
                            currIt, currIt + 3);

        // if we encounter a quad, we can split it into two tri's
        if (nVerts == 4) {
            // insert verts 2, 3, 0
            newIndexList.emplace_back(*(currIt + 2));
            newIndexList.emplace_back(*(currIt + 3));
            newIndexList.emplace_back(*(currIt));
        }
    }

    rtcSetSharedGeometryBuffer(mesh, RTC_BUFFER_TYPE_INDEX, 0,
            RTC_FORMAT_UINT3,
            (void*)newIndexList.data(),
            0,
            sizeof(uint32_t) * 3,
            newIndexList.size() / 3);

    rtcCommitGeometry(mesh);
    // we can potentially attach geometry by id based on
    // location hash, etc
    uint32_t geomId = rtcAttachGeometry(mRtcScene, mesh);

    // release this mesh since it is attached to the scene now
    // which will reference it
    rtcReleaseGeometry(mesh);

    mGeometryMap[hash] = geomId;

    // dirty the scene; another commit is required
    // before we perform queries
    mSceneCommitted = false;

    return geomId;
}

uint32_t
EmbreeScene::addQuadMesh(
        const kodachi::FloatAttribute& meshPointsAttr,
        const kodachi::IntAttribute& vertexListAttr,
        const kodachi::IntAttribute& startIndexAttr,
        const size_t numTimeSteps)
{
    // prevent duplicate geometry from being created and added
    const kodachi::GroupAttribute hashGroup("type", kodachi::StringAttribute("quad"),
                                            "point.P", meshPointsAttr,
                                            "poly.vertexList", vertexListAttr,
                                            "poly.startIndex", startIndexAttr,
                                            false);
    const uint64_t hash = hashGroup.getHash().uint64();
    const auto it = mGeometryMap.find(hash);
    if (it != mGeometryMap.end()) {
        KdLogDebug("Embree addGeometry Error: geometry already exists.");
        return it->second;
    }

    const auto pointSamples = meshPointsAttr.getSamples();

    RTCGeometry mesh = rtcNewGeometry(mRtcDevice, RTC_GEOMETRY_TYPE_QUAD);
    rtcSetGeometryTimeStepCount(mesh, numTimeSteps);

    // points
    for (size_t t = 0; t < numTimeSteps; ++t)
    {
        const auto& points = pointSamples[t];
        const size_t numVerts = pointSamples.getNumberOfValues() / 3;

        // need to use new buffer here because we created a new attribute
        // with interpolated and transformed points
        Vec3fa* buffer = (Vec3fa*)rtcSetNewGeometryBuffer(
                mesh, RTC_BUFFER_TYPE_VERTEX, t,
                RTC_FORMAT_FLOAT3, sizeof(Vec3fa),
                numVerts);

        for (size_t i = 0; i < numVerts; ++i) {
            size_t idx = i*3;
            buffer[i] = Vec3fa(points[idx], points[idx+1], points[idx+2], 1);
        }
    }

    // vertex information
    const auto startIndexSamples = startIndexAttr.getSamples();
    const auto& startIndices     = startIndexSamples.front();
    const auto vertexListSamples = vertexListAttr.getSamples();
    const auto& vertexList       = vertexListSamples.front();

    // need to update the indices list for any non-quad faces
    std::vector<uint32_t>& newIndexList = mIndicesMap[hash];
    newIndexList.reserve((startIndices.size() - 1)*4);

    const auto vertexIt = vertexList.begin();
    auto currIt = vertexIt;

    for (size_t i = 1; i < startIndices.size(); ++i) {
        const int32_t nVerts = startIndices[i] - startIndices[i-1];

        if (nVerts != 4 && nVerts != 3) {
            KdLogDebug("Embree addQuadMesh Warning: "
                       "Unsupported face type encountered, skipping.");
            continue;
        }

        currIt = vertexIt + startIndices[i-1];

        newIndexList.insert(newIndexList.end(),
                currIt, currIt + nVerts);

        // if we encounter a tri in a quad mesh, we'll duplicate the first vertex
        if (nVerts == 3) {
            newIndexList.emplace_back(*currIt);
        }
    }

    rtcSetSharedGeometryBuffer(mesh, RTC_BUFFER_TYPE_INDEX, 0,
            RTC_FORMAT_UINT4,
            (void*)newIndexList.data(),
            0,
            sizeof(uint32_t) * 4,
            newIndexList.size() / 4);

    rtcCommitGeometry(mesh);
    // we can potentially attach geometry by id based on
    // location hash, etc
    uint32_t geomId = rtcAttachGeometry(mRtcScene, mesh);

    // release this mesh since it is attached to the scene now
    // which will reference it
    rtcReleaseGeometry(mesh);

    mGeometryMap[hash] = geomId;

    // dirty the scene; another commit is required
    // before we perform queries
    mSceneCommitted = false;

    return geomId;
}

uint32_t
EmbreeScene::addGeometry(const kodachi::GroupAttribute& geometryAttr,
                         const kodachi::GroupAttribute& geometryXform,
                         const std::vector<float>& sampleTimes)
{
    // geometry of the mesh
    const kodachi::IntAttribute vertexListAttr =
            geometryAttr.getChildByName("poly.vertexList");
    const kodachi::IntAttribute startIndexAttr =
            geometryAttr.getChildByName("poly.startIndex");

    kodachi::FloatAttribute meshPointsAttr =
            geometryAttr.getChildByName("point.P");

    if (!startIndexAttr.getNumberOfValues() > 1 ||
            !vertexListAttr.getNumberOfValues() > 0 ||
            !meshPointsAttr.getNumberOfValues() > 0) {
        KdLogDebug("Embree addGeometry Error: invalid geometry.");
        return RTC_INVALID_GEOMETRY_ID;
    }

    // interpolate attrs to provided sample times
    // and transform it by the provided xform
    meshPointsAttr = kodachi::interpToSamples(meshPointsAttr, sampleTimes, 3);
    const kodachi::DoubleAttribute xformAttr =
            kodachi::XFormUtil::CalcTransformMatrixAtTimes(geometryXform,
                    sampleTimes.data(), sampleTimes.size()).first;
    meshPointsAttr = transformPoints(meshPointsAttr, xformAttr);

    const auto vertexList =
            vertexListAttr.getNearestSample(0.0f);
    const auto startIndices =
            startIndexAttr.getNearestSample(0.0f);

    // determine if the mesh is tri or quad by counting each type
    size_t triCount = 0;
    size_t quadCount = 0;
    for (size_t i = 1; i < startIndices.size(); ++i) {
        const int32_t nV = startIndices[i] - startIndices[i-1];
        if (nV == 3) {
            triCount++;
        } else if (nV == 4) {
            quadCount++;
        }
    }

    if (triCount == 0 && quadCount == 0) {
        KdLogDebug("Embree addGeometry Error: Unsupported geometry type.");
        return RTC_INVALID_GEOMETRY_ID;
    }

    if (triCount > quadCount) {
        return addTriangleMesh(meshPointsAttr, vertexListAttr,
                startIndexAttr, sampleTimes.size());
    } else {
        return addQuadMesh(meshPointsAttr, vertexListAttr,
                startIndexAttr, sampleTimes.size());
    }
}

bool
EmbreeScene::isOccluded(Ray& ray)
{
    // make sure the scene is committed if geometry were added
    // if not, the query is undefined
    if (!mSceneCommitted) {
        KdLogError("Embree: attempting to perform query with uncommitted scene.");
        return false;
    }

    RTCIntersectContext context;
    rtcInitIntersectContext(&context);
    rtcOccluded1(mRtcScene, &context, (RTCRay*)&ray);

    return ray.tfar < 0.0f;
}

uint32_t
EmbreeScene::intersect(Ray& ray)
{
    // make sure the scene is committed if geometry were added
    // if not, the query is undefined
    if (!mSceneCommitted) {
        KdLogError("Embree: attempting to perform query with uncommitted scene.");
        return false;
    }

    RTCIntersectContext context;
    rtcInitIntersectContext(&context);
    rtcIntersect1(mRtcScene, &context, (RTCRayHit*)&ray);

    return ray.geomID;
}

} // namespace embree_util
} // namespace kodachi_moonray


// (c) 2025 Intel Corporation and DreamWorks Animation LLC.  All Rights Reserved.
// Reproduction in whole or in part without prior written permission of a
// duly authorized representative is prohibited.
