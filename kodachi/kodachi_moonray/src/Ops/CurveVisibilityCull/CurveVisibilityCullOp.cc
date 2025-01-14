// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/AttributeUtils.h>

#include <kodachi/attribute_function/AttributeFunctionPlugin.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>

#include <kodachi/cache/GroupAttributeCache.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <kodachi_moonray/embree_util/EmbreeUtil.h>
#include <kodachi_moonray/kodachi_geometry/GenerateUtil.h>

// tbb
#include <tbb/concurrent_vector.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_for_each.h>

// Imath
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>

// std
#include <unordered_map>
#include <unordered_set>

#include <atomic>

namespace
{

const std::string kCurveVisibilityCullOp { "CurveVisibilityCullOp" };

KdLogSetup(kCurveVisibilityCullOp);

const std::string sKeyName_curve_geometry("curve.geometry");
const std::string sKeyName_curve_xform("curve.xform");
const std::string sKeyName_obstructors("obstructors");
const std::string sKeyName_viewObject_xform("viewObject.xform");
const std::string sKeyName_mb("mb");

// debug
void printGroup(const kodachi::GroupAttribute& inG, int level) {
    if (inG.isValid()) {
        for (int i = 0; i < inG.getNumberOfChildren(); ++i) {
            KdLogDebug(std::string(level*3, '>') << "  " << inG.getChildName(i));
            printGroup(inG.getChildByIndex(i), level + 1);
        }
    }
}

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

using EmbreeScene = kodachi_moonray::embree_util::EmbreeScene;

bool
isCurveObstructed(
        EmbreeScene& embreeScene,
        kodachi::SampleAccessor<float>::const_reference curvePoints,
        const int64_t startIndex,
        const int64_t numCvs,
        const Imath::V3f& cameraPos,
        const float rayTime)
{
    // TODO: can we use ray packets to query all the rays at once?
    // TODO: can we share RTCIntersectionContext instead of creating
    // one for each query?
    // for each cv of the curve
    for (int64_t cv = 0; cv < numCvs; ++cv) {
        const int64_t idx = (startIndex + cv) * 3;

        // the CV
        const Imath::V3f cvPt(curvePoints[idx],
                              curvePoints[idx + 1],
                              curvePoints[idx + 2]);

        // embree does not make guarantees with intersections on or close to a
        // surface; shorten the ray slightly to include cv's near a surface
        // TODO: this is a giant band-aid for look diffs
        static constexpr float sRayError = 5.0f;

        // the ray
        const Imath::V3f rayDir(cvPt - cameraPos);
        EmbreeScene::Ray ray(cameraPos,             // ray origin
                             rayDir.normalized(),   // ray direction
                             rayDir.length() - sRayError,
                             rayTime);

        // query
        const bool cvObstructed =
                embreeScene.isOccluded(ray);

        // if any cv is not obstructed,
        // we need to keep the curve - return false
        // otherwise keep testing the other cv's
        if (!cvObstructed) {
            return false;
        }
    } // cv loop

    // all cv's have been tested, and all are obstructed by the mesh
    // return true - we can remove this curve
    return true;
}

std::atomic_uint sTotalMinutes(0);
std::atomic_uint sTotalSeconds(0);
std::atomic_uint sTotalCurves (0);
std::atomic_uint sTotalRuns   (0);

// curve geometry - geometry of curves, ie. point.P and numVertices
// curve xform group - local xform of the curve geometry, used to transform the
//                     curve to world space
// mesh attrs - a list of meshes that are potential obstructors for the curve geometry;
//              each mesh is expected to have its geometry and xform
// camera xform group - xform of the camera
// shutter open, shutter close
kodachi::GroupAttribute
visibilityCullInternal(const kodachi::GroupAttribute& curveGeometry,
                       const kodachi::GroupAttribute& curveXformGroup,
                       const kodachi::GroupAttribute& meshAttrs,
                       const kodachi::GroupAttribute& viewObjectXformGroup,
                       const kodachi::GroupAttribute& mbAttrs)
{
    if (!curveGeometry.isValid() ||
            !curveXformGroup.isValid() ||
            !viewObjectXformGroup.isValid() ||
            !meshAttrs.isValid()) {
        KdLogWarn(" >>> Curve Visibility Culling: missing necessary attrs.");
        return {};
    }

    KdLogDebug(" >>> Curve Visibility Culling: Inputs >>> " <<
               curveGeometry.getHash().str() << " | " <<
               curveXformGroup.getHash().str() << " | " <<
               meshAttrs.getHash().str() << " | " <<
               viewObjectXformGroup.getHash().str() << " | " <<
               mbAttrs.getHash().str());

    // *** sample times ***
    // only if mb is enabled
    std::vector<float> sampleTimes;

    const bool mbEnabled =
            kodachi::IntAttribute(mbAttrs.getChildByName("enabled")).getValue(false, false);

    KdLogDebug(" >>> Curve Visibility Culling: mb enabled " << mbEnabled);

    if (mbEnabled) {

        const float shutterOpen =
                kodachi::FloatAttribute(mbAttrs.getChildByName("shutterOpen")).getValue();
        const float shutterClose =
                kodachi::FloatAttribute(mbAttrs.getChildByName("shutterClose")).getValue();

        KdLogDebug(" >>> Curve Visibility Culling: shutter times: " << shutterOpen <<
                " : " << shutterClose);

        if (std::fabs(shutterOpen - shutterClose) <
            std::numeric_limits<float>::epsilon()) {
            sampleTimes.push_back(shutterOpen);
        } else {
            sampleTimes.insert(sampleTimes.end(), { shutterOpen, shutterClose });
        }

    } else {
        sampleTimes.push_back(0.0f);
    }

    // *** curve geometry ***
    const kodachi::IntAttribute numVertsAttr =
            curveGeometry.getChildByName("numVertices");
    if (!numVertsAttr.isValid()) {
        KdLogWarn(" >>> Curve Visibility Culling: missing numVertices.");
        return {};
    }
    const auto numVertsSamples = numVertsAttr.getSamples();
    const int64_t numCurves = numVertsAttr.getNumberOfTuples();

    kodachi::FloatAttribute curvePointsAttr =
            curveGeometry.getChildByName("point.P");
    if (!curvePointsAttr.isValid()) {
        KdLogWarn(" >>> Curve Visibility Culling: missing point.P");
        return {};
    }

    // interpolate to shutter times
    curvePointsAttr = kodachi::interpToSamples(curvePointsAttr, sampleTimes, 3);
    // transform points to world space
    const kodachi::DoubleAttribute curveXformAttr =
            kodachi::XFormUtil::CalcTransformMatrixAtTimes(curveXformGroup,
                    sampleTimes.data(), sampleTimes.size()).first;
    curvePointsAttr = transformPoints(curvePointsAttr, curveXformAttr);
    const auto curvePointSamples = curvePointsAttr.getSamples();

    // helper vector that points each curve to the correct start index
    // of the points samples list (not factoring tuple size)
    std::vector<int64_t> ptIdxArray(numCurves, 0);
    for (auto i = 1; i < numCurves; ++i) {
        ptIdxArray[i] = ptIdxArray[i-1] + numVertsSamples[0][i-1];
    }

    // *** camera xform ***
    // interpolated to shutter times
    const kodachi::DoubleAttribute viewObjectXformAttr =
            kodachi::XFormUtil::CalcTransformMatrixAtTimes(viewObjectXformGroup,
                    sampleTimes.data(), sampleTimes.size()).first;
    const auto cameraMatrixSamples = viewObjectXformAttr.getSamples();

    // store camera position at each sample time
    std::unordered_map<float, Imath::V3f> camXformPos;
    for (const float t : sampleTimes) {
        Imath::M44d mat;
        kodachi_moonray::setXformMatrix(mat, cameraMatrixSamples.getNearestSample(t).data());
        camXformPos.emplace(t, mat.translation());
    }

    // *** Embree scene ***
    // create and populate embree scene

    KdLogDebug(" >>> CurveVisibilityCull Op: Processing " <<
            meshAttrs.getNumberOfChildren() << " meshes.");

    EmbreeScene embreeScene;
    // populate the scene with obstructor meshes
    for (auto mesh : meshAttrs) {
        const kodachi::GroupAttribute meshAttr(mesh.attribute);
        const kodachi::GroupAttribute meshGeometry =
                meshAttr.getChildByName("geometry");
        const kodachi::GroupAttribute meshXformGroup =
                meshAttr.getChildByName("xform");
        embreeScene.addGeometry(meshGeometry,
                                meshXformGroup,
                                sampleTimes);
    }

    // done with scene population
    embreeScene.commit();

    // *** PROCESS ***
    // whether to keep the curve or not
    // each time sample thread will mark the curve keep list
    // if the curve needs to be kept
    // by default, if no sample thread votes to keep it,
    // we can discard it
    std::vector<int32_t> curveKeepList(numCurves, 0);

    const size_t grainSize = 100;

    // *** time reporting ***
    const auto start = std::chrono::system_clock::now();

    // main chunk of work at each time sample
    // each attribute should be already
    // interpolated to match sample times
    // we check the rays at the sample times (0, 1)
    tbb::blocked_range<int64_t> samplesRange(0, sampleTimes.size(), 1);
    tbb::parallel_for(samplesRange,
    [&](const tbb::blocked_range<int64_t> &tRange) {
    for (int64_t t = tRange.begin();
            t < tRange.end(); ++t) {

        const float time = sampleTimes[t];

        const auto& curvePointAtTimeT =
                curvePointSamples.getNearestSample(time);

        const Imath::V3f& cameraPos = camXformPos[time];

        // *** for each curve
        tbb::blocked_range<int64_t> range(0, numCurves, grainSize);
        tbb::parallel_for(range,
        [&](const tbb::blocked_range<int64_t> &r) {
            // for each curve
            for (int64_t curveIt = r.begin();
                    curveIt < r.end(); ++curveIt) {

                // number of cv's
                const int64_t numCvs = numVertsSamples[0][curveIt];
                // index into curve points
                const int64_t startIdx = ptIdxArray[curveIt];

                // for each curve, trace a ray from the camera to its cv's
                // and test the ray against the scene

                // if any mesh obstructs the curve,
                // we can kill it, otherwise we need to keep it
                if (!isCurveObstructed(embreeScene,
                                       curvePointAtTimeT, startIdx, numCvs,
                                       cameraPos, float(t))) {
                    curveKeepList[curveIt] = 1;
                }

            } // for each curve
        }); // parallel for each curve
    }
    }); // parallel for each time sample

    // *** OUTPUT ***
    std::vector<int32_t> omitList;

    size_t killCount = 0;
    int32_t pIdx = 0;
    for (int64_t curveIt = 0; curveIt < numCurves; ++curveIt) {

        int32_t numCvs = numVertsSamples[0][curveIt];

        if (!curveKeepList[curveIt]) {
            killCount++;
            for (int32_t v = 0; v < numCvs; ++v) {
                 omitList.emplace_back(pIdx);
                 pIdx++;
             }
        } else {
            pIdx += numCvs;
        }
    }

    KdLogDebug(" >>> Curve Visibility Culling: culling " <<
            killCount << " curves, " << omitList.size() << " cv's total.");

    // *** time reporting ***
    const auto end = std::chrono::system_clock::now();
    auto dur = end - start;

    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(dur);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(dur -= minutes);
    const auto millis  = std::chrono::duration_cast<std::chrono::milliseconds>(dur -= seconds);
    const auto micros  = std::chrono::duration_cast<std::chrono::microseconds>(dur -= millis);

    KdLogDebug(" >>> Curve Visibility Culling: "
              << std::setfill('0') << std::setw(2)
              << minutes.count()   << ":" << std::setw(2)
              << seconds.count()   << "." << std::setw(3)
              << millis.count()    << "." << std::setw(3)
              << micros.count()    << " (mm:ss.ms.us)\n");

    sTotalMinutes += minutes.count();
    sTotalSeconds += seconds.count();
    sTotalCurves  += killCount;
    sTotalRuns++;

    KdLogDebug(" >>> Curve Visibility Culling: TOTAL: " << sTotalMinutes << ":"
            << sTotalSeconds << " minutes, " << sTotalCurves << " curves culled.");
    KdLogDebug(" >>> Curve Visibility Culling: Average " <<
            ((static_cast<float>(sTotalMinutes) +
                    (static_cast<float>(sTotalSeconds)/60.f))/sTotalRuns) << " minutes, "
                    << (static_cast<float>(sTotalCurves)/sTotalRuns)
            << " curves culled --- " << sTotalRuns << " total runs.");

    return kodachi::GroupAttribute(
            "omitList", kodachi::ZeroCopyIntAttribute::create(omitList),
            "visibility", kodachi::ZeroCopyIntAttribute::create(curveKeepList),
            false);
}

kodachi::GroupAttribute
visibilityCull(const kodachi::GroupAttribute& iAttr, kodachi::GroupAttribute* iSupportAttrs)
{
    // perform visibility cull
    return visibilityCullInternal(
            iAttr.getChildByName(sKeyName_curve_geometry),
            iAttr.getChildByName(sKeyName_curve_xform),
            iAttr.getChildByName(sKeyName_obstructors),
            iAttr.getChildByName(sKeyName_viewObject_xform),
            iAttr.getChildByName(sKeyName_mb)
            );
}

// Attribute function for performing reduceCurves on arbitrary geometry attribute
// expects input attrs:
//  - curve.geometry
//  - curve.xform
//  - obstructors
//  - viewObject.xform
//  - mb (optional motion blur params containing shutterOpen and shutterClose)
// returns: GroupAttribute containing 'omitList' int attribute
//                                and 'visibility' int attribute (denoting visibility of each curve)
class CurveVisibilityCullAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        kodachi::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            KdLogDebug(" >>> CurveVisibilityCull Attribute Func: Running.");
            return visibilityCullInternal(
                    rootAttr.getChildByName(sKeyName_curve_geometry),
                    rootAttr.getChildByName(sKeyName_curve_xform),
                    rootAttr.getChildByName(sKeyName_obstructors),
                    rootAttr.getChildByName(sKeyName_viewObject_xform),
                    rootAttr.getChildByName(sKeyName_mb)
                    );
        }
        KdLogDebug(" >>> CurveVisibilityCull Attribute Func: Input is invalid.");
        return {};
    }
};


class CurveVisibilityCullOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        {
            static const kodachi::StringAttribute kDefaultCELAttr(
                    R"(/root/world/geo//*{@type=="curves"})");

            kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
            if (!celAttr.isValid()) {
                celAttr = kDefaultCELAttr;
            }

            kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
            kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, celAttr);

            if (!celInfo.canMatchChildren) {
                interface.stopChildTraversal();
            }

            if (!celInfo.matches) {
                return;
            }
        }

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
            KdLogWarn(" >>> CurveVisibilityCull Op: missing 'geometry'.");
            return;
        }

        const kodachi::StringAttribute viewObjectAttr =
                interface.getAttr("curveOperations.visibilityCull.viewObject");
        const kodachi::StringAttribute obstructorsAttr =
                interface.getAttr("curveOperations.visibilityCull.obstructors");
        const kodachi::StringAttribute meshesCelAttr =
                interface.getAttr("curveOperations.visibilityCull.CEL");

        // whether or not to cull the curves
        const bool cull = kodachi::IntAttribute(
                interface.getAttr("curveOperations.visibilityCull.cull")).getValue(true, false);

        interface.deleteAttr("curveOperations.visibilityCull");

        // meshes that can potentially obstruct the curves
        kodachi::GroupBuilder meshesGb;

        // recursively find mesh locations underneath the provided locations
        // and populates meshesGb if it matches the CEL
        std::function<void(const kodachi::StringAttribute&, const kodachi::string_view)>
        findMeshesLambda = [&](const kodachi::StringAttribute& locations,
                               const kodachi::string_view root) {
            if (locations.isValid()) {
                const auto samples = locations.getSamples();

                for (const kodachi::string_view name : samples.front()) {

                    const std::string location = root.empty() ? name.data() :
                                                 kodachi::concat(root, "/", name);

                    if (interface.doesLocationExist(location)) {
                        interface.prefetch(location);

                        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
                        celInfo.matches = true;
                        celInfo.canMatchChildren = true;

                        if (meshesCelAttr.isValid()) {
                            kodachi::CookInterfaceUtils::matchesCEL(
                                    celInfo, interface, meshesCelAttr, location);
                        }

                        kodachi::StringAttribute type =
                                interface.getAttr("type", location);

                        if (celInfo.matches && (type == "subdmesh" || type == "polymesh")) {
                            kodachi::GroupBuilder gb;
                            gb.set("geometry", interface.getAttr("geometry", location));
                            // xform needed to transform points to world space
                            gb.set("xform", kodachi::GetGlobalXFormGroup(interface, location.data()));
                            meshesGb.set(location, gb.build());
                        } else if (celInfo.canMatchChildren) {
                            // recurse on children
                            const kodachi::StringAttribute children =
                                    interface.getPotentialChildren(location);
                            findMeshesLambda(children, location);
                        }
                    }
                } // for each location
            }
        }; // end lambda

        findMeshesLambda(obstructorsAttr, "");

        const std::string viewObject = viewObjectAttr.getValue("", false);
        if (viewObject.empty() ||
                !interface.doesLocationExist(viewObject)) {
            KdLogWarn(" >>> CurveVisibilityCull Op: invalid view object.");
            return;
        }
        interface.prefetch(viewObject);

        // motion blur attrs if needed
        const int numSamples = kodachi::GetNumSamples(interface);
        const float shutterOpen = kodachi::GetShutterOpen(interface);
        const float shutterClose = kodachi::GetShutterClose(interface);
        const bool mbEnabled = (numSamples > 1);

        // invalid if not using motion blur
        kodachi::GroupBuilder mbBuilder;
        mbBuilder.set("enabled", kodachi::IntAttribute(mbEnabled));
        if (mbEnabled) {
            // don't include as key if unneeded
            mbBuilder.set("shutterOpen", kodachi::FloatAttribute(shutterOpen));
            mbBuilder.set("shutterClose", kodachi::FloatAttribute(shutterClose));
        }

        // build the cache key
        kodachi::GroupBuilder keyBuilder;
        keyBuilder.set(sKeyName_curve_geometry,   geometryAttr);
        keyBuilder.set(sKeyName_curve_xform,      kodachi::GetGlobalXFormGroup(interface));
        keyBuilder.set(sKeyName_obstructors,      meshesGb.build());
        keyBuilder.set(sKeyName_viewObject_xform, kodachi::GetGlobalXFormGroup(interface, viewObject));
        keyBuilder.set(sKeyName_mb,               mbBuilder.build());

        // fetch/create cache entry
        if (sCurveVisibilityCachePtr == nullptr) {
            const kodachi::GroupAttribute kodachiCacheSettings =
                    interface.getAttr("kodachi.cache", "/root");

            sCurveVisibilityCachePtr =
                    kodachi::getGroupAttributeCacheInstance<visibilityCull>(
                            kodachiCacheSettings,
                            kCurveVisibilityCullOp);
        }

        // perform visibility cull
        const kodachi::GroupAttribute resultAttr =
                sCurveVisibilityCachePtr->getValue(keyBuilder.build());

        if (!resultAttr.isValid()) {
            return;
        }

        if (cull) {
            std::unordered_set<int32_t> omitList;
            const kodachi::IntAttribute oldOmitListAttr = interface.getAttr("geometry.omitList");
            // if there's an existing omitList, merge those results
            // into a set to avoid unnecessary duplication of values
            if (oldOmitListAttr.getNumberOfValues() > 0) {
                const auto omitListSamples = oldOmitListAttr.getSamples();
                const auto& omitListSample  = omitListSamples.front();
                omitList.insert(omitListSample.begin(), omitListSample.end());
            }

            const kodachi::IntAttribute newOmitListAttr = resultAttr.getChildByName("omitList");
            if (newOmitListAttr.getNumberOfValues() > 0) {
                const auto omitListSamples = newOmitListAttr.getSamples();
                const auto& omitListSample  = omitListSamples.front();
                omitList.insert(omitListSample.begin(), omitListSample.end());
            }

            // *** update new omit list ***
            if (!omitList.empty()) {
                std::vector<int32_t> newOmitList(omitList.begin(), omitList.end());
                interface.setAttr("geometry.omitList", kodachi::ZeroCopyIntAttribute::create(newOmitList));
            }
        }

        // *** visibility attr ***
        // attribute specific to curve visibility indicating whether each curve
        // is occluded or visible
        const kodachi::IntAttribute visibilityAttr = resultAttr.getChildByName("visibility");
        if (visibilityAttr.isValid()) {
            interface.setAttr("geometry.visibility", visibilityAttr);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        const std::string opHelp    = "";
        const std::string opSummary = "";

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {
        if (sCurveVisibilityCachePtr != nullptr) {
            sCurveVisibilityCachePtr->clear(kodachi::Cache::ClearAction::MEMORY);
        }
    }

private:
    static kodachi::GroupAttributeCache<visibilityCull>::Ptr_t sCurveVisibilityCachePtr;
};

kodachi::GroupAttributeCache<visibilityCull>::Ptr_t CurveVisibilityCullOp::sCurveVisibilityCachePtr { nullptr };

DEFINE_KODACHIOP_PLUGIN(CurveVisibilityCullOp)
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(CurveVisibilityCullAttrFunc)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(CurveVisibilityCullOp, "CurveVisibilityCullOp", 0, 1);
    REGISTER_PLUGIN(CurveVisibilityCullAttrFunc, "CurveVisibilityCullAttrFunc", 0, 1);
}


