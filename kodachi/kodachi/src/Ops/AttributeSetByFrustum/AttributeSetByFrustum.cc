// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Katana
#include <kodachi/StringView.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/GeometryUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

// OpenEXR
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathLine.h>

// kodachi
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/Op.h>

namespace {
KdLogSetup("AttributeSetByFrustum");

const std::string kIntersect      = "intersect";
const std::string kContainsAll    = "contains all";
const std::string kContainsCenter = "contains center";

const std::string kImmediateExecutionMode = "immediate";
const std::string kDeferredExecutionMode = "deferred";

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

//------------------------------------------------
//
//------------------------------------------------

class AttributeSetByFrustumOp: public kodachi::GeolibOp {
public:

    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(
                kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        // If CEL not specified, do nothing.
        if (!celAttr.isValid()) {
            KdLogDebug("Invalid CEL");
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
                KdLogWarn("Invalid method chosen");
                return;
            }
        } else {
            KdLogWarn("Missing method attribute.");
            return;
        }

        const kodachi::IntAttribute invertAttr = interface.getOpArg("invert");
        const bool invertMethod = invertAttr.getValue(0, false);

        const kodachi::StringAttribute executionModeAttr = interface.getOpArg("executionMode");
        if (!executionModeAttr.isValid()) {
            return;
        }

        const std::string executionMode = executionModeAttr.getValue();

        const kodachi::StringAttribute attributeNameAttr = interface.getOpArg("attributeName");
        kodachi::String attributeName;
        if (attributeNameAttr.isValid()) {
            attributeName = attributeNameAttr.getValue();
        }
        else {
            KdLogWarn("Invalid attribute name");
            return;
        }

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

        // Only set attr if the bounding box is visible at at least one time sample.
        bool setAttr = false;
        for (std::size_t sampleIdx = 0; sampleIdx < sampleTimes.size(); ++sampleIdx) {
            const Imath::M44d camBBoxXform =
                    kodachi::internal::XFormAttrToIMath(cameraXformAttr, sampleTimes[sampleIdx]) *
                        kodachi::internal::XFormAttrToIMath(bboxXformAttr, sampleTimes[sampleIdx]).inverse();

            const kodachi::internal::Frustum frustum(frustumVertices, camBBoxXform);

            if (methodStr == kIntersect) {
                const kodachi::internal::IntersectionTestResult result =
                        frustum.AABBIntersection(aabbMin, aabbMax);

                // (1) (intersect) AND (not invert)
                //      - set if (intersects) OR (fully inside),
                //      - ignore if (fully outside)
                // (2) (intersect) AND (invert)
                //      - set if (fully outside),
                //      - ignore if (intersects) OR (fully inside)
                if (!invertMethod &&
                        (result == kodachi::internal::IntersectionTestResult::FULLY_INSIDE ||
                            result == kodachi::internal::IntersectionTestResult::INTERSECTS)) {
                    setAttr = true;
                }
                else if (invertMethod && result == kodachi::internal::IntersectionTestResult::FULLY_OUTSIDE) {
                    setAttr = true;
                }
            }
            else if (methodStr == kContainsCenter) {
                const Imath::V3d aabbCenter = (aabbMax + aabbMin) / 2.0;
                const bool containsCenter = frustum.containsPoint(aabbCenter);

                // (1) (contains center) AND (not invert)
                //      - set if (center point is inside),
                //      - ignore if (center point is not inside)
                // (2) (contains center) AND (invert)
                //      - set if (center point is not inside),
                //      - ignore if (center point is inside)
                if (!invertMethod) {
                    setAttr = containsCenter;
                }
                else {
                    setAttr = !containsCenter;
                }
            }
            else if (methodStr == kContainsAll) {
                const kodachi::internal::IntersectionTestResult result =
                        frustum.AABBIntersection(aabbMin, aabbMax);

                // (1) (contains all) AND (not invert)
                //      - set if (fully inside),
                //      - ignore if (intersects) OR (fully outside)
                // (2) (contains all) AND (invert)
                //      - set if (fully outside),
                //      - ignore if (intersects) OR (fully inside)
                if (!invertMethod && result == kodachi::internal::IntersectionTestResult::FULLY_INSIDE) {
                    setAttr = true;
                }
                else if (invertMethod && result == kodachi::internal::IntersectionTestResult::FULLY_OUTSIDE) {
                    setAttr = true;
                }
            }
            else {
                KdLogWarn("Invalid method chosen");
                return;
            }

            // If bounding box is visible at 1st time sample, there is no need to test the rest of the sample times.
            if (setAttr) {
                if (executionModeAttr == kImmediateExecutionMode) {
                    interface.setAttr("volume.metrics." + attributeName,
                                      kodachi::IntAttribute(1));
                } else {
                    interface.setAttr("volume.metrics." + attributeName + "Deferred",
                                      kodachi::IntAttribute(1));
                    interface.stopChildTraversal();
                }
                return;
            }
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(AttributeSetByFrustumOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(AttributeSetByFrustumOp, "AttributeSetByFrustumOp", 0, 3);
}

