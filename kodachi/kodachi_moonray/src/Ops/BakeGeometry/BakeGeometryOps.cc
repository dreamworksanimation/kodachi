// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/ArrayView.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>
#include <kodachi/cache/GroupAttributeCache.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <kodachi_moonray/moonray_util/MoonrayUtil.h>

#include <rendering/shading/PrimitiveAttribute.h>
#include <rendering/geom/PolygonMesh.h>
#include <rendering/rndr/RenderContext.h>
#include <rendering/rndr/RenderOptions.h>

#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/GeometrySet.h>
#include <scene_rdl2/scene/rdl2/Layer.h>
#include <scene_rdl2/scene/rdl2/LightSet.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>
#include <scene_rdl2/scene/rdl2/Utils.h>
#include <scene_rdl2/common/math/Xform.h>

#include <chrono>

#define TBB_PREVIEW_GLOBAL_CONTROL 1
#include <tbb/global_control.h>

namespace
{

KdLogSetup("BakeGeometryOps");

void
printGroup(const kodachi::GroupAttribute& g, int level = 1)
{
    for (const auto c : g) {
        std::cout << std::string(level, '-') << c.name;

        const kodachi::DataAttribute dc = c.attribute;
        if (dc.isValid()) {
            const auto size = dc.getNumberOfValues();
            std::cout << "     ( num values: " << size << ") ";
            if (size == 1) {
                const kodachi::IntAttribute ic = c.attribute;
                if (ic.isValid()) {
                    std::cout << " ---> " << ic.getValue();
                }

                const kodachi::FloatAttribute fc = c.attribute;
                if (fc.isValid()) {
                    std::cout << " ---> " << fc.getValue();
                }
            }
        }

        std::cout << "\n";

        const kodachi::GroupAttribute gc = c.attribute;
        if (gc.isValid()) {
            printGroup(gc, level + 3);
        }
    }
}

kodachi::StringAttribute
attributeRateToScope(arras::shading::AttributeRate rate)
{
    using namespace arras::shading;

    static const std::map<AttributeRate,
                 kodachi::StringAttribute> sRateMap {
        { AttributeRate::RATE_CONSTANT,     "primitive" },
        { AttributeRate::RATE_UNIFORM,      "face"      },
        { AttributeRate::RATE_VERTEX,       "point"     },
        { AttributeRate::RATE_VARYING,      "point"     },
        { AttributeRate::RATE_FACE_VARYING, "vertex"    }
    };

    if (sRateMap.find(rate) != sRateMap.end()) {
        return sRateMap.at(rate);
    } else {
        return {};
    }
}

kodachi::FloatAttribute
invertPointTransforms(const kodachi::FloatAttribute& inPointsAttr,
                      const arras::math::Xform3f& render2ObjXform,
                      bool transformNormals = false)
{
    const auto samples = inPointsAttr.getSamples();

    const auto sampleTimes = samples.getSampleTimes();
    const std::size_t numSamples = sampleTimes.size();

    std::vector<float> transformedData;
    transformedData.reserve(inPointsAttr.getNumberOfValues() * numSamples);

    for (const auto sample : samples) {
        float time = sample.getSampleTime();

        for (size_t i = 0; i < sample.size(); i += 3) {
            arras::math::Vec3f pt(sample[i], sample[i+1], sample[i+2]);

            arras::math::Vec3f pt2;
            if (transformNormals) {
                arras::math::Xform3f invXform = render2ObjXform.inverse();
                pt2 = arras::math::transformNormal(invXform, pt);
                pt2.normalize();
            } else {
                pt2 = arras::math::transformPoint(render2ObjXform, pt);
            }

            transformedData.push_back(pt2.x);
            transformedData.push_back(pt2.y);
            transformedData.push_back(pt2.z);
        }
    }

    return kodachi::ZeroCopyFloatAttribute::create(
                               sampleTimes, std::move(transformedData), 3);
}

// remap point scoped UV's back to vertex scope UV's
// given a point scoped inAttr and a list of vertex indices
kodachi::FloatAttribute
remapPointToVertexScopeAttr(const kodachi::FloatAttribute& inAttr,
                            const std::vector<int32_t>& indexList,
                            const size_t tupleSize = 1)
{
    const auto samples = inAttr.getSamples();

    const auto sampleTimes = samples.getSampleTimes();
    const std::size_t numSamples = sampleTimes.size();

    std::vector<float> remappedData;
    remappedData.reserve(indexList.size() * tupleSize * numSamples);

    for (const auto sample : samples) {
        for (const auto idx : indexList) {
            const auto iter = std::begin(sample) + (idx * tupleSize);
            remappedData.insert(remappedData.end(), iter, iter + tupleSize);
        }
    }

    return kodachi::ZeroCopyFloatAttribute::create(
                               sampleTimes, std::move(remappedData), tupleSize);
}

// remaps attributes with interleaved multiple time samples into
// consecutive data samples
// ie. v0t0, v0t1, v0t2, v1t0, v1t1, v1t2, ... --->
//     v0t0 v1t0 v2t0, v0t1, v1t1, v2t1, v0t2, v1t2, v2t2, ...
template <class ATTR_T, class RDL_T>
ATTR_T
remapMultiSampleAttr(const kodachi::array_view<RDL_T> data,
                     const kodachi::array_view<float> sampleTimes,
                     const size_t motionSampleCount,
                     const size_t dataSizePerSample,
                     const size_t tupleSize = 1)
{
    using value_t = typename ATTR_T::value_type;
    std::vector<value_t> outData;
    outData.reserve(data.size() * tupleSize);

    for (size_t t = 0; t < motionSampleCount; ++t) {
        for (size_t i = 0; i < dataSizePerSample; ++i) {

            const size_t inDataIdx = i*motionSampleCount + t;
            if (inDataIdx >= data.size()) {
                KdLogError("Indexing error mapping result attributes.");
                return {};
            }
            const RDL_T* val = data.data() + inDataIdx;
            // cast rdl types to raw values eg. vec3f -> 3 floats
            const value_t* rawVal = reinterpret_cast<const value_t*>(val);
            outData.insert(outData.end(), rawVal, rawVal + tupleSize);
        }
    }
    return kodachi::ZeroCopyAttribute<ATTR_T>::create(
                               sampleTimes, std::move(outData), tupleSize);
}

// string specialization
// tuple size always assumed to be 1 for strings
template <>
kodachi::StringAttribute
remapMultiSampleAttr(const kodachi::array_view<std::string> data,
                     const kodachi::array_view<float> sampleTimes,
                     const size_t motionSampleCount,
                     const size_t dataSizePerSample,
                     const size_t tupleSize)
{
    std::vector<const char*> outData;
    outData.reserve(data.size());

    for (size_t t = 0; t < motionSampleCount; ++t) {
        for (size_t i = 0; i < dataSizePerSample; ++i) {

            const size_t inDataIdx = i*motionSampleCount + t;
            if (inDataIdx >= data.size()) {
                KdLogError("Indexing error mapping result attributes.");
                return {};
            }
            outData.emplace_back(data[inDataIdx].data());
        }
    }

    std::vector<const char**> values(motionSampleCount);
    for (std::size_t i = 0; i < motionSampleCount; ++i) {
        values[i] = outData.data() + i * dataSizePerSample;
    }

    return kodachi::StringAttribute(sampleTimes.data(),
                                    sampleTimes.size(),
                                    values.data(),
                                    dataSizePerSample,
                                    1);
}

template <typename RDL_T, typename ATTR_T>
kodachi::DataAttribute
convertPrimitiveAttr(void* data,
                     const kodachi::array_view<float> sampleTimes,
                     const size_t motionSampleCount,
                     const size_t dataSizePerSample,
                     const size_t tupleSize = 1)
{
    RDL_T* castedData = reinterpret_cast<RDL_T*>(data);
    const size_t totalDataSize = dataSizePerSample * motionSampleCount;
    const kodachi::array_view<RDL_T> dataView(castedData, totalDataSize);
    return remapMultiSampleAttr<ATTR_T>(dataView,
                                        sampleTimes,
                                        motionSampleCount,
                                        dataSizePerSample,
                                        tupleSize);
}

kodachi::DataAttribute
extractPrimitiveAttr(kodachi::GroupBuilder& attrGb,
                     void* data,
                     arras::rdl2::AttributeType type,
                     const kodachi::array_view<float> sampleTimes,
                     const size_t motionSampleCount,
                     const size_t dataSizePerSample)
{
    using namespace arras::rdl2;

    static const std::string kInputType("inputType");
    static const std::string kElemSize ("elementSize");
    static const std::string kValue    ("value");

    // input types
    static const kodachi::StringAttribute kInt   ("int");
    static const kodachi::StringAttribute kFloat ("float");
    static const kodachi::StringAttribute kDouble("double");
    static const kodachi::StringAttribute kColor3("color3");
    static const kodachi::StringAttribute kColor4("color4");
    static const kodachi::StringAttribute kVec2  ("vector2");
    static const kodachi::StringAttribute kVec3  ("vector3");
    static const kodachi::StringAttribute kVec4  ("vector4");
    static const kodachi::StringAttribute kMat16 ("matrix16");

    switch (type) {
    case AttributeType::TYPE_BOOL: {
        attrGb.set(kInputType, kInt);
        attrGb.set(kElemSize, kodachi::IntAttribute(1));
        return convertPrimitiveAttr<bool, kodachi::IntAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample);
    }
    case AttributeType::TYPE_INT: {
        attrGb.set(kInputType, kInt);
        attrGb.set(kElemSize, kodachi::IntAttribute(1));
        return convertPrimitiveAttr<int32_t, kodachi::IntAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample);
    }
    case AttributeType::TYPE_LONG: {
        attrGb.set(kInputType, kInt);
        attrGb.set(kElemSize, kodachi::IntAttribute(1));
        return convertPrimitiveAttr<int64_t, kodachi::IntAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample);
    }
    case AttributeType::TYPE_FLOAT: {
        attrGb.set(kInputType, kFloat);
        attrGb.set(kElemSize, kodachi::IntAttribute(1));
        return convertPrimitiveAttr<float, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample);
    }
    case AttributeType::TYPE_DOUBLE: {
        attrGb.set(kInputType, kDouble);
        attrGb.set(kElemSize, kodachi::IntAttribute(1));
        return convertPrimitiveAttr<double, kodachi::DoubleAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample);
    }
    case AttributeType::TYPE_STRING: {
        attrGb.set(kInputType, kodachi::StringAttribute("string"));
        attrGb.set(kElemSize, kodachi::IntAttribute(1));
        return convertPrimitiveAttr<std::string, kodachi::StringAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample);
    }
    case AttributeType::TYPE_RGB: {
        attrGb.set(kInputType, kColor3);
        attrGb.set(kElemSize, kodachi::IntAttribute(3));
        return convertPrimitiveAttr<arras::math::Color, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 3);
    }
    case AttributeType::TYPE_RGBA: {
        attrGb.set(kInputType, kColor4);
        attrGb.set(kElemSize, kodachi::IntAttribute(4));
        return convertPrimitiveAttr<arras::math::Color4, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 4);
    }
    case AttributeType::TYPE_VEC2F: {
        attrGb.set(kInputType, kVec2);
        attrGb.set(kElemSize, kodachi::IntAttribute(2));
        return convertPrimitiveAttr<Vec2f, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 2);
    }
    case AttributeType::TYPE_VEC2D: {
        attrGb.set(kInputType, kDouble); // vector types default to float
        attrGb.set(kElemSize, kodachi::IntAttribute(2));
        return convertPrimitiveAttr<Vec2d, kodachi::DoubleAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 2);
    }
    case AttributeType::TYPE_VEC3F: {
        attrGb.set(kInputType, kVec3);
        attrGb.set(kElemSize, kodachi::IntAttribute(3));
        return convertPrimitiveAttr<Vec3f, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 3);
    }
    case AttributeType::TYPE_VEC3D: {
        attrGb.set(kInputType, kDouble); // vector types default to float
        attrGb.set(kElemSize, kodachi::IntAttribute(3));
        return convertPrimitiveAttr<Vec3d, kodachi::DoubleAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 3);
    }
    case AttributeType::TYPE_VEC4F: {
        attrGb.set(kInputType, kVec4);
        attrGb.set(kElemSize, kodachi::IntAttribute(4));
        return convertPrimitiveAttr<Vec4f, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 4);
    }
    case AttributeType::TYPE_VEC4D: {
        attrGb.set(kInputType, kDouble); // vector types default to float
        attrGb.set(kElemSize, kodachi::IntAttribute(4));
        return convertPrimitiveAttr<Vec4d, kodachi::DoubleAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 4);
    }
    case AttributeType::TYPE_MAT4F: {
        attrGb.set(kInputType, kMat16);
        attrGb.set(kElemSize, kodachi::IntAttribute(16));
        return convertPrimitiveAttr<Mat4f, kodachi::FloatAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 16);
    }
    case AttributeType::TYPE_MAT4D: {
        attrGb.set(kInputType, kDouble); // matrix types default to float
        attrGb.set(kElemSize, kodachi::IntAttribute(16));
        return convertPrimitiveAttr<Mat4d, kodachi::DoubleAttribute>(
                    data, sampleTimes, motionSampleCount, dataSizePerSample, 16);
    }
    default:
        KdLogDebug("Unexpected primitive attribute type; Skipping.");
        std::cout << "Unexpected primitive attribute type; Skipping." << "\n";
        break;
    }

    return {};
}

kodachi::DoubleAttribute
calculateBounds(const kodachi::FloatAttribute& pointsAttr)
{
    std::vector<double> bounds = {
            std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
            std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
            std::numeric_limits<double>::max(), std::numeric_limits<double>::min(),
    };

    const auto points = pointsAttr.getNearestSample(0.0f);
    for (size_t i = 0; i < points.size(); i += 3) {

        size_t x = i;
        // x min
        if (points[x] < bounds[0]) {
            bounds[0] = points[x];
        }
        // x max
        if (points[x] > bounds[1]) {
            bounds[1] = points[x];
        }
        // y min
        x++;
        if (points[x] < bounds[2]) {
            bounds[2] = points[x];
        }
        // y max
        if (points[x] > bounds[3]) {
            bounds[3] = points[x];
        }
        // z min
        x++;
        if (points[x] < bounds[4]) {
            bounds[4] = points[x];
        }
        // z max
        if (points[x] > bounds[5]) {
            bounds[5] = points[x];
        }
    }

    return kodachi::ZeroCopyDoubleAttribute::create(std::move(bounds), 2);
}

// ===================================================================
// BakeGeometry
// Roundtrip geometry through Moonray's geometry baking API
// This gives the ability to retrieve geometry with subdivision, displacements
// applied by Moonray.
// Basically you load up the RenderContext with objects you want to bake,
// then call context->bakeGeometry which fills in a struct of data
// BakedMesh:
/*
class BakedAttribute
{
public:
    BakedAttribute() : mData(nullptr) {}

    ~BakedAttribute()
    {
        if (mType == AttributeType::TYPE_STRING) {
            // Array of non-POD requires us to cast back so destructors are called
            delete[] reinterpret_cast<std::string*>(mData);
        } else {
            delete[] mData;
        }
    }

    std::string mName;
    size_t mTimeSampleCount;
    AttributeType mType;
    AttributeRate mRate;
    size_t mNumElements;
    char *mData;
};

class BakedMesh
{
public:
    const rdl2::Geometry* mRdlGeometry;

    // Will be 3 or 4
    int mVertsPerFace;

    // Size of index buffer is numFaces * mVertsPerFace.  Obtain the number of
    // faces by dividing.
    std::vector<unsigned int> mIndexBuffer;

    size_t mVertexCount;
    size_t mMotionSampleCount;

    // The vertex buffer always has the format v0_t0, v0_t1, v1_t0, v1_t1...
    // (if there are two motion samples t0 and t1.)
    std::vector<Vec3f> mVertexBuffer;

    // The layout of the attribute buffers depends on the attribute rate and
    // the number of motion samples.  The examples below assume two motion
    // samples t0 and t1.

    // For RATE_CONSTANT: The number of elements is the number of motion samples.
    // Format is c_t0, c_t1.

    // For RATE_UNIFORM: # elements = numFaces * mMotionSampleCount.
    // Format is f0_t0, f0_t1, f1_t0, f1_t1, f2_t0, f2_t1...

    // For RATE_VERTEX: #elements = mVertexCount * mMotionSampleCount
    // Format is v0_t0, v0_t1, v1_t0, v1_t1, v2_t0, v2_t1...

    // For RATE_VARYING: same as RATE_VERTEX after baking

    // For RATE_FACE_VARYING: #elements = numFaces * mVertsPerFace * mMotionSampleCount
    // E.g. for mVertsPerFace=3 and mMotionSampleCount=2:
    // Format is f0_v0_t0, f0_v0_t1, f0_v1_t0, f0_v1_t1, f0_v2_t0, f0_v2_t1,
    //           f1_v0_t0, f1_v0_t1, f1_v1_t0, f1_v1_t1, f1_v2_t0, f1_v2_t1...

    // For RATE_PART: #elements = numParts * mMotionSampleCount
    // Format is: p0_t0, p0_t1, p1_t0, p1_t1...

    // WARNING: Baked geometry vertices and normals are in RENDER SPACE.  You may want to
    // transform back to object space.  Be careful with transformNormal() as it uses
    // the inverse xform!

    // Mapping from tessellated face id to base face id.
    std::vector<int> mTessellatedToBaseFace;

    // Mapping of face to part
    std::vector<int> mFaceToPart;

    // Additional prim attrs
    std::vector<std::unique_ptr<BakedAttribute>> mAttrs;
};
 */
// ===================================================================
//
//kodachi::GroupAttribute
//createBakedGeometry(const kodachi::GroupAttribute& keyAttr, kodachi::GroupAttribute* iSupportAttrs)
//{
//    using namespace arras;
//
//    // *** ONE TIME RENDER CONTEXT PREREQUISITES ***
//    // init render options
//    // TODO: share this with MoonrayRenderBackend?
//    static std::unique_ptr<rndr::RenderOptions> sRenderOptions;
//    if (!sRenderOptions) {
//        static std::once_flag sInitRenderOptionsFlag;
//        std::call_once(sInitRenderOptionsFlag,
//          [&]() {
//            KdLogDebug("Initializing RenderOptions.");
//            sRenderOptions.reset(new rndr::RenderOptions);
//            // sRenderOptions.setThreads ?
//          }
//        );
//    }
//    // make sure global driver is initialized
//    {
//        KdLogDebug("Initializing Global Render Driver.");
//        kodachi_moonray::moonray_util::initGlobalRenderDriver(*sRenderOptions);
//    }
//
//    // *** CREATE THE RENDER CONTEXT ***
//    // TODO: evaluate performance of spooling a new render context each time
//    // and consider ways to share render context (or if multiple render contexts
//    // are possible)
//    // finally create the render context
//    std::unique_ptr<rndr::RenderContext> bakeGeometryContext(
//                                new rndr::RenderContext(*sRenderOptions));
//    std::unique_ptr<kodachi_moonray::MoonrayRenderState> moonrayRenderState(
//            new kodachi_moonray::MoonrayRenderState(kodachi::GroupAttribute{}));
//    moonrayRenderState->useExternalSceneContext(&bakeGeometryContext->getSceneContext());
//
//    // *** CREATE SCENE OBJECTS ***
//    // scene context
//    auto& sceneContext = bakeGeometryContext->getSceneContext();
//
//    // --- shutter attrs
//    const kodachi::GroupAttribute shutterAttrs =
//            keyAttr.getChildByName("shutter_attrs");
//
//    const kodachi::FloatAttribute shutterOpenAttr  = shutterAttrs.getChildByName("shutterOpen");
//    const kodachi::FloatAttribute shutterCloseAttr = shutterAttrs.getChildByName("shutterClose");
//    const kodachi::IntAttribute   mbEnabledAttr    = shutterAttrs.getChildByName("mbEnabled");
//
//    const float shutterOpen  = shutterOpenAttr.getValue();
//    const float shutterClose = shutterCloseAttr.getValue();
//    const bool  mbEnabled    = mbEnabledAttr.getValue();
//    KdLogDebug("Shutter Open: "  << shutterOpen);
//    KdLogDebug("Shutter Close: " << shutterClose);
//    KdLogDebug("MB Enabled: "    << mbEnabled);
//    std::cout << "Shutter Open: "  << shutterOpen << "\n";
//    std::cout << "Shutter Close: " << shutterClose << "\n";
//    std::cout << "MB Enabled: "    << mbEnabled << "\n";
//
//    // Interpolate and convert camera and geometry attributes to rdl2 attributes
//    // we want to use only one rdl geometry (which means only 2 motion samples) because
//    // we don't want the risk of geometry returning different topology at different time samples
//
//    // we don't have to check for existing scene objects since we're
//    // always starting with a new render context
//
//    static const kodachi::StringAttribute kRdl2Attr("rdl2");
//
//    // --- Scene Variables and Camera
//    {
//        // CAMERA
//        // object name
//        const kodachi::StringAttribute cameraPathAttr = keyAttr.getChildByName("camera_path");
//
//        const kodachi::GroupAttribute cameraAttrs = keyAttr.getChildByName("camera_attrs");
//
//        // rdl2 camera attributes
//        kodachi::GroupBuilder rdl2CameraGb;
//
//        // first populate with args
//        rdl2CameraGb.set("type", kRdl2Attr);
//        rdl2CameraGb.set("rdl2.meta.shutterOpen" , shutterOpenAttr);
//        rdl2CameraGb.set("rdl2.meta.shutterClose", shutterCloseAttr);
//        rdl2CameraGb.set("rdl2.meta.mbEnabled",    mbEnabledAttr);
//        rdl2CameraGb.set("geometry",               cameraAttrs);
//        rdl2CameraGb.set("args.objectName",        cameraPathAttr);
//        // TODO: do we need moonrayCameraStatements.mb_shutter_bias, dof_aperture, and dof_image_size?
//
//        // xform
//        const kodachi::GroupAttribute camXformAttr =
//                keyAttr.getChildByName("camera_xform");
//        if (camXformAttr.isValid()) {
//            rdl2CameraGb.set("xform", camXformAttr);
//        }
//        const kodachi::GroupAttribute attrFuncArgs =
//                rdl2CameraGb.build(kodachi::GroupBuilder::BuildAndRetain);
//
//        // KPOPNodeAttrFunc for xform
//        // currently contains: rdl2.sceneObject.attrs.node_xform
//        // TODO: currently setting node_xform seems to return transformed points,
//        // which may cause a double-xform being applied
//        // we may need to inverse trasform the returned points
//        if (camXformAttr.isValid()) {
//            rdl2CameraGb.deepUpdate(kodachi::GroupAttribute(
//                    kodachi::AttributeFunctionUtil::run("KPOPNodeAttrFunc", attrFuncArgs)));
//        }
//
//        // KPOPCameraAttrFunc for generic camera attrs
//        // currently contains: rdl2.sceneObject.attrs.node_xform
//        //                                           .<camera attrs>
//        const kodachi::GroupAttribute kpopCamerAttrs =
//                kodachi::AttributeFunctionUtil::run("KPOPCameraAttrFunc", attrFuncArgs);
//        rdl2CameraGb.deepUpdate(kpopCamerAttrs);
//
//        const kodachi::StringAttribute sceneClassAttr =
//                kpopCamerAttrs.getChildByName("rdl2.sceneObject.sceneClass");
//
//        // scene class specific conversions
//        // currently contains: rdl2.sceneObject.attrs.node_xform
//        //                                           .<camera attrs>
//        //                                           .<perspective or orthographic attrs>
//        if (sceneClassAttr == "OrthographicCamera") {
//            rdl2CameraGb.deepUpdate(kodachi::GroupAttribute(
//                    kodachi::AttributeFunctionUtil::run("KPOPOrthographicCameraAttrFunc", attrFuncArgs)));
//        } else if (sceneClassAttr == "PerspectiveCamera") {
//            rdl2CameraGb.deepUpdate(kodachi::GroupAttribute(
//                    kodachi::AttributeFunctionUtil::run("KPOPPerspectiveCameraAttrFunc", attrFuncArgs)));
//        } else {
//            KdLogError("Unsupported 'sceneClass' for camera: " <<
//                    sceneClassAttr.getValueCStr("(empty)", false));
//        }
//
//        // final camera conversion result
//        const kodachi::GroupAttribute rdl2FinalCameraAttrs = rdl2CameraGb.build();
//
//        // Debug
////        std::cout << "[BakeGeometry] Camera Conversion\n";
////        printGroup(rdl2FinalCameraAttrs);
//
//        // rdl2 conversion
//        // we only need processLocation to use: rdl2.meta.<shutter>
//        //                                          .sceneObject.sceneClass
//        //                                                      .name
//        //                                                      .attrs
//        // - create scene object
//        // - set scene object attrs
//        const auto processedSceneObj =
//                moonrayRenderState->processLocation(cameraPathAttr, rdl2FinalCameraAttrs);
//        if (!processedSceneObj) {
//            KdLogError("Failure processing scene object for camera: " <<
//                    cameraPathAttr.getValueCStr("(empty)", false));
//            return {};
//        }
//
//        rdl2::Camera* cam = processedSceneObj->asA<rdl2::Camera>();
//        std::cout << " -----[CREATED CAMERA] " << cam << "\n";
//        if (!cam) {
//            KdLogError("Scene object is not a camera: " <<
//                    cameraPathAttr.getValueCStr("(empty)", false));
//            return {};
//        }
//
//        // update camera for scene vars
//        rdl2::SceneVariables& sceneVars =
//                sceneContext.getSceneVariables();
//        sceneVars.beginUpdate();
//        sceneVars.set(rdl2::SceneVariables::sCamera, cam);
//        sceneVars.endUpdate();
//    }
//
//    // --- Geometry
//    kodachi::StringAttribute geometryName("geo");
//    if (iSupportAttrs) {
//        const kodachi::StringAttribute locationPath =
//                iSupportAttrs->getChildByName("input_location");
//        if (locationPath.isValid()) {
//            geometryName = locationPath;
//        }
//    } else {
//        KdLogDebug(" >>> Missing supporting attributes.");
//    }
//
//    // parts information
//    kodachi::GroupAttribute partsAttr = keyAttr.getChildByName("parts");
//    const int64_t numParts = partsAttr.getNumberOfChildren();
//    const bool usingParts = numParts > 0;
//
//    // --- Geometry
//    kodachi_moonray::MoonrayRenderState::SceneObjectPtr geometryObj;
//    const kodachi::GroupAttribute geometryAttrs = keyAttr.getChildByName("geometry_attrs");
//    {
//        // first create the layer, lightset, and geometry sets - default values are fine
//        rdl2::Layer *layer =
//                sceneContext.createSceneObject("Layer", "defaultLayer")->asA<rdl2::Layer>();
//        if (!layer) {
//            KdLogError("Failure creating default Layer.");
//            return {};
//        }
//
//        rdl2::LightSet *lightSet =
//                sceneContext.createSceneObject("LightSet", "defaultLightSet")->asA<rdl2::LightSet>();
//        if (!lightSet) {
//            KdLogError("Failure creating default LightSet.");
//            return {};
//        }
//
//        rdl2::GeometrySet *geometrySet =
//                sceneContext.createSceneObject("GeometrySet", "defaultGeometrySet")->asA<rdl2::GeometrySet>();
//        if (!geometrySet) {
//            KdLogError("Failure creating default GeometrySet.");
//            return {};
//        }
//
//        const kodachi::GroupAttribute moonrayMeshStatementAttr
//                                             = keyAttr.getChildByName("moonrayMeshStatements");
//
//        // rdl2 geometry attributes
//        kodachi::GroupBuilder rdl2GeometryGb;
//        rdl2GeometryGb.set("type", kRdl2Attr);
//        rdl2GeometryGb.set("rdl2.meta.shutterOpen" , shutterOpenAttr);
//        rdl2GeometryGb.set("rdl2.meta.shutterClose", shutterCloseAttr);
//        rdl2GeometryGb.set("rdl2.meta.mbEnabled",    mbEnabledAttr);
//        rdl2GeometryGb.set("rdl2.meta.kodachiType",  keyAttr.getChildByName("type"));
//        rdl2GeometryGb.set("geometry",               geometryAttrs);
//        rdl2GeometryGb.set("args.objectName",        geometryName);
//        rdl2GeometryGb.set("moonrayMeshStatements",  moonrayMeshStatementAttr);
//        rdl2GeometryGb.set("moonrayStatements",
//                              keyAttr.getChildByName("moonrayStatements"));
//
//        if (usingParts) {
//            rdl2GeometryGb.set("args.parts", partsAttr);
//        }
//
//        // TODO: may need KPOPGeometry for moonrayStatements?
//
//        // xform
//        const kodachi::GroupAttribute geoXformAttr =
//                keyAttr.getChildByName("geometry_xform");
//        if (geoXformAttr.isValid()) {
//            rdl2GeometryGb.set("xform", geoXformAttr);
//        }
//
////        std::cout << "[BakeGeometry] Geometry Conversion Args\n";
////        printGroup(rdl2GeometryGb.build(kodachi::GroupBuilder::BuildAndRetain));
//
//        // KPOPNodeAttrFunc for xform
//        // currently contains: rdl2.sceneObject.attrs.node_xform
//        // TODO: same issue as camera xform
//        // also, geometry itself may not have xform attribute, but could inherit from parent
//        // we may need to do some form of xform localization
//        if (geoXformAttr.isValid()) {
//            rdl2GeometryGb.deepUpdate(kodachi::GroupAttribute(
//                    kodachi::AttributeFunctionUtil::run("KPOPNodeAttrFunc",
//                            rdl2GeometryGb.build(kodachi::GroupBuilder::BuildAndRetain))));
//        }
//
//        // KPOPGeometry
//        // adds rdl2.sceneObject.attrs.<moonray statement geometry attrs>
//        rdl2GeometryGb.deepUpdate(kodachi::GroupAttribute(
//                kodachi::AttributeFunctionUtil::run("KPOPGeometryAttrFunc",
//                        rdl2GeometryGb.build(kodachi::GroupBuilder::BuildAndRetain))));
//
//        // KPOPMeshWindingOrder to reverse mesh winding orders (default is true)
//        rdl2GeometryGb.deepUpdate(kodachi::GroupAttribute(
//                kodachi::AttributeFunctionUtil::run("KPOPMeshWindingOrderAttrFunc",
//                        rdl2GeometryGb.build(kodachi::GroupBuilder::BuildAndRetain))));
//
//        // contains rdl2 attributes: rdl2.sceneObject.sceneClass
//        //                                           .attrs.<attributes>
//        // vertices_by_index
//        // vertex_list, vertex_list_mb
//        // face_vertex_count
//        // uv_list
//        // normal_list
//        rdl2GeometryGb.deepUpdate(kodachi::GroupAttribute(
//                kodachi::AttributeFunctionUtil::run("KPOPRdlMeshGeometryAttrFunc",
//                        rdl2GeometryGb.build(kodachi::GroupBuilder::BuildAndRetain))));
//        // Prevent UVs from being added as a PrimitiveAttribute
//        rdl2GeometryGb.del("geometry.arbitrary.st");
//
//        // primitive attributes
//        // will now contain an extra
//        // prmitiveAttributes child containing User Data attributes
//        rdl2GeometryGb.deepUpdate(kodachi::GroupAttribute(
//                kodachi::AttributeFunctionUtil::run("KPOPPrimitiveAttributesAttrFunc",
//                        rdl2GeometryGb.build(kodachi::GroupBuilder::BuildAndRetain))));
//
//        // check displacement materials
//        // map the part name to the displacement terminal scene object
//        std::map<kodachi::string_view,
//            kodachi_moonray::MoonrayRenderState::SceneObjectPtr> partDisplacementMap;
//        static const std::string kMesh("mesh");
//        bool partAssignment = false;
//        const kodachi::GroupAttribute displacementAttr = keyAttr.getChildByName("displacement");
//        if (displacementAttr.isValid()) {
//            const kodachi::GroupAttribute partsDisplacements =
//                    displacementAttr.getChildByName("parts");
//
//            // per part assignments
//            if (partsDisplacements.isValid()) {
//                for (const auto& part : partsDisplacements) {
//                    std::cout << "[PART] " << part.name << "\n";
//                    const kodachi::GroupAttribute partGrp(part.attribute);
//                    const kodachi::GroupAttribute nodesGrp = partGrp.getChildByName("nodes");
//                    const kodachi::StringAttribute terminalNodeName = partGrp.getChildByName("rdl2.layerAssign.displacement");
//
//                    std::cout << "---terminal node name: " << terminalNodeName.getValue("", false) << "\n";
//
//                    if (nodesGrp.isValid()) {
//                        for (const auto& node : nodesGrp) {
//                            const auto processedNode =
//                                    moonrayRenderState->processLocation(
//                                            kodachi::StringAttribute(node.name.data()), kodachi::GroupAttribute(node.attribute));
//
//                            if (!processedNode) {
//                                std::cout << "Failure creating node: " << node.name << "\n";
//                                KdLogError("Failure creating node: " << node.name);
//                                continue;
//                            }
//
//                            std::cout << "Created Node: " << processedNode->getName() << " --- " <<  processedNode.get() << "\n";
//                            if (terminalNodeName == processedNode->getName()) {
//                                // only using per part assignment when at least one part terminal node is successfully
//                                // created
//                                partAssignment = true;
//                                // this is the terminal node, store it with the part
//                                partDisplacementMap.emplace(part.name, processedNode);
//                            }
//                        }
//                    }
//                }
//            }
//
//            {
//                // mesh displacement material
//                std::cout << "[MESH]\n";
//                const kodachi::GroupAttribute meshDisplacement =
//                        displacementAttr.getChildByName(kMesh);
//                const kodachi::GroupAttribute nodesGrp = meshDisplacement.getChildByName("nodes");
//                const kodachi::StringAttribute terminalNodeName = meshDisplacement.getChildByName("rdl2.layerAssign.displacement");
//
//                std::cout << "---terminal node name: " << terminalNodeName.getValue("", false) << "\n";
//
//                if (nodesGrp.isValid()) {
//                    for (const auto& node : nodesGrp) {
//                        const auto processedNode =
//                                moonrayRenderState->processLocation(
//                                        kodachi::StringAttribute(node.name.data()), kodachi::GroupAttribute(node.attribute));
//                        if (!processedNode) {
//                            std::cout << "Failure creating node: " << node.name << "\n";
//                            KdLogError("Failure creating node: " << node.name);
//                            continue;
//                        }
//
//                        std::cout << "Created Node: " << processedNode->getName() << " --- " <<  processedNode.get() << "\n";
//                        if (terminalNodeName == processedNode->getName()) {
//                            // this is the terminal node, store it with the part name 'mesh'
//                            partDisplacementMap.emplace(kMesh, processedNode);
//                        }
//                    }
//                }
//            }
//        } // if has displacement
//
//        // final geometry conversion result
//        const kodachi::GroupAttribute rdl2FinalGeometryAttrs = rdl2GeometryGb.build();
//
//        // Debug
//        std::cout << "[BakeGeometry] Geometry Conversion\n";
//        printGroup(rdl2FinalGeometryAttrs);
//
//        // primitive attributes
//        {
//            kodachi::GroupAttribute primitiveAttributes =
//                    rdl2FinalGeometryAttrs.getChildByName("primitiveAttributes");
//            if (primitiveAttributes.isValid()) {
//                for (const auto& primitiveAttr : primitiveAttributes) {
//                    const auto userDataObj =
//                            moonrayRenderState->processLocation(
//                                    kodachi::StringAttribute(primitiveAttr.name.data()), primitiveAttr.attribute);
//                    if (!userDataObj) {
//                        std::cout << "Failure creating UserData for Primitive Attribute: " << primitiveAttr.name << "\n";
//                        KdLogError("Failure creating UserData for Primitive Attribute: " << primitiveAttr.name);
//                        continue;
//                    }
//                }
//            }
//        }
//
//        // rdl2 conversion
//        // we only need processLocation to use: rdl2.meta.<shutter>
//        //                                          .sceneObject.sceneClass
//        //                                                      .name
//        //                                                      .attrs
//        //                                          .layerAssign (geometry, layer, lightSet)
//        //                                          .geoSetAssign
//        // - create scene object
//        // - set scene object attrs
//        // - add to deferred layer assign
//        // - add to deferred geo set assign
//        geometryObj =
//                moonrayRenderState->processLocation(geometryName, rdl2FinalGeometryAttrs);
//        if (!geometryObj) {
//            KdLogError("Failure processing scene object for geometry: " <<
//                    geometryName.getValueCStr());
//            return {};
//        }
//
//        rdl2::Geometry* geo = geometryObj->asA<rdl2::Geometry>();
//        std::cout << " -----[CREATED GEOMETRY] " << geo << "\n";
//        if (!geo) {
//            KdLogError("Scene object is not geometry: " <<
//                    geometryName.getValueCStr());
//            return {};
//        }
//
//        // TODO: process connections?
//        rdl2::Material* baseMaterial = sceneContext.createSceneObject("DwaBaseMaterial",
//                "material")->asA<rdl2::Material>();
//
//        // for deferred connections
//        moonrayRenderState->processingComplete();
//
//        // layer assign
//        {
//            rdl2::Layer::UpdateGuard updateGuard(layer);
//
//            // geometry layer assignment
//            // TODO: KPOPLayerAssign
//            {
//                rdl2::Displacement* displacementObj = nullptr;
//
//                const auto meshDisplacementObj = partDisplacementMap.find(kMesh);
//                if (meshDisplacementObj != partDisplacementMap.end()) {
//                    displacementObj = (*meshDisplacementObj).second->asA<rdl2::Displacement>();
//                }
//
//                if (!displacementObj) {
//                    std::cout << "No displacement object for mesh\n";
//                } else {
//                    std::cout << "Assigning geometry: " << displacementObj->getName() << "\n";
//                }
//
//                layer->assign(geo, "", baseMaterial, lightSet, displacementObj, nullptr);
//            }
//
//            // per part layer assignments
//            {
//                for (const auto& part : partsAttr) {
//                    rdl2::Displacement* displacementObj = nullptr;
//                    const auto partDisplacementObj = partDisplacementMap.find(part.name);
//                    if (partDisplacementObj != partDisplacementMap.end()) {
//                        displacementObj = (*partDisplacementObj).second->asA<rdl2::Displacement>();
//                    }
//
//                    if (!displacementObj) {
//                        std::cout << "No displacement object to part " << part.name << "\n";
//                    } else {
//                        std::cout << "Assigning to part: " << part.name << " --- "
//                                << displacementObj->getName() << "\n";
//                    }
//
//                    layer->assign(geo, part.name.data(), baseMaterial, lightSet, displacementObj, nullptr);
//                }
//            }
//        }
//        // geometry set
//        {
//            // TODO: KPOPGeometrySetAssign
//            rdl2::GeometrySet::UpdateGuard updateGuard(geometrySet);
//            geometrySet->add(geo);
//        }
//    }
//
//    // initialize the render context
//    // because bake requires Camera and Geometry Managers
//    std::stringstream initmessages; // dummy
//    bakeGeometryContext->initialize(initmessages);
//
//    // *** BAKE ***
//    std::vector<geom::BakedMesh*> bakedMeshes;
//    KdLogDebug("Baking: " << geometryName.getValueCStr());
//    bakeGeometryContext->bakeGeometry(bakedMeshes);
//    if (bakedMeshes.empty()) {
//        KdLogError("No baked meshes returned.");
//        return {};
//    }
//
//    // *** SET RESULT ATTRS ***
//    // output
//    kodachi::GroupBuilder resultBuilder;
//    std::vector<float> sampleTimes { shutterOpen, shutterClose };
//    std::vector<float> singleSampleTime { 0.0f };
//
//    // parts information of the old geometry
//    std::map<kodachi::string_view, std::set<int32_t>> controlMeshPartFaces;
//    // output: new parts information of the baked geometry
//    std::map<kodachi::string_view, std::set<int32_t>> bakedMeshPartFaces;
//
//    if (usingParts) {
//        for (const auto& part : partsAttr) {
//            const kodachi::IntAttribute facesAttr = part.attribute;
//            const auto faces = facesAttr.getNearestSample(0.0f);
//            controlMeshPartFaces[part.name] = std::set<int32_t>(faces.begin(), faces.end());
//            bakedMeshPartFaces  [part.name] = std::set<int32_t>();
//        }
//    }
//
//    // see comments on top for BakedMesh structure
//    // only expect one baked mesh result for now
//    {
//        const geom::BakedMesh* bakedMesh = bakedMeshes.front();
//        KdLogDebug("Baked Result: " << bakedMesh->mRdlGeometry->getName());
//
//        const size_t motionSampleCount = mbEnabled ? bakedMesh->mMotionSampleCount : 1;
//
//        // point.P //
//
//        const kodachi::array_view<rdl2::Vec3f> vertexBufferView(bakedMesh->mVertexBuffer);
//
//        const int numVerts = bakedMesh->mVertexCount; // varying rate / vertex rate
//        KdLogDebug("Num Verts: " << numVerts);
//        if (numVerts == 0) {
//            KdLogError(geometryName.getValueCStr() << " returned empty baked vertices.");
//            return {};
//        }
//        // assume points will have the proper sample times
//        kodachi::FloatAttribute points =
//                remapMultiSampleAttr<kodachi::FloatAttribute>(vertexBufferView,
//                        motionSampleCount > 1 ? sampleTimes : singleSampleTime,
//                        motionSampleCount,
//                        numVerts,
//                        3);
//
//        // vertex points have been transformed due to camera and geometry xforms
//        // render2Object xform will transform them back into object space
//        rdl2::Geometry* geo = geometryObj->asA<rdl2::Geometry>();
//        const math::Xform3f render2ObjectXform = geo->getRender2Object();
//        std::cout << "[Render 2 Object] " << render2ObjectXform << "\n";
//        points = invertPointTransforms(points, render2ObjectXform);
//
//        resultBuilder.set("point.P", points);
//
//        // poly.vertexList //
//        // poly.startIndex //
//
//        // mIndexBuffer is unsigned int
//        std::vector<int32_t> indexBuffer(bakedMesh->mIndexBuffer.begin(),
//                bakedMesh->mIndexBuffer.end());
//        if (indexBuffer.size() == 0) {
//            KdLogError(geometryName.getValueCStr() << " returned empty baked indices.");
//            return {};
//        }
//
//        const int vertsPerFace = bakedMesh->mVertsPerFace;
//        const int numIndices   = indexBuffer.size(); // face varying rate
//        const int numFaces     = numIndices / vertsPerFace; // uniform rate
//        KdLogDebug("Verts Per Face: " << vertsPerFace);
//        KdLogDebug("Num Indices: "    << numIndices);
//        KdLogDebug("Num Faces: "      << numFaces);
//
//        std::vector<int32_t> startIndex;
//        startIndex.reserve(numFaces + 1);
//        for (int32_t i = 0; i <= indexBuffer.size(); i += vertsPerFace) {
//            startIndex.emplace_back(i);
//        }
//
//        // arbitrary attrs //
//        auto rateToDataSize = [&](geom::AttributeRate rate)->size_t {
//            switch(rate) {
//            case geom::RATE_CONSTANT:
//                return 1;
//                break;
//            case geom::RATE_UNIFORM:
//                return numFaces;
//                break;
//            case geom::RATE_VARYING:
//            case geom::RATE_VERTEX:
//                return numVerts;
//                break;
//            case geom::RATE_FACE_VARYING:
//                return numIndices;
//                break;
//            case geom::RATE_PART:
//                return 1;
//                break;
//            }
//            KdLogError("Unrecognized attribute rate.");
//            return 0;
//        };
//
//        static const std::string kArbitraryGrp("arbitrary.");
//        static const std::string kScope       ("scope");
//        static const std::string kInputType   ("inputType");
//        static const std::string kElemSize    ("elementSize");
//        static const std::string kValue       ("value");
//
//        kodachi::StringAttribute kVertexScope ("vertex");
//        kodachi::StringAttribute kFloat       ("float");
//
//        kodachi::GroupBuilder arbitraryAttrBuilder;
//
//        // primitive attributes
//        {
//            static const std::string kStName("st");
//            static const std::string kSurfaceStName("surface_st");
//            static const std::string kNormalName("normal");
//
//            std::cout << "[Arbitrary Attributes]\n";
//            for (const std::unique_ptr<
//                    geom::BakedAttribute>& bakedAttr : bakedMesh->mAttrs) {
//
//                KdLogDebug("Extracting attribute: " << bakedAttr->mName);
//                std::cout << "Extracting attribute: " << bakedAttr->mName << "\n";
//
//                const geom::AttributeRate rate = bakedAttr->mRate;
//                const size_t dataSizePerSample = rateToDataSize(rate);
//                const size_t attrMotionSampleCount = bakedAttr->mTimeSampleCount;
//
//                if (bakedAttr->mNumElements !=
//                        (attrMotionSampleCount * dataSizePerSample)) {
//                    KdLogDebug("Unexpected element count in arbitrary attr '" <<
//                            bakedAttr->mName << "' - size is " <<
//                            bakedAttr->mNumElements << " vs the expected " <<
//                            attrMotionSampleCount << " * " << dataSizePerSample
//                            << "; Skipping.");
//                    std::cout << "Unexpected element count in arbitrary attr '" <<
//                            bakedAttr->mName << "' - size is " <<
//                            bakedAttr->mNumElements << " vs the expected " <<
//                            attrMotionSampleCount << " * " << dataSizePerSample << "\n";
//                    continue;
//                }
//
//                kodachi::GroupBuilder attrGb;
//                kodachi::StringAttribute scope = attributeRateToScope(rate);
//                attrGb.set(kScope, scope);
//                kodachi::DataAttribute arbitraryAttr =
//                        extractPrimitiveAttr(attrGb,
//                                             bakedAttr->mData,
//                                             bakedAttr->mType,
//                                             attrMotionSampleCount > 1 ? sampleTimes : singleSampleTime,
//                                             attrMotionSampleCount,
//                                             dataSizePerSample);
//                if (arbitraryAttr.isValid()) {
//                    std::string name = bakedAttr->mName;
//
//                    // rename surface_st to st
//                    if (name == kSurfaceStName) {
//                        name = kStName;
//                    }
//
//                    // Note: RdlMeshGeometry does not accept non vertex scoped uv's or normals
//                    // so we won't bother with point.N here
//                    // Moonray's subd tesselation only outputs point scope uv's and normals,
//                    // but bake geometry will interpolate them back to vertex scopes
//
//                    // for normals, let's set vertex.N if it was provided in the original
//                    // geometry
//                    // normals are weird because RdlMeshGeometry will ignore them if any
//                    // of these are true:
//                    // - there are displacements
//                    // - smooth_normals attribute is true
//                    // - real time mode (arras)
//                    if (name == kNormalName) {
//                        // normals are also in render space - transform them back to object space
//                        arbitraryAttr =
//                                invertPointTransforms(arbitraryAttr, render2ObjectXform, true);
//                        if (scope == "vertex" &&
//                                geometryAttrs.getChildByName("vertex.N").isValid()) {
//                            resultBuilder.set("vertex.N", arbitraryAttr);
//                        }
//                    }
//
//                    attrGb.set(kValue, arbitraryAttr);
//                    arbitraryAttrBuilder.set(name, attrGb.build());
//                }
//            } // primitive attr for loop
//        }
//
//        // !!! indexBuffer now invalid !!!
//        resultBuilder.set("poly.vertexList",
//                kodachi::ZeroCopyIntAttribute::create(std::move(indexBuffer)));
//        resultBuilder.set("poly.startIndex",
//                kodachi::ZeroCopyIntAttribute::create(std::move(startIndex)));
//
//        resultBuilder.set("arbitrary", arbitraryAttrBuilder.build());
//
//        if (usingParts) {
//            // parts
//            // this vector maps the corresponding face id to the original meshes' face id
//            // we need to seek out the original face id in the child parts, and replace it with the current id
//            const kodachi::array_view<int> tessellatedToControlFace(
//                    bakedMesh->mTessellatedToBaseFace);
//            KdLogDebug("Num Tesselated to Control Face Ids: "      << tessellatedToControlFace.size());
//
//            for (int32_t newId = 0; newId < tessellatedToControlFace.size(); ++newId) {
//                for (const auto& part : controlMeshPartFaces) {
//                    const auto& oldPartFaces = part.second;
//                    if (oldPartFaces.find(tessellatedToControlFace[newId]) != oldPartFaces.end()) {
//                        // this face belonged to this part
//                        bakedMeshPartFaces[part.first].insert(newId);
//                    }
//                }
//            }
//
//            kodachi::GroupBuilder partsBuilder;
//            for (const auto& newPart : bakedMeshPartFaces) {
//                const auto& newPartFaces = newPart.second;
//                std::vector<int32_t> newPartFacesList;
//                newPartFacesList.insert(newPartFacesList.end(), newPartFaces.begin(), newPartFaces.end());
//
//                partsBuilder.set(newPart.first,
//                        kodachi::ZeroCopyIntAttribute::create(std::move(newPartFacesList)));
//
//            }
//            resultBuilder.set("parts", partsBuilder.build());
//        }
//    }
//
//    KdLogDebug("Baking complete.");
//    return resultBuilder.build();
//}
//
//kodachi::GroupAttributeCache<createBakedGeometry>::Ptr_t&
//bakeGeometryCache()
//{
//    // *** kodachi cache ***
//    static auto sBakeGeometryCache =
//              kodachi::GroupAttributeCache<createBakedGeometry>::createCache("BakeGeometryOp");
//    return sBakeGeometryCache;
//}
//
//// sets 'faces' attribute for faceset children under baked meshes
//class BakeFacesOp : public kodachi::Op
//{
//public:
//    static void setup(kodachi::OpSetupInterface &interface)
//    {
//        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
//    }
//
//    static void cook(kodachi::OpCookInterface &interface)
//    {
//        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
//        if (typeAttr == "faceset") {
//
//            // set new faces
//            const kodachi::IntAttribute newFaces =
//                    interface.getOpArg(interface.getInputName());
//            if (newFaces.isValid()) {
//                interface.setAttr("geometry.faces", newFaces);
//            }
//        }
//    }
//};
//
//// In Katana, BakeGeometry is used as a Macro and only the resulting
//// geometry attribute is copied back to the main scene graph
//// This Op performs cleanup functionality for baked geometries:
//// - removes moonrayMeshStatements bake geometry attributes
//// - resets moonrayMeshStatements subdivision surface attributes
//// - removes displacement terminals; currently assumes there will not be a different
//// displacement shader assigned AFTER bake geometry
//class BakedGeometryCleanupOp : public kodachi::Op
//{
//public:
//    static void setup(kodachi::OpSetupInterface &interface)
//    {
//        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
//    }
//
//    static void cook(kodachi::OpCookInterface &interface)
//    {
//        const kodachi::IntAttribute bakedGeometryAttr =
//                // global attr so facesets can get the attr from their parents
//                kodachi::GetGlobalAttr(interface, "geometry.baked");
//        if (bakedGeometryAttr.getValue(0, false)) {
//
//            interface.deleteAttr("material.terminals.moonrayDisplacement");
//            interface.deleteAttr("material.terminals.moonrayDisplacementPort");
//            interface.deleteAttr("moonrayMeshStatements.bake_geometry");
//            interface.deleteAttr("moonrayMeshStatements.bake_geometry_camera");
//
//            interface.setAttr("moonrayMeshStatements.mesh_resolution", kodachi::FloatAttribute(1.0f));
//            interface.setAttr("moonrayMeshStatements.reverse winding order", kodachi::IntAttribute(0));
//
//            // for subdmeshes, tesselation code always run even at resolution = 1
//            // reset it to polymesh to avoid further tesselation from Moonray
//            if (kodachi::StringAttribute(interface.getAttr("type")) == "subdmesh") {
//                interface.setAttr("type", kodachi::StringAttribute("polymesh"));
//            }
//        }
//    }
//};
//
//// ===================================================================
//// BakeGeometryContextOp
//// Gathers all the geometry marked for baking, and populates the render
//// context if necessary.
//// Currently expects materials and xforms to be localized, and Variables
//// to be resolved.
//// Prerequisite ops:
//// - VariableResolve
//// - MaterialResolve
//// - MaterialToNetworkMaterial
//// - MoonrayFlattenMaterial
//// - LocalizeXform
//// ===================================================================
//class BakeGeometryOp : public kodachi::Op
//{
//public:
//    static void setup(kodachi::OpSetupInterface &interface)
//    {
//        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
//    }
//
//    static void cook(kodachi::OpCookInterface &interface)
//    {
//        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
//        const bool isSubD = (typeAttr == "subdmesh");
//
//        if (!isSubD && typeAttr != "polymesh") {
//            return;
//        }
//
//        // *** moonray mesh statements ***
//        const kodachi::GroupAttribute moonrayMeshStatementsAttr =
//                kodachi::GetGlobalAttr(interface, "moonrayMeshStatements");
//        if (!moonrayMeshStatementsAttr.isValid()) {
//            return;
//        }
//
//        // *** bake geometry (true or false) ***
//        const kodachi::IntAttribute bakeGeometryAttr =
//                moonrayMeshStatementsAttr.getChildByName("bake_geometry");
//        // Always remove the attr if it exists since we are handling it here.
//        if (bakeGeometryAttr.isValid()) {
//            interface.deleteAttr("moonrayMeshStatements.bake_geometry");
//        }
//
//        // *** camera path ***
//        kodachi::StringAttribute bakeGeometryCamAttr =
//                interface.getAttr("moonrayMeshStatements.bake_geometry_camera");
//        // Always remove the attr if it exists since we are handling it here.
//        if (bakeGeometryCamAttr.isValid()) {
//            interface.deleteAttr("moonrayMeshStatements.bake_geometry_camera");
//        }
//
//        // If set to false there is nothing else to do.
//        if (!bakeGeometryAttr.getValue(false, false)) {
//            return;
//        }
//
//        // check if camera path is empty
//        // if so, we'll use the render settings camera
//        kodachi::string_view bakeGeometryCamPath = bakeGeometryCamAttr.getValue("", false);
//        if (bakeGeometryCamPath.empty()) {
//            // if no camera specified, default to render settings camera
//            KdLogDebug("No camera specified for Bake Geometry, defaulting to RenderSettings camera.");
//            bakeGeometryCamAttr =
//                            interface.getAttr("renderSettings.cameraName", "/root");
//
//            // test again
//            bakeGeometryCamPath = bakeGeometryCamAttr.getValue("", false);
//            if (bakeGeometryCamPath.empty()) {
//                KdLogDebug("No render camera set; defaulting to '/root/world/cam/camera'.");
//                static const kodachi::StringAttribute kDefaultCamPath("/root/world/cam/camera");
//                bakeGeometryCamAttr = kDefaultCamPath;
//                bakeGeometryCamPath = bakeGeometryCamAttr.getValue();
//            }
//        }
//        std::cout << "Using Camera: " << bakeGeometryCamPath << "\n";
//
//        // *** geometry ***
//        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
//        if (!geometryAttr.isValid()) {
//            KdLogWarn("Missing 'geometry' attribute.");
//            kodachi::ReportWarning(interface, "Missing 'geometry' attribute.");
//            return;
//        }
//
//        // *** camera ***
//        interface.prefetch(bakeGeometryCamPath);
//        const kodachi::GroupAttribute cameraGeometryAttr =
//                interface.getAttr("geometry", bakeGeometryCamPath);
//        if (!cameraGeometryAttr.isValid()) {
//            std::stringstream ss;
//            ss << "Could not get 'geometry' attr from camera: '" <<
//                    bakeGeometryCamPath << "'";
//            KdLogWarn(ss.str());
//            kodachi::ReportWarning(interface, ss.str());
//            return;
//        }
//
//        const std::string inputPath = interface.getInputLocationPath();
//        KdLogDebug("Processing: " << inputPath);
//        const auto start = std::chrono::system_clock::now();
//
//        // *** shutter and mb attrs ***
//        float shutterOpen    = kodachi::GetShutterOpen(interface);
//        float shutterClose   = kodachi::GetShutterClose(interface);
//        const bool mbEnabled = (kodachi::GetNumSamples(interface) > 1);
//
//        if (!mbEnabled) {
//            shutterOpen = shutterClose = 0;
//        }
//
//        const kodachi::GroupAttribute shutterAttrs(
//            "shutterOpen" , kodachi::FloatAttribute(shutterOpen),
//            "shutterClose", kodachi::FloatAttribute(shutterClose),
//            "mbEnabled"   , kodachi::IntAttribute  (mbEnabled),
//            false
//        );
//
//        // *** xform info ***
//        const kodachi::GroupAttribute geometryXformAttr = interface.getAttr("xform");
//        const kodachi::GroupAttribute camXformAttr =
//                                     interface.getAttr("xform", bakeGeometryCamPath);
//
//        // *** cache key entries ***
//        kodachi::GroupBuilder keyBuilder;
//        keyBuilder.set("type",           typeAttr);
//        keyBuilder.set("geometry_attrs", geometryAttr);
//        keyBuilder.set("geometry_xform", geometryXformAttr);
//        keyBuilder.set("camera_attrs",   cameraGeometryAttr);
//        keyBuilder.set("camera_path",    bakeGeometryCamAttr);
//        keyBuilder.set("camera_xform",   camXformAttr);
//        keyBuilder.set("shutter_attrs",  shutterAttrs);
//        keyBuilder.set("moonrayMeshStatements", moonrayMeshStatementsAttr);
//        keyBuilder.set("moonrayStatements",
//                kodachi::GetGlobalAttr(interface, "moonrayStatements"));
//
//        kodachi::GroupBuilder supportArgsBuilder;
//        supportArgsBuilder.set("input_location", kodachi::StringAttribute(inputPath));
//
//        // *** materials ***
//        // either sets <part name>.<material attr>
//        // if per part materials exist, or
//        // just a material attr if no per part materials exist
//        kodachi::GroupBuilder materialGb;
//
//        // *** parts ***
//        const auto potentialChildrenAttr = interface.getPotentialChildren();
//        const auto potentialChildren = potentialChildrenAttr.getNearestSample(0.0f);
//        kodachi::GroupBuilder partsBuilder;
//        for (auto& child : potentialChildren) {
//            interface.prefetch(child);
//        }
//
//        static const std::string sFacesetLocation = "faceset";
//        static const std::string sGeometryFaces   = "geometry.faces";
//        // set up parts information to pass to the cache
//        for (auto & child : potentialChildren) {
//            if (GetInputLocationType(interface, child) == sFacesetLocation) {
//                const kodachi::IntAttribute faces =
//                        interface.getAttr(sGeometryFaces, child);
//
//                if (faces.isValid()) {
//                    partsBuilder.set(child, faces);
//                }
//
//                // set material attr if any
//                // !!! assumes material has been localized
//                kodachi::GroupAttribute  materialGroup  = interface.getAttr("material", child);
//                if (!materialGroup.isValid()) {
//                    continue;
//                }
//                materialGb.set(child, materialGroup);
//            }
//        }
//        keyBuilder.set("parts", partsBuilder.build());
//
//        // *** displacement ***
//        // filter out other terminals except displacement
//        // sets either parts or mesh rdl2 and nodes data
//        // result: either parts.<part>.rdl2.layerAssign.<terminal>.<object name>
//        //             or  mesh.       nodes.<object name>.<attrs>
//        static const std::string kMoonrayDisplacement("moonrayDisplacement");
//        static const kodachi::GroupAttribute terminalFilterAttr(
//                kMoonrayDisplacement, kodachi::IntAttribute(1), false);
//        bool hasDisplacement = false;
//
//        const kodachi::GroupAttribute partMaterials = materialGb.build();
//        // process each part material
//        for (const auto& partMaterial : partMaterials) {
//            kodachi::GroupBuilder argsGb;
//            argsGb.deepUpdate(partMaterial.attribute);
//            // filter to only process displacement terminals
//            argsGb.set("filter", terminalFilterAttr);
//
//            const kodachi::GroupAttribute kpopPartMaterial =
//                    kodachi::AttributeFunctionUtil::run("KPOPMaterialAttrFunc", argsGb.build());
//            if (kpopPartMaterial.getChildByName("rdl2.layerAssign.displacement").isValid()) {
//                materialGb.set(kodachi::concat("parts.", partMaterial.name), kpopPartMaterial);
//                hasDisplacement = true;
//            }
//        }
//
//        // check the material on the geometry itself
//        // !!! assumes material has been localized
//        kodachi::GroupAttribute  materialGroup  = interface.getAttr("material");
//        if (materialGroup.isValid()) {
//            kodachi::GroupBuilder argsGb;
//            argsGb.deepUpdate(materialGroup);
//            // filter to only process displacement terminals
//            argsGb.set("filter", terminalFilterAttr);
//
//            const kodachi::GroupAttribute kpopMaterial =
//                    kodachi::AttributeFunctionUtil::run("KPOPMaterialAttrFunc", argsGb.build());
//            if (kpopMaterial.getChildByName("rdl2.layerAssign.displacement").isValid()) {
//                materialGb.set("mesh", kpopMaterial);
//                hasDisplacement = true;
//            }
//        }
//        keyBuilder.set("displacement", materialGb.build());
//
//        kodachi::GroupAttribute supportArgsGrp = supportArgsBuilder.build();
//
//        // *** mesh resolution ***
//        const float meshResolution = kodachi::FloatAttribute(
//                moonrayMeshStatementsAttr.getChildByName("mesh_resolution")).getValue(2.0f, false);
//        std::cout << "MESH RESOLUTION: " << meshResolution << "\n";
//        std::cout << "MESH DISPLACEMENT: " << hasDisplacement << "\n";
//
//        // if mesh resolution is 1 and no displacement has been assigned, no work needs to be done
//        // however, subdmeshes are baked regardless even at resolution = 1, so we will let subdmesh through
//        if ((!isSubD && meshResolution <= 1.0f) && !hasDisplacement) {
//            KdLogDebug("Polymesh Resolution <= 1 and no displacement assigned - no work needed; Skipping.");
//            std::cout << "Polymesh Resolution <= 1 and no displacement assigned - no work needed; Skipping.\n";
//            return;
//        }
//
//        // *** get or create bake results ***
//        const kodachi::GroupAttribute bakedGeometryAttrs =
//                bakeGeometryCache()->getValue(keyBuilder.build(), &supportArgsGrp);
//        if (!bakedGeometryAttrs.isValid()) {
//            FnKat::ReportError(interface, "BakeGeometry processing failed.");
//            return;
//        }
//
//        // *** timing ***
//        const auto end = std::chrono::system_clock::now();
//        auto dur = end - start;
//
//        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(dur);
//        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(dur -= minutes);
//        const auto millis  = std::chrono::duration_cast<std::chrono::milliseconds>(dur -= seconds);
//
//        KdLogDebug(" >>> Processing time: "
//                << std::setfill('0') << std::setw(2)
//                << minutes.count() << ":" << std::setw(2)
//                << seconds.count() << "." << std::setw(3)
//                << millis.count()  << " (mm:ss.ms)");
//
//        // *** set result attrs ***
//        interface.setAttr("geometry", bakedGeometryAttrs);
//
//        // Displacement needs to be reset as well so it does not get doubly applied
//        // this allows us to know if a geometry has been baked, and to remove
//        // any displacement assignments to it
//        // we will also reset mesh resolution and reverse winding order since those are also
//        // applied in the bake
//        interface.setAttr("geometry.baked", kodachi::IntAttribute(1));
//
//        // *** handle new parts ***
//        const kodachi::GroupAttribute newParts = bakedGeometryAttrs.getChildByName("parts");
//        if (newParts.isValid()) {
//            // new parts contains the new 'faces' attribute for each child name
//            // child faces can now set their 'faces' attribute
//            interface.replaceChildTraversalOp("BakeFacesOp", newParts);
//        }
//        interface.deleteAttr("geometry.parts");
//    } // cook
//
//    static kodachi::GroupAttribute describe()
//    {
//        kodachi::OpDescriptionBuilder builder;
//
//        const std::string opHelp    = "";
//        const std::string opSummary = "";
//
//        builder.setHelp(opHelp);
//        builder.setSummary(opSummary);
//        builder.setNumInputs(0);
//
//        return builder.build();
//    }
//
//    static void flush()
//    {
//        bakeGeometryCache()->clear(kodachi::Cache::ClearAction::MEMORY);
//    }
//};

// BakeGeometryRdlOp ===================================================================
// Bake Geometry from given rdl file
class BakeGeometryRdlOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::StringAttribute rootNameArg = interface.getOpArg("name");
        const std::string rootName = rootNameArg.getValue("", false);

        interface.stopChildTraversal();

        const kodachi::StringAttribute rdlFile = interface.getOpArg("scene_file_input");
        if (rdlFile.isValid()) {
            const std::string rdlFileStr = rdlFile.getValue();
            if (rdlFileStr.empty()) {
                // nothing to do
                return;
            }

            if (rdlFileStr.rfind(".rdla") == std::string::npos &&
                     rdlFileStr.rfind(".rdlb") == std::string::npos) {
                 KdLogWarn("Invalid rdl file.");
                 return;
            }
            KdLogDebug("Using scene file: " << rdlFileStr);

            using namespace arras;

            // init render options
            static std::unique_ptr<rndr::RenderOptions> sRenderOptions;
            if (!sRenderOptions) {
                static std::once_flag sInitRenderOptionsFlag;
                std::call_once(sInitRenderOptionsFlag,
                  [&]() {
                    sRenderOptions.reset(new rndr::RenderOptions);
                  }
                );
            }

            // make sure global driver is initialized
            {
                kodachi_moonray::moonray_util::initGlobalRenderDriver(*sRenderOptions);
            }

            std::unique_ptr<rndr::RenderContext> bakeGeometryContext(
                                        new rndr::RenderContext(*sRenderOptions));
            auto& sceneContext = bakeGeometryContext->getSceneContext();

            try {
                KdLogDebug("Reading scene...");
                rdl2::readSceneFromFile(rdlFileStr, sceneContext);
            } catch (const std::exception& e) {
                KdLogError("Error loading rdl scene file '"
                           << rdlFileStr << "'(" << e.what() << ")");
                return;
            }

            // shutter information
            std::vector<float> sampleTimes =
                    getMotionSteps(bakeGeometryContext);
            std::vector<float> singleSampleTime = { 0.0f };
            std::stringstream ss;
            ss << "Motion samples: ";
            for (const auto t : sampleTimes) {
                ss << t << " ";
            }
            KdLogDebug(ss.str());

            // initialize the render context
            // because bake requires Camera and Geometry Managers
            std::stringstream initmessages; // dummy
            bakeGeometryContext->initialize(initmessages);

            // *** BAKE ***
            std::vector<geom::BakedMesh*> bakedMeshes;
            KdLogDebug("Baking...");
            bakeGeometryContext->bakeGeometry(bakedMeshes);
            if (bakedMeshes.empty()) {
                KdLogError("No baked meshes returned.");
                return;
            }

            KdLogDebug("Baking Complete. Total Meshes Baked: " << bakedMeshes.size());
            kodachi::GroupBuilder resultBuilder;

            // *** BAKED MESHES LOOP ***
            for (const auto& bakedMesh : bakedMeshes) {
                kodachi::GroupBuilder geoBuilder;

                const std::string meshName = bakedMesh->mName;
                const std::string geoName  = bakedMesh->mRdlGeometry->getName();
                const std::string pathName =
                        kodachi::concat(geoName, "/", meshName);

                KdLogDebug("Baked Result: " << pathName);

                rdl2::SceneObject* obj = nullptr;
                rdl2::Geometry* geo = nullptr;
                if (sceneContext.sceneObjectExists(geoName)) {
                    obj = sceneContext.getSceneObject(geoName);
                    if (obj) {
                        geo = obj->asA<rdl2::Geometry>();
                    }
                }

                if (!geo) {
                    KdLogError("Could not find scene object for " << geoName);
                    continue;
                }

                size_t motionSampleCount = bakedMesh->mMotionSampleCount;
                bool mbEnabled = (motionSampleCount > 1);

                if (mbEnabled && (motionSampleCount != sampleTimes.size())) {
                    KdLogError("Motion sample mismatch! Baked mesh: " <<
                            motionSampleCount << " vs Render Context: " << sampleTimes.size());
                    mbEnabled = false;
                    motionSampleCount = 1;
                }

                // point.P //
                const int numVerts = bakedMesh->mVertexCount; // varying rate / vertex rate
                const kodachi::array_view<rdl2::Vec3f> vertexBufferView(bakedMesh->mVertexBuffer);
                KdLogDebug("Num verts: " << numVerts);

                if (numVerts == 0 ||
                        vertexBufferView.empty()) {
                    KdLogError(pathName << " returned empty baked vertices.");
                    continue;
                }

                // assume points will have the proper sample times
                kodachi::FloatAttribute points =
                        remapMultiSampleAttr<kodachi::FloatAttribute>(vertexBufferView,
                                mbEnabled ? sampleTimes : singleSampleTime,
                                motionSampleCount,
                                numVerts,
                                3);

                // vertex points have been transformed due to camera and geometry xforms
                // render2Object xform will transform them back into object space
                const math::Xform3f render2ObjectXform = geo->getRender2Object();
                points = invertPointTransforms(points, render2ObjectXform);

                geoBuilder.set("bound", calculateBounds(points));
                geoBuilder.set("geometry.point.P", points);

                // poly.vertexList //
                // poly.startIndex //

                // mIndexBuffer is unsigned int
                std::vector<int32_t> indexBuffer(bakedMesh->mIndexBuffer.begin(),
                        bakedMesh->mIndexBuffer.end());
                if (indexBuffer.empty()) {
                    KdLogError(pathName << " returned empty baked indices.");
                    continue;
                }
                const int vertsPerFace = bakedMesh->mVertsPerFace;
                const int numIndices   = indexBuffer.size(); // face varying rate
                const int numFaces     = numIndices / vertsPerFace; // uniform rate
                KdLogDebug("Verts Per Face: " << vertsPerFace);
                KdLogDebug("Num Indices: "    << numIndices);
                KdLogDebug("Num Faces: "      << numFaces);

                std::vector<int32_t> startIndex;
                startIndex.reserve(numFaces + 1);
                for (int32_t i = 0; i <= indexBuffer.size(); i += vertsPerFace) {
                    startIndex.emplace_back(i);
                }

                // arbitrary attrs //
                auto rateToDataSize = [&](shading::AttributeRate rate)->size_t {
                    switch(rate) {
                    case shading::RATE_CONSTANT:
                        return 1;
                        break;
                    case shading::RATE_UNIFORM:
                        return numFaces;
                        break;
                    case shading::RATE_VARYING:
                    case shading::RATE_VERTEX:
                        return numVerts;
                        break;
                    case shading::RATE_FACE_VARYING:
                        return numIndices;
                        break;
                    case shading::RATE_PART:
                        return 1;
                        break;
                    }
                    KdLogError("Unrecognized attribute rate.");
                    return 0;
                };

                static const std::string kArbitraryGrp("arbitrary.");
                static const std::string kScope       ("scope");
                static const std::string kInputType   ("inputType");
                static const std::string kElemSize    ("elementSize");
                static const std::string kValue       ("value");

                kodachi::StringAttribute kVertexScope ("vertex");
                kodachi::StringAttribute kFloat       ("float");

                kodachi::GroupBuilder arbitraryAttrBuilder;

                // primitive attributes //
                {
                    static const std::string kStName("st");
                    static const std::string kSurfaceStName("surface_st");
                    static const std::string kNormalName("normal");

                    for (const std::unique_ptr<
                            geom::BakedAttribute>& bakedAttr : bakedMesh->mAttrs) {

                        KdLogDebug("Extracting attribute: " << bakedAttr->mName);

                        const shading::AttributeRate rate = bakedAttr->mRate;
                        const size_t dataSizePerSample = rateToDataSize(rate);
                        size_t attrMotionSampleCount = bakedAttr->mTimeSampleCount;

                        if ((attrMotionSampleCount > 1) && (attrMotionSampleCount != sampleTimes.size())) {
                            KdLogError("Motion sample mismatch! Baked attr " << bakedAttr->mName << ": " <<
                                    attrMotionSampleCount << " vs Render Context: " << sampleTimes.size());
                            attrMotionSampleCount = 1;
                        }

                        if (bakedAttr->mNumElements !=
                                (attrMotionSampleCount * dataSizePerSample)) {
                            KdLogDebug("Unexpected element count in arbitrary attr '" <<
                                    bakedAttr->mName << "' - size is " <<
                                    bakedAttr->mNumElements << " vs the expected " <<
                                    attrMotionSampleCount << " * " << dataSizePerSample
                                    << "; Skipping.");
                            continue;
                        }

                        kodachi::GroupBuilder attrGb;
                        kodachi::StringAttribute scope = attributeRateToScope(rate);
                        attrGb.set(kScope, scope);
                        kodachi::DataAttribute arbitraryAttr =
                                extractPrimitiveAttr(attrGb,
                                                     bakedAttr->mData,
                                                     bakedAttr->mType,
                                                     attrMotionSampleCount > 1 ? sampleTimes : singleSampleTime,
                                                     attrMotionSampleCount,
                                                     dataSizePerSample);
                        if (arbitraryAttr.isValid()) {
                            std::string name = bakedAttr->mName;

                            // rename surface_st to st
                            if (name == kSurfaceStName) {
                                name = kStName;
                            }

                            // Note: RdlMeshGeometry does not accept non vertex scoped uv's or normals
                            // so we won't bother with point.N here
                            if (name == kNormalName) {
                                // normals are also in render space - transform them back to object space
                                arbitraryAttr =
                                        invertPointTransforms(arbitraryAttr, render2ObjectXform, true);
                                if (scope == "vertex") {
                                    geoBuilder.set("geometry.vertex.N", arbitraryAttr);
                                }
                            }

                            attrGb.set(kValue, arbitraryAttr);
                            arbitraryAttrBuilder.set(name, attrGb.build());
                        }
                    } // primitive attr for loop
                } // primitive attrs

                // !!! indexBuffer now invalid !!!
                geoBuilder.set("geometry.poly.vertexList",
                        kodachi::ZeroCopyIntAttribute::create(std::move(indexBuffer)));
                geoBuilder.set("geometry.poly.startIndex",
                        kodachi::ZeroCopyIntAttribute::create(std::move(startIndex)));
                geoBuilder.set("geometry.arbitrary", arbitraryAttrBuilder.build());

                // XFORM //
                {
                    std::vector<double> data;
                    const rdl2::Mat4d xformTSBegin =
                            geo->get(rdl2::Geometry::sNodeXformKey,
                                      rdl2::AttributeTimestep::TIMESTEP_BEGIN);
                    const double* xformTSBeginData =
                            reinterpret_cast<const double*>(&xformTSBegin);
                    data.insert(data.end(), xformTSBeginData, xformTSBeginData + 16);

                    const rdl2::Mat4d xformTSEnd =
                            geo->get(rdl2::Geometry::sNodeXformKey,
                                      rdl2::AttributeTimestep::TIMESTEP_END);

                    bool hasBlur = (xformTSBegin != xformTSEnd);
                    if (hasBlur && sampleTimes.size() != 2) {
                        KdLogWarn("xform motion samples do not match sample times");
                        hasBlur = false;
                    }

                    if (!hasBlur) {
                        geoBuilder.set("xform",
                                kodachi::ZeroCopyDoubleAttribute::create(std::move(data), 16));
                    } else {
                        const double* xformTSEndData =
                                reinterpret_cast<const double*>(&xformTSEnd);
                        data.insert(data.end(), xformTSEndData, xformTSEndData + 16);

                        geoBuilder.set("xform",
                                kodachi::ZeroCopyDoubleAttribute::create(
                                        sampleTimes, std::move(data), 16));
                    }
                }

                // PARTS //
                {
                    static const std::string kPartList("part_list");

                    // list of part names
                    const auto partListAttr =
                            geo->getSceneClass().getAttribute(kPartList);
                    rdl2::StringVector partList;

                    try {
                        partList = geo->get(
                            rdl2::AttributeKey<rdl2::StringVector>(*partListAttr));
                    } catch (...) { }
                    KdLogDebug("Num Parts: " << partList.size());

                    if (!partList.empty()) {
                        // original face id to part list index
                        const kodachi::array_view<int> faceToPart(bakedMesh->mFaceToPart);
                        // tesselated face to original face mapping
                        const kodachi::array_view<int> tesselatedToBaseFace(
                                bakedMesh->mTessellatedToBaseFace);

                        kodachi::GroupBuilder partsGb;
                        std::vector<std::vector<int>> partFaces;
                        partFaces.resize(partList.size());

                        for (size_t i = 0; i < tesselatedToBaseFace.size(); ++i) {
                            // for each new tesselated face, lookup which part it belongs to
                            partFaces[faceToPart[tesselatedToBaseFace[i]]].emplace_back(i);
                        }

                        for (size_t i = 0; i < partList.size(); ++i) {
                            partsGb.set(partList[i],
                                    kodachi::ZeroCopyIntAttribute::create(partFaces[i]));
                        }

                        geoBuilder.set("parts", partsGb.build());
                    }
                }

                geoBuilder.set("geometry.baked", kodachi::IntAttribute(1));
                resultBuilder.set(pathName, geoBuilder.build());

            } // baked meshes loop

            KdLogDebug("Creating locations...");
            const kodachi::GroupAttribute bakedGeometryResult =
                    resultBuilder.build();

            static const std::string kType("type");
            static const std::string kGeometry("geometry");
            static const std::string kPolymesh("polymesh");
            static const std::string kParts("parts");
            static const kodachi::StringAttribute kAttrSetArgsCel("//*");

            kodachi::StaticSceneCreateOpArgsBuilder sscb(true);

            for (const auto bakedGeometry : bakedGeometryResult) {
                const std::string path = (bakedGeometry.name[0] == '/') ?
                        kodachi::concat(rootName, bakedGeometry.name.data()) :
                        kodachi::concat(rootName, "/", bakedGeometry.name.data());

                const kodachi::GroupAttribute geoGroup(bakedGeometry.attribute);
                if (!geoGroup.isValid()) {
                    continue;
                }

                kodachi::GroupBuilder gb;
                // keep existing attrs if location previously exists
                gb.deepUpdate(interface.getAttr("", path));
                // geometry, xform, and bounds
                gb.deepUpdate(geoGroup);

                sscb.setAttrAtLocation(path, "", gb.build());
                sscb.setAttrAtLocation(path,
                        kType, kodachi::StringAttribute(kPolymesh));

                const kodachi::GroupAttribute partsGroup =
                                        geoGroup.getChildByName(kParts);
                if (partsGroup.isValid()) {
                    for (const auto part : partsGroup) {
                        const std::string childPath =
                                kodachi::concat(path, "/", part.name.data());
                        sscb.setAttrAtLocation(childPath,
                                kType, kodachi::StringAttribute("faceset"));
                        sscb.setAttrAtLocation(childPath,
                                "geometry.faces", part.attribute);
                    }
                }
            }
            interface.execOp("StaticSceneCreate", sscb.build());

            KdLogDebug("Bake Geometry Completed.");
        } // if rdl file valid
    }

private:
    static std::vector<float>
    getMotionSteps(const std::unique_ptr<arras::rndr::RenderContext>& renderContext)
    {
        using namespace arras;

        const auto& sceneVars = renderContext->getSceneContext().getSceneVariables();

        auto sceneVarsMotionSteps =
                sceneVars.get(rdl2::SceneVariables::sMotionSteps);
        if (!sceneVarsMotionSteps.empty()) {
            // rdl2 only supports 2 motion steps currently
            return sceneVarsMotionSteps;
        }

        return { 0.0f };
    }
};

class BakedGeometryViewerOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::IntAttribute bakedGeometryAttr =
                                    interface.getAttr("geometry.baked");

        if (bakedGeometryAttr.getValue(0, false)) {
            // during bake geometry, mesh winding orders are reversed for moonray's purposes
            // here let's reverse them back for Katana's viewer purposes
//            {
//                const kodachi::IntAttribute reverseWindingOrder =
//                        interface.getAttr("moonrayMeshStatements.reverse winding order");
//                if (reverseWindingOrder.getValue(true, false)) {
//                    kodachi::GroupAttribute reversedGeometryAttrs(
//                                    kodachi::AttributeFunctionUtil::run("KPOPMeshWindingOrderAttrFunc",
//                                                                        interface.getAttr("")));
//                    if (reversedGeometryAttrs.isValid()) {
//                        const kodachi::GroupAttribute resultGeometry =
//                                reversedGeometryAttrs.getChildByName("geometry");
//                        if (resultGeometry.isValid()) {
//                            kodachi::GroupBuilder geometryOutputGb;
//                            geometryOutputGb.deepUpdate(interface.getAttr("geometry"));
//                            geometryOutputGb.deepUpdate(resultGeometry);
//
//                            interface.setAttr("geometry", geometryOutputGb.build());
//                        }
//                    }
//                }
//            }

            // the viewer also doesn't like point scoped normal's and uv's
            {
                interface.deleteAttr("geometry.point.N");
                //interface.deleteAttr("geometry.vertex.N");

                static const kodachi::StringAttribute kPointScope("point");
                static const kodachi::StringAttribute kVertexScope("vertex");

                // turn point scoped uv's into vertex uv's
                kodachi::StringAttribute stScopeAttr =
                        interface.getAttr("geometry.arbitrary.st.scope");
                if (stScopeAttr == kPointScope) {
                    kodachi::IntAttribute vertexListAttr =
                            interface.getAttr("geometry.poly.vertexList");
                    if (!vertexListAttr.isValid()) {
                        // error
                        return;
                    }
                    const auto vertexList =
                            vertexListAttr.getNearestSample(0.0f);
                    std::vector<int32_t> indexBuffer(vertexList.begin(), vertexList.end());

                    kodachi::FloatAttribute stAttr =
                            interface.getAttr("geometry.arbitrary.st.value");

                    stAttr = remapPointToVertexScopeAttr(stAttr,
                                                         indexBuffer,
                                                         2);
                    interface.setAttr("geometry.arbitrary.st.value", stAttr);
                    interface.setAttr("geometry.arbitrary.st.scope", kVertexScope);
                }
            }
        } // if baked geometry
    }
};

//DEFINE_KODACHIOP_PLUGIN(BakeGeometryOp)
//DEFINE_KODACHIOP_PLUGIN(BakeFacesOp)
//DEFINE_KODACHIOP_PLUGIN(BakedGeometryCleanupOp)
DEFINE_KODACHIOP_PLUGIN(BakeGeometryRdlOp)
DEFINE_KODACHIOP_PLUGIN(BakedGeometryViewerOp)

}   // anonymous

void registerPlugins()
{
//    REGISTER_PLUGIN(BakeGeometryOp, "BakeGeometryOp", 0, 1);
//    REGISTER_PLUGIN(BakeFacesOp, "BakeFacesOp", 0, 1);
//    REGISTER_PLUGIN(BakedGeometryCleanupOp, "BakedGeometryCleanupOp", 0, 1);
    REGISTER_PLUGIN(BakeGeometryRdlOp, "BakeGeometryRdlOp", 0, 1);
    REGISTER_PLUGIN(BakedGeometryViewerOp, "BakedGeometryViewerOp", 0, 1);
}


