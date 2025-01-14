// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// Katana
#include <kodachi/StringView.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyDataBuilder.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>

// OpenEXR
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathBox.h>
#include <OpenEXR/ImathBoxAlgo.h>

namespace {

class CoCMetricSetOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        if (!celAttr.isValid()) {
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

        const kodachi::DoubleAttribute currentBoundAttr = interface.getAttr("bound");
        if (currentBoundAttr.isValid()) {
            const kodachi::StringAttribute cameraPathAttr = interface.getOpArg("cameraLocation");
            if (cameraPathAttr.isValid()) {
                const kodachi::string_view cameraPath = cameraPathAttr.getValueCStr();
                if (!cameraPath.empty()) {
                    if (!interface.doesLocationExist(cameraPath)) {
                        interface.setAttr(
                                "metrics.coc.error",
                                kodachi::StringAttribute("Camera " + std::string(cameraPath) + " not found!"));
                        interface.stopChildTraversal();
                        return;
                    }
                    interface.prefetch(cameraPath);

                    const kodachi::DoubleAttribute currentXFormAttr =
                            kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                                    kodachi::GetGlobalXFormGroup(interface)).first;
                    if (currentXFormAttr.isValid()) {

                        const float sceneScale = kodachi::FloatAttribute(
                                interface.getAttr("moonrayGlobalStatements.scene scale", "/root")).getValue(.01, false);

                        // Cached via replacement of op args by an ancnestral cook
                        kodachi::GroupAttribute camInfoAttr = interface.getOpArg("camInfo");
                        kodachi::DoubleAttribute cameraXFormAttr = interface.getOpArg("cameraXForm");
                        double magnification;
                        double camImageWidth;
                        double camAperatureDia;
                        double camCenterOfInterest;

                        if (!camInfoAttr.isValid()) {
                            cameraXFormAttr = kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                                    kodachi::GetGlobalXFormGroup(interface, std::string(cameraPath))).first;

                            const float camAperture = kodachi::FloatAttribute(
                                    interface.getAttr("moonrayCameraStatements.dof_aperture", cameraPath)).getValue(
                                    8.0, false);
                            const double camFOV = kodachi::DoubleAttribute(
                                    interface.getAttr("geometry.fov", cameraPath)).getValue();

                            const kodachi::DoubleAttribute coiAttr =
                                    interface.getAttr("geometry.centerOfInterest", cameraPath);
                            if (!coiAttr.isValid()) {
                                interface.setAttr(
                                        "metrics.coc.error",
                                        kodachi::StringAttribute("Camera 'dof' not enabled, missing 'geometry.centerOfInterest'"));
                                interface.stopChildTraversal();
                                return;
                            }

                            camCenterOfInterest = coiAttr.getValue() * sceneScale * 1000;
                            camImageWidth = kodachi::FloatAttribute(
                                    interface.getAttr("moonrayCameraStatements.dof_image_size", cameraPath))
                                    .getValue(35.0, false);
                            const double camFocalLength = (camImageWidth / 2.0)
                                    / std::tan((camFOV / 2.0) * (M_PIl / 180));
                            camAperatureDia = camFocalLength / camAperture;
                            magnification = camFocalLength / (camCenterOfInterest - camFocalLength);
                            camInfoAttr = kodachi::GroupBuilder().set("camAperatureDia",
                                                                      kodachi::DoubleAttribute(camAperatureDia)).set(
                                    "magnification", kodachi::DoubleAttribute(magnification)).set(
                                    "camCenterOfInterest", kodachi::DoubleAttribute(camCenterOfInterest)).set(
                                    "camImageWidth", kodachi::DoubleAttribute(camImageWidth)).build();

                            // Save the cameraXform for any location beneath this one so we don't calculate it again
                            const auto newOpArgs = kodachi::GroupBuilder().update(interface.getOpArg("")).set(
                                    "cameraXForm", cameraXFormAttr).set("camInfo", camInfoAttr).build();
                            interface.replaceChildTraversalOp("", newOpArgs);
                        } else {
                            camAperatureDia = kodachi::DoubleAttribute(camInfoAttr.getChildByName("camAperatureDia"))
                                    .getValue();
                            magnification = kodachi::DoubleAttribute(camInfoAttr.getChildByName("magnification"))
                                    .getValue();
                            camCenterOfInterest = kodachi::DoubleAttribute(
                                    camInfoAttr.getChildByName("camCenterOfInterest")).getValue();
                            camImageWidth = kodachi::DoubleAttribute(camInfoAttr.getChildByName("camImageWidth"))
                                    .getValue();
                        }

                        // Now that we have all the camera info, loop over this location's bound
                        // samples and build multi-sammpled coc attrs
                        kodachi::ZeroCopyDoubleBuilder minCocMmBuilder;
                        kodachi::ZeroCopyDoubleBuilder maxCocMmBuilder;
                        kodachi::ZeroCopyDoubleBuilder minCocPctBuilder;
                        kodachi::ZeroCopyDoubleBuilder maxCocPctBuilder;
                        const double oneOverCamImageWidth = 1.0 / static_cast<double>(camImageWidth);
                        bool inFrontOfCamera = false;
                        for (const auto currentSample : currentXFormAttr.getSamples()) {
                            const float sampleTime = currentSample.getSampleTime();

                            // Get the camera worldspace position
                            const auto cameraXFormAttrSamples = cameraXFormAttr.getSamples();
                            const auto cameraXForm = cameraXFormAttrSamples.getNearestSample(sampleTime)
                                    .getAs<Imath::M44d, 16>();

                            // Get the current location's worldspace transform
                            const Imath::M44d currentXForm = currentSample.getAs<Imath::M44d, 16>();

                            // Put the camera position in current location's space
                            const auto cameraPositionInBoxSpace = cameraXForm.translation() * currentXForm.inverse();
                            const auto currentBound = currentBoundAttr.getNearestSample(sampleTime);

                            // Now that we have an axis-aligned bounds, get the closest point
                            // in the box to the camera position
                            const Imath::V3d currentBoundMin = Imath::V3d(currentBound[0], currentBound[2],
                                                                          currentBound[4]);
                            const Imath::V3d currentBoundMax = Imath::V3d(currentBound[1], currentBound[3],
                                                                          currentBound[5]);
                            const Imath::Box3d currentBoundBox(currentBoundMin, currentBoundMax);
                            const auto deltaVec = cameraPositionInBoxSpace
                                    - Imath::clip<Imath::V3d>(cameraPositionInBoxSpace, currentBoundBox);

                            // Camera z-axis
                            const auto zAxis = Imath::V3d(0, 0, 1000000) * cameraXForm;

                            // Get all the points in the bounding box
                            std::vector<Imath::V3d> points;
                            points.reserve(8);
                            points.push_back(currentBoundMin);
                            points.push_back(currentBoundMax);
                            Imath::V3d bboxSize = currentBoundBox.size();
                            for (unsigned i = 0; i < 2; ++i) {
                                Imath::V3d activePoint = Imath::V3d(points[i]);
                                activePoint.x += bboxSize.x;
                                points.emplace_back(activePoint);
                                activePoint.y += bboxSize.y;
                                points.emplace_back(activePoint);
                                activePoint.x -= bboxSize.x;
                                points.emplace_back(activePoint);
                                bboxSize.negate();
                            }

                            // For every point, get the distance to camera along the camera z-axis
                            // and keep the min and max distances.
                            double minDist = std::numeric_limits<double>::infinity();
                            double maxDist = -std::numeric_limits<double>::infinity();
                            for (const auto& point : points) {
                                // Get the vector from camera to this point in world space
                                const auto deltaVec = cameraPositionInBoxSpace - point;
                                Imath::V3d deltaVecWS;
                                currentXForm.multDirMatrix(deltaVec, deltaVecWS);

                                // Get the distance from camera along z-axis and convert to mm
                                const double dist = deltaVecWS.dot(zAxis.normalized()) * sceneScale * 1000;
                                minDist = std::min(minDist, dist);
                                maxDist = std::max(maxDist, dist);
                            }

                            // If the maxDist is in front of the camera, do something
                            const bool behindCamera = maxDist < 0;
                            if (!behindCamera) {
                                // Compute the actual CoC values for both distances
                                double minCoc = 0.0;
                                double maxCoc = 0.0;

                                const double minDistCoc = std::abs(
                                        camAperatureDia * magnification * (minDist - camCenterOfInterest) / minDist);

                                const double maxDistCoc = std::abs(
                                        camAperatureDia * magnification * (maxDist - camCenterOfInterest) / maxDist);

                                // If the distances straddle the COI, then minCoc is zero
                                maxCoc = std::max(minDistCoc, maxDistCoc);
                                if (minDist < camCenterOfInterest && maxDist > camCenterOfInterest) {
                                    minCoc = 0;
                                } else {
                                    minCoc = std::min(minDistCoc, maxDistCoc);
                                }

                                minCocMmBuilder.push_back(minCoc, sampleTime);
                                maxCocMmBuilder.push_back(maxCoc, sampleTime);
                                minCocPctBuilder.push_back(100.0 * minCoc * oneOverCamImageWidth, sampleTime);
                                maxCocPctBuilder.push_back(100.0 * maxCoc * oneOverCamImageWidth, sampleTime);
                                inFrontOfCamera = true;
                            }
                        }
                        if (inFrontOfCamera) {
                            interface.setAttr("metrics.coc.min.mm", kodachi::DoubleAttribute(minCocMmBuilder.build()));
                            interface.setAttr("metrics.coc.min.percent",
                                              kodachi::DoubleAttribute(minCocPctBuilder.build()));
                            interface.setAttr("metrics.coc.max.mm", kodachi::DoubleAttribute(maxCocMmBuilder.build()));
                            interface.setAttr("metrics.coc.max.percent",
                                              kodachi::DoubleAttribute(maxCocPctBuilder.build()));
                        } else {
                            interface.setAttr("metrics.coc.info",
                                              kodachi::StringAttribute("Location is behind the camera."));
                        }
                    }
                }
            }
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(CoCMetricSetOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(CoCMetricSetOp, "CoCMetricSet", 0, 1);
}

