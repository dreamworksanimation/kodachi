// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// kodachi
#include <kodachi/attribute/ArbitraryAttribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionPlugin.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>

#include <kodachi/cache/GroupAttributeCache.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

// std
#include <chrono>
#include <iomanip>
#include <algorithm>

// tbb
#include <tbb/parallel_for.h>

// Imath
#include <OpenEXR/ImathVec.h>

namespace
{

const std::string kCurveReductionOp { "CurveReductionOp" };

KdLogSetup(kCurveReductionOp);

enum SimplificationMode {
    VISVALINGAM     = 0,  // percent
    DOUGLAS_PEUCKER = 1   // distance
};

// squared distance of point to line
// given a point (P) and start and end points to a line (AB)
float
distanceSq(const Imath::V3f& P,
           const Imath::V3f& A,
           const Imath::V3f& B)
{
    const Imath::V3f line = B - A;
    const Imath::V3f AP   = P - A;

    const float lenLine = line.length2();

    float dot = AP.dot(line);

    if (dot <= 0) {
        // point is behind start point,
        // simply use the distance of A -> P
        return AP.length2();
    } else if (dot >= lenLine) {
        // point is beyond end point,
        // simply use distance of B -> P
        const Imath::V3f BP = P - B;
        return BP.length2();
    } else {
        const Imath::V3f BP = P - B;

        Imath::V3f APxBP = AP.cross(BP);
        return APxBP.length2() / line.length2();
    }
}

// Douglas-Peucker algorithm
// calculates distance between an estimation of the curve (start -> end)
// if distance is beyond a tolerance value of epsilon, we need to keep
// that point, and recurse between the sub-curves start -> point -> end
// otherwise, we can discard the points within epsilon because they
// are close enough to the approximation
// eps - expected squared value of tolerance value
void
decimateDouglasPeucker(const std::vector<Imath::V3f>& PV,
                       const int64_t pointStartIdx,
                       const float eps,
                       std::vector<int>& outPointsKillList,
                       int64_t start,
                       int64_t end)
{
    // by default keep the start and end points
    outPointsKillList[pointStartIdx + start] = 1;
    outPointsKillList[pointStartIdx + end  ] = 1;

    // nothing to decimate
    if (end <= (start + 1)) {
        return;
    }

    // max distance, squared
    // and the index of the furthest point
    float maxDsq = 0;
    int   maxIdx = 0;

    for (auto i = (start + 1); i < end; ++i) {
        // squared distance
        const float dsq = distanceSq(PV[i], PV[start], PV[end]);
        if (dsq > maxDsq) {
            maxDsq = dsq;
            maxIdx = i;
        }
    }

    if (maxDsq > eps) {
        // distance is greater than tolerance, we need to keep this point
        // and recurse for the line segments of start -> index -> end
        decimateDouglasPeucker(PV, pointStartIdx, eps,
                               outPointsKillList, start, maxIdx);
        decimateDouglasPeucker(PV, pointStartIdx, eps,
                               outPointsKillList, maxIdx, end);
    }
}

// calculates area of triangle APB
float
areaTriangle(const Imath::V3f& P,
             const Imath::V3f& A,
             const Imath::V3f& B)
{
    const Imath::V3f AP = A-P;
    const Imath::V3f BP = B-P;

    return AP.cross(BP).length() * 0.5f;
}

struct
VisvalingamData {
    VisvalingamData(const float area,
                    const size_t cvIndex,
                    const size_t preIndex,
                    const size_t nextIndex)
    : mArea(area)
    , mCvIndex(cvIndex)
    , mPreIndex(preIndex)
    , mNextIndex(nextIndex)
    { }

    float mArea;       // area of the associated triangle
    size_t mCvIndex;   // index of current cv
    size_t mPreIndex;  // index of previous neighbor
    size_t mNextIndex; // index of next neighbor

    bool
    operator < (const VisvalingamData& other) const
    {
        return mArea < other.mArea;
    }
};

// Visvalingam algorithm
// calculates areas for triangles associated with each cv, ie.
//               P
//             /   \
//           P-1  P+1
// remove cv's one by one with the smallest triangle area
// until the desired resolution is achieved
void
decimateVisvalingam(const std::vector<Imath::V3f>& PV,
               const int64_t pointStartIdx,
               const float simplification,
               const int32_t minCvCount,
               std::vector<int>& outPointsKillList)
{
    // number of cv's to remove
    const size_t iterations =
            std::ceil((PV.size() - minCvCount) * simplification);

    std::vector<VisvalingamData> data;
    data.reserve(iterations);

    // ignoring the end cv's,
    // calculate the areas of the associated triangle of each cv
    for (size_t i = 1; i < PV.size() - 1; ++i) {
        data.emplace_back(
                areaTriangle(PV[i], PV[i-1], PV[i+1]),
                i, i-1, i+1);
    }

    for (size_t i = 0; i < iterations; ++i) {
        const auto currCvIt =
                std::min_element(std::begin(data), std::end(data));
        const auto idx = std::distance(data.begin(), currCvIt);

        // remove smallest area cv
        outPointsKillList[pointStartIdx + currCvIt->mCvIndex] = 0;

        // update areas and pre/next pointers of neighboring cv's
        const auto preIdx = idx - 1;
        if (preIdx >= 0) {
            VisvalingamData& preCv = data[preIdx];
            preCv.mNextIndex = currCvIt->mNextIndex;
            preCv.mArea = areaTriangle(PV[preCv.mCvIndex],
                                       PV[preCv.mPreIndex],
                                       PV[preCv.mNextIndex]);
        }
        const auto nextIdx = idx + 1;
        if (nextIdx < data.size()) {
            VisvalingamData& nextCv = data[nextIdx];
            nextCv.mPreIndex = currCvIt->mPreIndex;
            nextCv.mArea = areaTriangle(PV[nextCv.mCvIndex],
                                        PV[nextCv.mPreIndex],
                                        PV[nextCv.mNextIndex]);
        }

        data.erase(currCvIt);
    }
}

kodachi::GroupAttribute
reduceCurves(const kodachi::GroupAttribute& iAttr, kodachi::GroupAttribute* iSupportAttrs)
{
    const kodachi::GroupAttribute inputArgs(iAttr);

    // *** retrieve attributes ***

    float simplification =
            kodachi::FloatAttribute(inputArgs.getChildByName("simplification")).getValue(0.0f, false);
    KdLogDebug(" >>> Curve Reduction: " << simplification);
    // too close to zero to do anything
    if (simplification < std::numeric_limits<float>::epsilon()) {
        KdLogDebug(" >>> Curve Reduction: 'simplification' attr is zero or invalid.");
        return {};
    }

    const kodachi::StringAttribute simplificationMode =
            kodachi::StringAttribute(inputArgs.getChildByName("simplificationMode"));
    KdLogDebug(" >>> Curve Reduction: " << simplificationMode.getValueCStr("percent", false));

    // defaults to percent based (visvaligam)
    const SimplificationMode mode =
            (simplificationMode == "distance") ? DOUGLAS_PEUCKER : VISVALINGAM;

    // minimum cv count curves can be reduced to
    // can't go under 2
    const int32_t minCvCount =
            std::max(2, kodachi::IntAttribute(inputArgs.getChildByName("minCv")).getValue(4, false));

    // *** points ***
    const kodachi::FloatAttribute pointsAttr = inputArgs.getChildByName("point.P");
    if (!pointsAttr.isValid()) {
        KdLogWarn(" >>> Curve Reduction Failure: missing point.P");
        return { };
    }
    const auto pointsSamples = pointsAttr.getSamples();
    const int64_t tupleSize  = pointsAttr.getTupleSize(); // 3

    // *** num vertices (per curve CV's)
    const kodachi::IntAttribute numVertsAttr = inputArgs.getChildByName("numVertices");
    if (!numVertsAttr.isValid()) {
        KdLogWarn(" >>> Curve Reduction Failure: missing numVertices");
        return { };
    }
    // num vertices (per curve)
    const auto numVertsSamples = numVertsAttr.getSamples();
    const int64_t numCurves    = numVertsAttr.getNumberOfTuples();

    // *** BEGIN PROCESSING ***
    const int64_t numSamples = pointsSamples.getNumberOfTimeSamples();

    // helper vector that points each curve to the correct start index
    // of the points samples list (not factoring tuple size)
    std::vector<int64_t> ptIdxArray(numCurves, 0);
    for (auto i = 1; i < numCurves; ++i) {
        ptIdxArray[i] = ptIdxArray[i-1] + numVertsSamples[0][i-1];
    }

    // result vector:
    // whether to keep (true) or kill (false) the point
    // since we want the point count to be the same across samples
    // this is only a single sample
    // this is not thread protected because theoretically there should not be any
    // overlap of points between the curves, so no two threads should be able to access
    // the same value
    std::vector<int> pointsKillList(pointsAttr.getNumberOfTuples(), 1);
    if (mode == DOUGLAS_PEUCKER) {
        std::fill(pointsKillList.begin(), pointsKillList.end(), 0);
    } else if (mode == VISVALINGAM) {
        simplification = std::min(simplification, 1.0f);
    }

    // CPU and time testing for grain size
    // num curves: 52 num verts per curve: 31 total points: 1612
    //                    avg per curve CPU       total time (us)
    // grain size 1           ~9000~14000           ~132~178
    // grain size 10          ~9000~14000           ~139~211
    // grain size 25          ~10000~12000          ~138~152
    // grain size 100         ~9000~10000           ~191~217

    // num curves: 48389 num verts per curve: 16 total points: 774224
    //                    avg per curve CPU       total time (ms)
    // grain size 1           ~7500~8000            ~9.0~9.6
    // grain size 2           ~7500~7600            ~9.2~9.4
    // grain size 10          ~7500~8000            ~9.1~9.6
    // grain size 50          ~7200~7700            ~9.0~9.4
    // grain size 100         ~7000~8000            ~9.3~9.7
    // grain size 1000        ~7000~7800            ~9.7~9.9
    // grain size 10000       ~5000~5200            ~11.7~12
    // grain size 50000       ~4000~4200            ~69~70

    // there isn't a significant difference between grain sizes,
    // setting constant of 1000 for now
    const size_t grainSize = 1000;
    KdLogDebug(" >>> Start Reduction Processing - using grain size: " << grainSize);

    // process each curve
    // TODO: we can ignore curves that are marked in a kill list
    tbb::blocked_range<int64_t> range(0, numCurves, grainSize);
    tbb::parallel_for(range, [&](const tbb::blocked_range<int64_t> &r) {
        for (int64_t c = r.begin(); c < r.end(); ++c) {

            const int64_t numPoints = numVertsSamples[0][c];

            const int64_t ptIdx = ptIdxArray[c];

            switch(mode) {
            case DOUGLAS_PEUCKER:
            {
                // if a cv should be removed at any sample,
                // it is removed
                for (auto t = 0; t < numSamples; ++t) {
                    std::vector<Imath::V3f> curveV3f;
                    curveV3f.reserve(numPoints);
                    for (auto i = 0; i < numPoints; ++i) {
                        const int idx = (ptIdx + i) * tupleSize;
                        curveV3f.emplace_back(pointsSamples[t][idx],
                                              pointsSamples[t][idx + 1],
                                              pointsSamples[t][idx + 2]);
                    }

                    decimateDouglasPeucker(curveV3f, ptIdx, simplification,
                            pointsKillList, 0, numPoints - 1);

                } // time sample loop
            }
            break;
            case VISVALINGAM:
            default:
            {
                if (minCvCount >= numPoints) {
                    // can't reduce, nothing to do
                    continue;
                }

                // different samples may determine which cv's should be
                // removed differently, so we just use the first time sample
                std::vector<Imath::V3f> curveV3f;
                curveV3f.reserve(numPoints);
                for (auto i = 0; i < numPoints; ++i) {
                    const int idx = (ptIdx + i) * tupleSize;
                    curveV3f.emplace_back(pointsSamples[0][idx],
                                          pointsSamples[0][idx + 1],
                                          pointsSamples[0][idx + 2]);
                }
                decimateVisvalingam(curveV3f, ptIdx, simplification,
                        minCvCount, pointsKillList);
            }
            break;
            }
        }
    }); // parallel for

    // *** OUTPUT ***
    std::vector<int32_t> outOmitList;
    for (int32_t i = 0; i < pointsKillList.size(); ++i) {
        if (!pointsKillList[i]) {
            outOmitList.emplace_back(i);
        }
    }

    KdLogDebug(" >>> Curve Reduction: culling " << outOmitList.size() << " cv's.");

    return kodachi::GroupAttribute(
            "omitList", kodachi::ZeroCopyIntAttribute::create(outOmitList),
            false);
}

// Attribute function for performing reduceCurves on arbitrary geometry attribute
// expects input attrs:
//  - simplification
//  - simplificationMode (default: "percent")
//  - minCv (default: 4)
//  - point.P
//  - numVertices
// returns: GroupAttribute containing 'omitList' int attribute
class CurveReductionAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        kodachi::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            KdLogDebug(" >>> Curve Reduction Attribute Func: Running reduce curves.");
            return reduceCurves(rootAttr, nullptr);
        }
        KdLogDebug(" >>> Curve Reduction Attribute Func: Input is invalid.");
        return {};
    }
};

class CurveReductionOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const std::string kCurves("curves");
        if (kodachi::StringAttribute(interface.getAttr("type")) != kCurves) {
            return;
        }

        // distance unit that drives curve reduction
        // 0 is no reduction where as the bigger value the greater the reduction
        const kodachi::FloatAttribute simplificationAttr =
                                          kodachi::GetGlobalAttr(interface, "curveOperations.simplification");
        if (!simplificationAttr.isValid()) {
            return;
        }

        // since simplification is retrieved globally, reset the value at this location
        interface.setAttr("curveOperations.simplification", kodachi::FloatAttribute(0.0f));

        const float simplification = simplificationAttr.getValue();
        // too close to zero to do anything
        // return here to avoid triggering cache
        if (simplification < std::numeric_limits<float>::epsilon()) {
            return;
        }

        const kodachi::StringAttribute simplificationMode =
                kodachi::GetGlobalAttr(interface, "curveOperations.simplificationMode");
        const kodachi::IntAttribute minCvAttr =
                kodachi::GetGlobalAttr(interface, "curveOperations.minCv");

        // *** Key for cache ***
        kodachi::GroupBuilder keyBuilder;
        keyBuilder.set("simplification", simplificationAttr);
        keyBuilder.set("simplificationMode", simplificationMode);
        keyBuilder.set("minCv", minCvAttr);

        // *** Geometry Attribute ***
        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        // points
        const kodachi::FloatAttribute pointsAttr = geometryAttr.getChildByName("point.P");
        keyBuilder.set("point.P", pointsAttr);
        // num vertices (per curve)
        const kodachi::IntAttribute numVertsAttr = geometryAttr.getChildByName("numVertices");
        keyBuilder.set("numVertices", numVertsAttr);

        const auto start = std::chrono::system_clock::now();

        // *** fetch/create cache entry ***
        // triggers reduceCurves
        if (sCurveReducedCachePtr == nullptr) {
            const kodachi::GroupAttribute kodachiCacheSettings =
                    interface.getAttr("kodachi.cache", "/root");

            sCurveReducedCachePtr =
                    kodachi::getGroupAttributeCacheInstance<reduceCurves>(kodachiCacheSettings,
                                                                          kCurveReductionOp);
        }

        KdLogDebug(" >>> Curve Reduction Op: Running reduce curves.");

        const kodachi::GroupAttribute resultGroupAttr =
                sCurveReducedCachePtr->getValue(keyBuilder.build());

        if (!resultGroupAttr.isValid()) {
            return;
        }

        // *** time reporting ***
        const auto end = std::chrono::system_clock::now();
        auto dur = end - start;

        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(dur);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(dur -= minutes);
        const auto millis  = std::chrono::duration_cast<std::chrono::milliseconds>(dur -= seconds);
        const auto micros  = std::chrono::duration_cast<std::chrono::microseconds>(dur -= millis);

        KdLogDebug(" >>> Processing time: "
                  << std::setfill('0') << std::setw(2)
                  << minutes.count()   << ":" << std::setw(2)
                  << seconds.count()   << "." << std::setw(3)
                  << millis.count()    << "." << std::setw(3)
                  << micros.count()    << " (mm:ss.ms.us)\n");

        std::unordered_set<int32_t> omitList;
        const kodachi::IntAttribute oldOmitListAttr = interface.getAttr("geometry.omitList");
        // if there's an existing omitList, merge those results
        // into a set to avoid unnecessary duplication of values
        if (oldOmitListAttr.getNumberOfValues() > 0) {
            const auto omitListSamples = oldOmitListAttr.getSamples();
            const auto& omitListSample  = omitListSamples.front();
            omitList.insert(omitListSample.begin(), omitListSample.end());
        }

        const kodachi::IntAttribute newOmitListAttr = resultGroupAttr.getChildByName("omitList");
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

        interface.setAttr("kodachi.parallelTraversal", kodachi::IntAttribute(0));
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
        if (sCurveReducedCachePtr != nullptr) {
            sCurveReducedCachePtr->clear(kodachi::Cache::ClearAction::MEMORY);
        }
    }

private:
    static kodachi::GroupAttributeCache<reduceCurves>::Ptr_t sCurveReducedCachePtr;
};

kodachi::GroupAttributeCache<reduceCurves>::Ptr_t CurveReductionOp::sCurveReducedCachePtr { nullptr };

DEFINE_KODACHIOP_PLUGIN(CurveReductionOp);
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(CurveReductionAttrFunc)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(CurveReductionOp, "CurveReductionOp", 0, 1);
    REGISTER_PLUGIN(CurveReductionAttrFunc, "CurveReductionAttrFunc", 0, 1);
}


