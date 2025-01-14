// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionPlugin.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/GeometryUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

// Imath
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>

// std
#include <unordered_set>

namespace
{

KdLogSetup("PrimitivePruneOps");

const std::string kIntersect      = "intersect";
const std::string kContainsAll    = "contains all";
const std::string kContainsCenter = "contains center";

const std::string kImmediateExecutionMode = "immediate";
const std::string kDeferredExecutionMode  = "deferred";

// debug
void printGroup(const kodachi::GroupAttribute& inG, int level) {
    if (inG.isValid()) {
        for (int i = 0; i < inG.getNumberOfChildren(); ++i) {
            KdLogDebug(std::string(level*3, '>') << "  " << inG.getChildName(i));
            printGroup(FnKat::GroupAttribute(inG.getChildByIndex(i)), level + 1);
        }
    }
}

template<class ATTR_L, class ATTR_R>
bool
timeSamplesMatch(const ATTR_L& lhs, const ATTR_R& rhs)
{
    const std::int64_t lhsSampleCount = lhs.getNumberOfTimeSamples();
    const std::int64_t rhsSampleCount = rhs.getNumberOfTimeSamples();

    if (lhsSampleCount != rhsSampleCount) {
        return false;
    }

    for (std::int64_t idx = 0; idx < lhsSampleCount; ++idx) {
        if (lhs.getSampleTime(idx) != rhs.getSampleTime(idx)) {
            return false;
        }
    }

    return true;
}

// attempts to match attr and xformB to time samples of xform
// if their time samples do not match, match them to
// the provided sampleTimes
template<class ATTR_T>
inline void
matchSamples(ATTR_T& attr,
             kodachi::DoubleAttribute& xformB,
             const kodachi::GroupAttribute& xformBGroup,
             kodachi::DoubleAttribute& xform,
             const kodachi::GroupAttribute& xformGroup,
             std::vector<float>& sampleTimes)
{
    // if no motion blur, match attr's sample to xform's sample
    if (xform.getNumberOfTimeSamples() == 1 &&
            attr.getNumberOfTimeSamples() == 1 &&
            xformB.getNumberOfTimeSamples() == 1) {
        sampleTimes.clear();

        const float xformTime = xform.getSampleTime(0);
        const float attrTime = attr.getSampleTime(0);

        sampleTimes.emplace_back(xformTime);

        if (xformTime != attrTime) {
            attr = kodachi::interpolateAttr(attr, xformTime);
        }

        const float xformBTime = xformB.getSampleTime(0);
        if (xformTime != xformBTime) {
            attr = kodachi::interpolateAttr(attr, xformTime);
            xformB = kodachi::XFormUtil::CalcTransformMatrixAtTimes(xformBGroup,
                                                              sampleTimes.data(),
                                                              sampleTimes.size()).first;
        }

    } else {
        // At least one has more than 1 time sample

        if (timeSamplesMatch(xform, attr) &&
                timeSamplesMatch(xform, xformB)) {
            // if all samples match up, use them as is
            auto samplesAccessor = xform.getSamples();
            sampleTimes.clear();

            for (const auto& sample : samplesAccessor) {
                sampleTimes.emplace_back(sample.getSampleTime());
            }
        } else {
            // If time samples don't match (different values, or different number of time samples),
            // match them with provided sample times
            xform = kodachi::XFormUtil::CalcTransformMatrixAtTimes(xformGroup,
                                                            sampleTimes.data(),
                                                            sampleTimes.size()).first;
            xformB =
                    kodachi::XFormUtil::CalcTransformMatrixAtTimes(xformBGroup,
                                                            sampleTimes.data(),
                                                            sampleTimes.size()).first;
            using value_t = typename ATTR_T::value_type;
            using array_t = typename std::unique_ptr<value_t[]>;

            const int64_t numValues = attr.getNumberOfValues();

            const ATTR_T attrAttr(attr);
            array_t floatArray(new value_t[numValues * sampleTimes.size()]);
            for (size_t t = 0; t < sampleTimes.size(); ++t) {
                attrAttr.fillInterpSample(floatArray.get() + (t*numValues),
                                          numValues, sampleTimes[t]);
            }
            attr = kodachi::ZeroCopyAttribute<ATTR_T>::create(sampleTimes,
                                                              std::move(floatArray),
                                                              numValues, attr.getTupleSize());
        }
    }
}

// ****************************************************************
// PRIMITIVE PRUNE BY FRUSTUM
// ****************************************************************

// inputArgs:
// frustumPrune
//      - cameraXform               (required)
//      - frustum_vertex_positions  (required)
//      - method                    (default to 'intersect')
//      - invert                    (default to false)
// geometry
//      - point.P                   (required)
//      - numVertices               (required)
// shutterOpen/shutterClose         (required)
// localXformGroup                  (required - local xform of current location)

// returns: GroupAttribute containing 'omitList' int attribute
kodachi::GroupAttribute
primitivePruneCurvesByFrustum(const kodachi::GroupAttribute& geometryArgs,
                              const kodachi::GroupAttribute& frustumArgs,
                              const kodachi::GroupAttribute& localXformGroup,
                              const float shutterOpen,
                              const float shutterClose)
{
    // required attributes
    const kodachi::GroupAttribute cameraXformGroup =
            frustumArgs.getChildByName("cameraXform");
    const kodachi::DoubleAttribute frustumVertices =
            frustumArgs.getChildByName("frustum_vertex_positions");
    kodachi::FloatAttribute pointAttr =
            geometryArgs.getChildByName("point.P");
    const kodachi::IntAttribute numVertsAttr =
            geometryArgs.getChildByName("numVertices");

    if (!cameraXformGroup.isValid() ||
        !frustumVertices.isValid()  ||
        !pointAttr.isValid()        ||
        !numVertsAttr.isValid()) {
        // nothing to do
        KdLogWarn(" >>> Primitive Prune By Frustum: Missing vital attr(s)" <<
                 (cameraXformGroup.isValid() ? "" : " cameraXform") <<
                 (frustumVertices.isValid() ? "" : " frustum_vertex_positions") <<
                 (pointAttr.isValid() ? "" : " point.P") <<
                 (numVertsAttr.isValid() ? "" : " numVertices"));
        return {};
    }

    // prune method
    const kodachi::StringAttribute methodAttr =
            frustumArgs.getChildByName("method");
    if (!methodAttr.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Frustum: Missing 'method' attr," <<
                  " defaulting to 'intersect'.")
    }
    const std::string methodStr = methodAttr.getValue(kIntersect, false);

    // invert flag
    const bool invertMethod =
            kodachi::IntAttribute(frustumArgs.getChildByName("invert")).getValue(0, false);

    // shutter and sample times
    std::vector<float> sampleTimes { shutterOpen, shutterClose };

    // camera xform
    kodachi::DoubleAttribute cameraXformAttr =
            kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(cameraXformGroup).first;

    // local xform group
    if (!localXformGroup.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Frustum: Missing local xform group.");
        return {};
    }
    KdLogDebug(" >>> Primitive Prune By Frustum: local xform -------- ");
    printGroup(localXformGroup, 1);

    kodachi::DoubleAttribute localXformAttr =
            kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(localXformGroup).first;

    // interpolate points and local xform to match sample times of camera xform
    matchSamples(pointAttr,
                 localXformAttr,
                 localXformGroup,
                 cameraXformAttr,
                 cameraXformGroup,
                 sampleTimes);
    if (!cameraXformAttr.isValid() || !localXformAttr.isValid() || !pointAttr.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Frustum: Failure matching samples of point.P and" <<
                  " camera and local xforms.");
        return {};
    }
    const auto& pointSamples = pointAttr.getSamples();

    // output
    std::vector<int32_t> omitList;
    size_t killCount = 0;

    /// Prune by Frustum ///
    if (numVertsAttr.isValid()) {
        const auto& numVertSamples = numVertsAttr.getSamples();
        const auto& numVertSample = numVertSamples.front();

        // which curves to prune out (1 = keep, 0 = prune)
        std::vector<int> keepList(numVertSample.getNumberOfValues(), 0);

        // for each sample
        for (size_t sampleIdx = 0; sampleIdx < sampleTimes.size(); ++sampleIdx) {

            // frustum at this sample
            const Imath::M44d camBBoxXform =
                    kodachi::internal::XFormAttrToIMath(cameraXformAttr, sampleTimes[sampleIdx]) *
                        kodachi::internal::XFormAttrToIMath(localXformAttr, sampleTimes[sampleIdx]).inverse();
            const kodachi::internal::Frustum frustum(frustumVertices, camBBoxXform);

            const auto& points = pointSamples[sampleIdx];
            size_t ptIt = 0; // N-th point

            // loop through the curves
            for (size_t curveIdx = 0; curveIdx < numVertSample.getNumberOfValues(); ++curveIdx) {

                const int32_t numVert = numVertSample[curveIdx];

                if (keepList[curveIdx] == 1) {
                    // at some time sample we've determined we need to keep this curve,
                    // no need to continue testing
                    ptIt += numVert;
                    continue;
                }

                if (methodStr == kIntersect) {
                    // if any cv of the curve is in the frustum, test is true
                    for (int32_t cvIdx = 0; cvIdx < numVert; ++cvIdx) {
                        size_t pIdx = (ptIt + cvIdx) * 3;

                        const Imath::V3f cv(points[pIdx], points[pIdx + 1],
                                points[pIdx + 2]);
                        const bool containsPt = frustum.containsPoint(cv);
                        if (invertMethod != containsPt) {
                            // keep the curve
                            keepList[curveIdx] = 1;
                            break;
                        }
                    }
                } else if (methodStr == kContainsCenter) {
                    // calculate an average of the curve; if the average point
                    // is in the frustum, test is true
                    Imath::V3f avgPt(0.0f, 0.0f, 0.0f);
                    for (int32_t cvIdx = 0; cvIdx < numVert; ++cvIdx) {
                        size_t pIdx = (ptIt + cvIdx) * 3;

                        const Imath::V3f cv(points[pIdx], points[pIdx + 1],
                                points[pIdx + 2]);
                        avgPt += cv;
                    }
                    avgPt /= numVert;
                    const bool containsPt = frustum.containsPoint(avgPt);
                    if (invertMethod != containsPt) {
                        // keep the curve
                        keepList[curveIdx] = 1;
                    }
                } else if (methodStr == kContainsAll) {
                    // all cv's must be in the frustum to be true
                    [&]{
                        for (int32_t cvIdx = 0; cvIdx < numVert; ++cvIdx) {
                            size_t pIdx = (ptIt + cvIdx) * 3;

                            const Imath::V3f cv(points[pIdx], points[pIdx + 1],
                                    points[pIdx + 2]);
                            const bool containsPt = frustum.containsPoint(cv);
                            if (invertMethod == containsPt) {
                                // any cv found to not match the criteria,
                                // remove the curve
                                return;
                            }
                        }
                        // if we reach here without returning, all cv's have matched
                        // the test, keep the curve
                        keepList[curveIdx] = 1;
                    }();
                }
                ptIt += numVert;
            } // numVerts (curves) loop
        } // sample time loop

        size_t ptIt = 0;
        for (size_t curveIdx = 0; curveIdx < numVertSample.getNumberOfValues(); ++curveIdx) {
            const int32_t numVerts = numVertSample[curveIdx];
            if (keepList[curveIdx] == 0) {
                // omit
                killCount++;
                for (int32_t v = 0; v < numVerts; ++v) {
                    omitList.emplace_back(ptIt + v);
                }
            }
            ptIt += numVerts;
        }
    }

    KdLogDebug(" >>> Primitive Prune By Frustum: pruning " << killCount << " curves, " <<
                 omitList.size() << " cv's.");

    // *** OUTPUT ***
    return kodachi::GroupAttribute(
            "omitList", kodachi::ZeroCopyIntAttribute::create(omitList),
            false);
}

// expects input attribute to have
// primitivePrune.frustumPrune
//  - note: this attr func does not evaluate any CEL under frustumPrune, it is up to the
//          caller to do so
// geometry
// xform (local xform of location to be evaluated)
// shutterOpen
// shutterClose
class PrimitivePruneCurvesByFrustumAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        kodachi::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            KdLogDebug(" >>> Primitive Prune By Frustum Attribute Func: Running.");

            const kodachi::GroupAttribute frustumPrune =
                    rootAttr.getChildByName("primitivePrune.frustumPrune");
            if (!frustumPrune.isValid()) {
                KdLogDebug(" >>> Primitive Prune By Frustum Attribute Func: missing 'frustumPrune' attrs.");
                return {};
            }

            const kodachi::GroupAttribute geometryAttr = rootAttr.getChildByName("geometry");
            if (!geometryAttr.isValid()) {
                KdLogWarn(" >>> Primitive Prune By Frustum Attribute Func: missing 'geometry'.");
                return {};
            }

            const kodachi::GroupAttribute localXformGroup = rootAttr.getChildByName("xform");
            if (!localXformGroup.isValid()) {
                KdLogDebug(" >>> Primitive Prune By Frustum Attribute Func: missing local xform group.");
                return {};
            }

            const kodachi::FloatAttribute shutterOpen  = rootAttr.getChildByName("shutterOpen");
            const kodachi::FloatAttribute shutterClose = rootAttr.getChildByName("shutterClose");
            if (!shutterOpen.isValid() || !shutterClose.isValid()) {
                KdLogDebug(" >>> Primitive Prune By Frustum Attribute Func: missing shutter values.");
                return {};
            }

            return primitivePruneCurvesByFrustum(geometryAttr, frustumPrune, localXformGroup,
                    shutterOpen.getValue(), shutterClose.getValue());
        }
        KdLogDebug(" >>> Primitive Prune By Frustum Attribute Func: Input is invalid.");
        return {};
    }
};

// ****************************************************************
// PRIMITIVE PRUNE BY FRUSTUM MAIN OP
// ****************************************************************

class PrimitivePruneByFrustumOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::GroupAttribute frustumPrune =
                kodachi::GetGlobalAttr(interface, "primitivePrune.frustumPrune");

        if (frustumPrune.isValid()) {
            const kodachi::StringAttribute celAttr = frustumPrune.getChildByName("CEL");
            // If CEL not specified, do nothing.
            if (celAttr.isValid()) {
                kodachi::CookInterfaceUtils::MatchesCELInfo info;
                kodachi::CookInterfaceUtils::matchesCEL(info, interface, celAttr);
                if (!info.canMatchChildren) {
                    interface.stopChildTraversal();
                }

                if (!info.matches) {
                    return;
                }

                const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
                if (!geometryAttr.isValid()) {
                    return;
                }

                const float shutterOpen  = kodachi::GetShutterOpen(interface);
                const float shutterClose = kodachi::GetShutterClose(interface);

                const kodachi::GroupAttribute localXformGroup = kodachi::GetGlobalXFormGroup(interface);
                if (!localXformGroup.isValid()) {
                    KdLogDebug(" >>> Primitive Prune By Frustum Op: missing local xform group.");
                    return;
                }

                kodachi::GroupAttribute resultGroupAttr;

                const kodachi::StringAttribute inputType =
                        interface.getAttr("type");
                if (inputType == "curves") {
                    KdLogDebug(" >>> Primitive Prune By Frustum Op: Running Prune Curves by Frustum.");
                    resultGroupAttr =
                            primitivePruneCurvesByFrustum(geometryAttr, frustumPrune, localXformGroup,
                                                          shutterOpen, shutterClose);
                } else if (inputType == "instance array") {
                    // TODO
                } else if (inputType == "pointcloud") {
                    // TODO
                }

                if (!resultGroupAttr.isValid()) {
                    return;
                }

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
            } // if cel valid
        } // if frustumPrune valid
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

    }
};

// ****************************************************************
// PRIMITIVE PRUNE BY VOLUME
// ****************************************************************

// curveGeometry
//      - point.P       (required)
//      - numVertices   (required)
// xform                (required - curves location global xform group)
// pruneVolumeArgs
//      - geometry      (required)
//          - point.P
//          - poly.vertexList
//          - poly.startIndex
//      - xform         (required - prune volume location global xform group)
//      - invert        (defaults to false)
//      - bound         (optional - allows for a bounds test first if the prune volume is too complicated)

// returns: GroupAttribute containing 'omitList' int attribute
kodachi::GroupAttribute
primitivePruneCurvesByVolume(const kodachi::GroupAttribute& curveGeometry,
                             const kodachi::GroupAttribute& curveXform,
                             const kodachi::GroupAttribute& pruneVolumeArgs)
{
    if (!pruneVolumeArgs.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Volume: Missing prune volume attrs.");
        return {};
    }

    if (!curveXform.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Volume: Missing geometry xform.");
        return {};
    }

    const kodachi::FloatAttribute pointAttr = curveGeometry.getChildByName("point.P");
    const kodachi::IntAttribute numVertsAttr = curveGeometry.getChildByName("numVertices");

    if (!pointAttr.isValid() || !numVertsAttr.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Volume: Missing geometry attrs.");
        return {};
    }

    // prune volume geometry
    const kodachi::GroupAttribute pruneVolumeGeometry =
            pruneVolumeArgs.getChildByName("geometry");
    // prune volume global xform group
    const kodachi::GroupAttribute pruneVolumeXform =
            pruneVolumeArgs.getChildByName("xform");
    if (!pruneVolumeGeometry.isValid() ||
            !pruneVolumeXform.isValid()) {
        KdLogWarn(" >>> Primitive Prune By Volume: invalid prune volume.");
        return {};
    }

    kodachi::internal::Mesh pruneMesh;
    kodachi::internal::Mesh pruneBoundMesh;

    // mesh information of the prune volume
    if (kodachi::internal::GetTransformedMesh(
            pruneMesh, pruneVolumeGeometry, pruneVolumeXform)) {

        // transform prune mesh into object space of the curve geometry
        const kodachi::DoubleAttribute xformAttr = kodachi::RemoveTimeSamplesIfAllSame(
             kodachi::RemoveTimeSamplesUnneededForShutter(
                 kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                     curveXform).first, 0, 0));
        Imath::M44d xform = kodachi::internal::XFormAttrToIMath(xformAttr, 0.0f);
        xform.invert();
        pruneMesh.transformMesh(xform);

        const bool invert = kodachi::IntAttribute(
                pruneVolumeArgs.getChildByName("invert")).getValue(false, false);

        // If our prune volume has more than 6 faces, do a bound test first
        const bool usePruneBoundMesh =
                (pruneMesh.faceCount() > 6 &&
                        kodachi::internal::GetTransformedBoundAsMesh(
                                pruneBoundMesh, pruneVolumeArgs.getChildByName("bound"), pruneVolumeXform));
        pruneBoundMesh.transformMesh(xform);

        // output
        std::vector<int32_t> omitList;

        const auto numVertSamples = numVertsAttr.getSamples();
        const auto& numVertSample = numVertSamples.getNearestSample(0.0f);

        const auto pointSamples = pointAttr.getSamples();
        const auto& pointSample = pointSamples.getNearestSample(0.0f);

        // convert to V3f vector and transform to world space
        const Imath::V3f* rawPoints =
                reinterpret_cast<const Imath::V3f*>(pointSample.data());
        std::vector<Imath::V3f> points(
                rawPoints, rawPoints + (pointAttr.getNumberOfTuples()));

        // loop through the curves and check it with the prune mesh
        size_t it = 0;
        size_t killCount = 0;
        for (const int32_t numVert : numVertSample) {
            bool boundIntersects = true;
            bool intersects = false;

            const kodachi::array_view<Imath::V3f> curve(
                    points.data() + it, numVert);

            if (usePruneBoundMesh) {
                boundIntersects = pruneBoundMesh.doesIntersect(curve);
            }

            if (boundIntersects) {
                intersects = pruneMesh.doesIntersect(curve);
            }

            if (intersects != invert) {
                // omit
                killCount++;
                for (int32_t v = 0; v < numVert; ++v) {
                    omitList.emplace_back(it + v);
                }
            }

            it += numVert;
        } // num vert loop

        // *** OUTPUT ***
        KdLogDebug(" >>> Primitive Prune By Volume: pruning " << killCount << " curves, " <<
                omitList.size() << " cv's.");
        return kodachi::GroupAttribute(
                "omitList", kodachi::ZeroCopyIntAttribute::create(omitList),
                false);

    } // do prune

    KdLogWarn(" >>> Primitive Prune By Volume: invalid prune volume.");
    return {};
}

class PrimitivePruneCurvesByVolumeAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        kodachi::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            KdLogDebug(" >>> Primitive Prune By Volume Attribute Func: Running.");

            const kodachi::GroupAttribute volumePrune =
                    rootAttr.getChildByName("primitivePrune.volumePrune");
            if (!volumePrune.isValid()) {
                KdLogDebug(" >>> Primitive Prune By Volume Attribute Func: missing 'volumePrune' attrs.");
                return {};
            }

            const kodachi::GroupAttribute geometryAttr = rootAttr.getChildByName("geometry");
            if (!geometryAttr.isValid()) {
                KdLogWarn(" >>> Primitive Prune By Volume Attribute Func: missing 'geometry'.");
                return {};
            }

            const kodachi::GroupAttribute localXformGroup = rootAttr.getChildByName("xform");
            if (!localXformGroup.isValid()) {
                KdLogDebug(" >>> Primitive Prune By Volume Attribute Func: missing local xform group.");
                return {};
            }

            return primitivePruneCurvesByVolume(
                    geometryAttr, localXformGroup, volumePrune);
        }
        KdLogDebug(" >>> Primitive Prune By Volume Attribute Func: Input is invalid.");
        return {};
    }
};

// ****************************************************************
// PRIMITIVE PRUNE BY VOLUME MAIN OP
// ****************************************************************

class PrimitivePruneByVolumeOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::GroupAttribute volumePrune =
                kodachi::GetGlobalAttr(interface, "primitivePrune.volumePrune");

        if (volumePrune.isValid()) {
            const kodachi::StringAttribute celAttr = volumePrune.getChildByName("CEL");
            // If CEL not specified, do nothing.
            if (celAttr.isValid()) {
                kodachi::CookInterfaceUtils::MatchesCELInfo info;
                kodachi::CookInterfaceUtils::matchesCEL(info, interface, celAttr);
                if (!info.canMatchChildren) {
                    interface.stopChildTraversal();
                }

                if (!info.matches) {
                    return;
                }

                const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
                if (!geometryAttr.isValid()) {
                    return;
                }

                const kodachi::GroupAttribute localXformGroup = kodachi::GetGlobalXFormGroup(interface);
                if (!localXformGroup.isValid()) {
                    KdLogDebug(" >>> Primitive Prune By Frustum Op: missing local xform group.");
                    return;
                }

                kodachi::GroupAttribute resultGroupAttr;

                const kodachi::StringAttribute inputType =
                        interface.getAttr("type");
                if (inputType == "curves") {
                    resultGroupAttr =
                            primitivePruneCurvesByVolume(geometryAttr,
                                    localXformGroup, volumePrune);
                } else if (inputType == "instance array") {
                    // TODO
                } else if (inputType == "pointcloud") {
                    // TODO
                }

                if (!resultGroupAttr.isValid()) {
                    return;
                }

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
            } // if cel valid
        } // if volumePrune valid
    }
};

DEFINE_KODACHIOP_PLUGIN(PrimitivePruneByFrustumOp)
DEFINE_KODACHIOP_PLUGIN(PrimitivePruneByVolumeOp)
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(PrimitivePruneCurvesByFrustumAttrFunc)
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(PrimitivePruneCurvesByVolumeAttrFunc)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(PrimitivePruneByFrustumOp, "PrimitivePruneByFrustumOp", 0, 1);
    REGISTER_PLUGIN(PrimitivePruneByVolumeOp, "PrimitivePruneByVolumeOp", 0, 1);
    REGISTER_PLUGIN(PrimitivePruneCurvesByFrustumAttrFunc, "PrimitivePruneCurvesByFrustumAttrFunc", 0, 1);
    REGISTER_PLUGIN(PrimitivePruneCurvesByVolumeAttrFunc, "PrimitivePruneCurvesByVolumeAttrFunc", 0, 1);
}


