// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/GeometryUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

// OpenEXR
#include <OpenEXR/ImathLine.h>
#include <OpenEXR/ImathNamespace.h>
#include <OpenEXR/ImathPlane.h>
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathBox.h>
#include <OpenEXR/ImathMatrix.h>

namespace //anonymous
{

//------------------------------------------------
//
//------------------------------------------------

#define DEBUGMSG(x) do { \
  if (false) { std::cerr << x << std::endl; } \
} while (0)

#undef COOKDEBUG
#define COOKDEBUG(x) do { \
    if (false) { std::cout << interface.getOpType() << ": " << x << std::endl; } \
} while (0)

//------------------------------------------------
//
//------------------------------------------------

const std::string kIntersect      = "intersect";
const std::string kContainsAll    = "contains all";
const std::string kContainsCenter = "contains center";

const std::string kImmediateExecutionMode = "immediate";
const std::string kDeferredExecutionMode  = "deferred";

//------------------------------------------------
//
//------------------------------------------------

inline bool
timeSamplesMatch(const kodachi::DoubleAttribute& lhs, const kodachi::DoubleAttribute& rhs)
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

bool
timeSamplesMatchShutterTimes(const kodachi::DoubleAttribute& lhs,
                             const kodachi::DoubleAttribute& rhs,
                             float shutterOpen,
                             float shutterClose)
{
    if (lhs.getSampleTime(0) != shutterOpen ||
            rhs.getSampleTime(0) != shutterOpen) {
        return false;
    }

    if (lhs.getSampleTime(1) != shutterClose ||
            rhs.getSampleTime(1) != shutterClose) {
        return false;
    }

    return true;
}

//------------------------------------------------
//
//------------------------------------------------

class PruneByFrustumOp : public kodachi::Op
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        // If CEL not specified, do nothing.
        if (!celAttr.isValid()) {
            COOKDEBUG("Invalid CEL");
            return;
        }

        kodachi::CookInterfaceUtils::MatchesCELInfo info;
        kodachi::CookInterfaceUtils::matchesCEL(info, interface, celAttr);
        if (!info.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!info.matches) {
            return;
        }

        const kodachi::StringAttribute methodAttr = interface.getOpArg("method");
        std::string methodStr;
        if (methodAttr.isValid()) {
            methodStr = methodAttr.getValue("", false);
            if (methodStr.empty()) {
                COOKDEBUG("Invalid method chosen");
                return;
            }
        } else {
            COOKDEBUG("Missing method attribute.");
            return;
        }

        const kodachi::IntAttribute invertAttr = interface.getOpArg("invert");
        const bool invertMethod = invertAttr.getValue(0, false);

        const kodachi::StringAttribute executionModeAttr = interface.getOpArg("executionMode");
        if (!executionModeAttr.isValid()) {
            return;
        }

        const std::string executionMode = executionModeAttr.getValue();

        const float padding =
                kodachi::FloatAttribute(interface.getOpArg("padding")).getValue(0.0f, false);

        //----------------------------------------------------
        // find camera properties:

        const FnAttribute::StringAttribute camLocationAttr = interface.getOpArg("cameraLocation");
        const std::string camLocation = camLocationAttr.getValue("", false);

        if (camLocation.empty() ||
                !interface.doesLocationExist(camLocation)) {
            // Nothing to do... can't create a frustum!
            return;
        }

        interface.prefetch(camLocation);

        const float shutterOpenTime  = kodachi::GetShutterOpen(interface);
        const float shutterCloseTime = kodachi::GetShutterClose(interface);

        std::vector<float> sampleTimes;

        //----------------------------------------------------

        const std::string inputLocation = interface.getInputLocationPath();

        // CAMERA XFORM
        const kodachi::GroupAttribute cameraXformGroup = kodachi::GetGlobalXFormGroup(interface, camLocation);
        if (!cameraXformGroup.isValid()) {
            return;
        }
        kodachi::DoubleAttribute cameraXformAttr =
                kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(cameraXformGroup).first;

        // FRUSTUM ----------------------------------------------------
        // Try to reuse the previously calculated frustum vertices by passing it as
        // an op argument to child locations.
        FnAttribute::DoubleAttribute frustumVertices = interface.getOpArg("frustum_vertex_positions");
        if (!frustumVertices.isValid()) {
            const kodachi::GroupAttribute cameraAttrs = interface.getAttr("geometry", camLocation);
            frustumVertices =
                    kodachi::internal::Frustum::calculateFrustumVertices(cameraAttrs,
                                                                         padding);

            FnAttribute::GroupBuilder gb;
            gb.set("frustum_vertex_positions", frustumVertices);
            const FnAttribute::GroupAttribute opArgsAttr = interface.getOpArg("");
            gb.deepUpdate(opArgsAttr);

            // Pass down frustum vertex coordinates to child locations.
            interface.replaceChildTraversalOp("", gb.build());
        }

        // *** primitive pruning ***
        // Points attr can be used for further pruning after bounds testing for curves,
        // points, and instance arrays
        bool prunePrims =
                kodachi::IntAttribute(interface.getOpArg("prune_primitives")).getValue(false, false);
        if (prunePrims) {
            interface.setAttr("primitivePrune.frustumPrune.CEL", celAttr);
            interface.setAttr("primitivePrune.frustumPrune.cameraXform", cameraXformGroup);
            interface.setAttr("primitivePrune.frustumPrune.method", methodAttr);
            interface.setAttr("primitivePrune.frustumPrune.invert", invertAttr);
            interface.setAttr("primitivePrune.frustumPrune.frustum_vertex_positions", frustumVertices);
        }

        // BBOX
        const kodachi::DoubleAttribute bboxAttr = interface.getAttr("bound", inputLocation);

        // BBOX XFORM
        const kodachi::GroupAttribute bboxXformGroup = kodachi::GetGlobalXFormGroup(interface, inputLocation);
        if (!bboxAttr.isValid() || !bboxXformGroup.isValid()) {
            return;
        }

        kodachi::DoubleAttribute bboxXformAttr =
                kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(bboxXformGroup).first;

        if (bboxXformAttr.getNumberOfTimeSamples() == 1 &&
                cameraXformAttr.getNumberOfTimeSamples() == 1) {

            const float bboxXformSampleTime = bboxXformAttr.getSampleTime(0);
            const float cameraXformSampleTime = cameraXformAttr.getSampleTime(0);

            sampleTimes.push_back(cameraXformSampleTime);
            if (bboxXformSampleTime != cameraXformSampleTime) {
                // The camera is stationary, interpolate bbox xform time sample to the same
                // time sample as the camera.
                bboxXformAttr =
                        kodachi::XFormUtil::CalcTransformMatrixAtTimes(bboxXformGroup,
                                                                sampleTimes.data(),
                                                                sampleTimes.size()).first;
            }
        }
        // At least one has more than 1 time sample
        else {
            if (timeSamplesMatch(cameraXformAttr, bboxXformAttr)) {
                auto samplesAccessor = cameraXformAttr.getSamples();
                const float sample_time_0 = samplesAccessor.begin()->getSampleTime();
                const float sample_time_N = (samplesAccessor.end() - 1)->getSampleTime();

                sampleTimes.push_back(sample_time_0);
                sampleTimes.push_back(sample_time_N);
            }
            // If time samples don't match (different values, or different number of time samples),
            // replace them with shutter open and shutter close times.
            else {
                sampleTimes.push_back(shutterOpenTime);
                sampleTimes.push_back(shutterCloseTime);

                bboxXformAttr =
                        kodachi::XFormUtil::CalcTransformMatrixAtTimes(bboxXformGroup,
                                                                sampleTimes.data(),
                                                                sampleTimes.size()).first;

                cameraXformAttr =
                        kodachi::XFormUtil::CalcTransformMatrixAtTimes(cameraXformGroup,
                                                                sampleTimes.data(),
                                                                sampleTimes.size()).first;
            }
        }

        if (!cameraXformAttr.isValid() || !bboxXformAttr.isValid()) {
            return;
        }

        const kodachi::DoubleAttribute::array_type boundingBox = bboxAttr.getNearestSample(0.0f);
        if (boundingBox.size() < 6) {
            return;
        }

        for (std::size_t idx = 0; idx < 6; ++idx) {
            if (std::isnan(boundingBox[idx]) || std::isinf(boundingBox[idx])) {
                // invalid bounding box coords
                return;
            }
        }

        //----------------------------------------------------

        const Imath::V3d aabbMin(boundingBox[0], boundingBox[2], boundingBox[4]);
        const Imath::V3d aabbMax(boundingBox[1], boundingBox[3], boundingBox[5]);

        // Only keep the location if the bounding box is visible at at least one time sample.
        bool deleteSelf = true;
        for (std::size_t sampleIdx = 0; sampleIdx < sampleTimes.size(); ++sampleIdx) {
            const Imath::M44d camBBoxXform =
                    kodachi::internal::XFormAttrToIMath(cameraXformAttr, sampleTimes[sampleIdx]) *
                        kodachi::internal::XFormAttrToIMath(bboxXformAttr, sampleTimes[sampleIdx]).inverse();

            const kodachi::internal::Frustum frustum(frustumVertices, camBBoxXform);

            if (methodStr == kIntersect) {
                const kodachi::internal::IntersectionTestResult result =
                        frustum.AABBIntersection(aabbMin, aabbMax);

                // (1) (intersect) AND (not invert)
                //      - keep if (intersects) OR (fully inside),
                //      - delete if (fully outside)
                // (2) (intersect) AND (invert)
                //      - keep if (fully outside),
                //      - delete if (intersects) OR (fully inside)
                if (!invertMethod &&
                        (result == kodachi::internal::IntersectionTestResult::FULLY_INSIDE ||
                            result == kodachi::internal::IntersectionTestResult::INTERSECTS)) {
                    deleteSelf = false;
                }
                else if (invertMethod && result == kodachi::internal::IntersectionTestResult::FULLY_OUTSIDE) {
                    deleteSelf = false;
                }
            }
            else if (methodStr == kContainsCenter) {
                const Imath::V3d aabbCenter = (aabbMax + aabbMin) / 2.0;
                const bool containsCenter = frustum.containsPoint(aabbCenter);

                // (1) (contains center) AND (not invert)
                //      - keep if (center point is inside),
                //      - delete if (center point is not inside)
                // (2) (contains center) AND (invert)
                //      - keep if (center point is not inside),
                //      - delete if (center point is inside)
                if (!invertMethod) {
                    deleteSelf = !containsCenter;
                }
                else {
                    deleteSelf = containsCenter;
                }
            }
            else if (methodStr == kContainsAll) {
                const kodachi::internal::IntersectionTestResult result =
                        frustum.AABBIntersection(aabbMin, aabbMax);

                // (1) (contains all) AND (not invert)
                //      - keep if (fully inside),
                //      - delete if (intersects) OR (fully outside)
                // (2) (contains all) AND (invert)
                //      - keep if (fully outside),
                //      - delete if (intersects) OR (fully inside)
                if (!invertMethod && result == kodachi::internal::IntersectionTestResult::FULLY_INSIDE) {
                    deleteSelf = false;
                }
                else if (invertMethod && result == kodachi::internal::IntersectionTestResult::FULLY_OUTSIDE) {
                    deleteSelf = false;
                }
            }
            else {
                COOKDEBUG("Invalid method chosen");
                return;
            }

            // If bounding box is visible at 1st time sample, there is no need to test the rest of the sample times.
            if (!deleteSelf) {
                return;
            }
        }

        // Getting to this point means the bounding box is not visible at any time samples.
        if (executionMode == kImmediateExecutionMode) {
            interface.deleteSelf();
        }
        else /* (executionMode == kDeferredExecutionMode) */ {
            interface.setAttr("deferredPrune", FnAttribute::IntAttribute(1));
            interface.stopChildTraversal();
        }

        // Getting to this point means this location is being pruned, and no
        // primitive pruning would be necessary
        interface.deleteAttr("primitivePrune.frustumPrune");
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(PruneByFrustumOp)

} // anonymous

//------------------------------------------------
//
//------------------------------------------------

void registerPlugins()
{
    REGISTER_PLUGIN(PruneByFrustumOp, "PruneByFrustum", 0, 3);
}

