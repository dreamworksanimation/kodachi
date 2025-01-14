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
#include <OpenEXR/ImathBox.h>
#include <OpenEXR/ImathBoxAlgo.h>

namespace {

class DistanceMetricSetOp : public kodachi::Op
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
        kodachi::CookInterfaceUtils::matchesCEL(info,
                                                                 interface,
                                                                 celAttr);
        if (!info.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!info.matches) {
            return;
        }

        const kodachi::DoubleAttribute currentBoundAttr =
                interface.getAttr("bound");
        if (currentBoundAttr.isValid()) {
            const kodachi::DoubleAttribute currentXFormAttr =
                    kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(kodachi::GetGlobalXFormGroup(interface)).first;
            if (currentXFormAttr.isValid()) {
                const kodachi::StringAttribute targetPathAttr =
                        interface.getOpArg("targetLocation");
                const kodachi::StringAttribute distanceAttributeNameAttr =
                        interface.getOpArg("distanceAttributeName");
                if (targetPathAttr.isValid() && distanceAttributeNameAttr.isValid()) {
                    const kodachi::string_view targetPath =
                            targetPathAttr.getValueCStr();
                    const kodachi::String distanceAttributeName =
                            distanceAttributeNameAttr.getValue("distance");
                    if (!targetPath.empty() && interface.doesLocationExist(targetPath)) {
                        interface.prefetch(targetPath);

                        kodachi::DoubleAttribute targetXFormAttr =
                                interface.getOpArg("targetXForm");
                        if (!targetXFormAttr.isValid()) {
                            targetXFormAttr =
                                    kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(
                                            kodachi::GetGlobalXFormGroup(interface,
                                                                         std::string(targetPath))).first;
                            interface.replaceChildTraversalOp("",
                                                              kodachi::GroupBuilder().deepUpdate(
                                                                      interface.getOpArg("")).set("targetXForm",
                                                                                                  targetXFormAttr).build());
                        }

                        kodachi::ZeroCopyDoubleBuilder distanceBuilder;
                        for (const auto currentSample : currentXFormAttr.getSamples()) {
                            const auto sampleTime = currentSample.getSampleTime();
                            auto& sampleDataRef = distanceBuilder.get(sampleTime);

                            // Get the target worldspace position
                            const auto targetXFormAttrSamples =
                                    targetXFormAttr.getSamples();
                            const auto targetXForm =
                                    targetXFormAttrSamples.getNearestSample(sampleTime).getAs<
                                            Imath::M44d, 16>();

                            // Get the current location's worldspace transform
                            const Imath::M44d currentXForm =
                                    currentSample.getAs<Imath::M44d, 16>();

                            // Put the target position in current location's space
                            const auto targetPositionInBoxSpace =
                                    targetXForm.translation()
                                            * currentXForm.inverse();
                            const auto currentBound =
                                    currentBoundAttr.getNearestSample(sampleTime);

                            // Now that we have an axis-aligned bounds, get the closest point
                            // in the box to the target position
                            const Imath::V3d currentBoundMin =
                                    Imath::V3d(currentBound[0],
                                               currentBound[2],
                                               currentBound[4]);
                            const Imath::V3d currentBoundMax =
                                    Imath::V3d(currentBound[1],
                                               currentBound[3],
                                               currentBound[5]);
                            const Imath::Box3d currentBoundBox(currentBoundMin,
                                                               currentBoundMax);
                            const auto deltaVec =
                                    targetPositionInBoxSpace
                                            - Imath::clip<Imath::V3d>(targetPositionInBoxSpace,
                                                                      currentBoundBox);

                            // Move the delta vector back into world space and get its length
                            Imath::V3d deltaVecWS;
                            currentXForm.multDirMatrix(deltaVec, deltaVecWS);
                            sampleDataRef.push_back(deltaVecWS.length());
                        }
                        interface.setAttr("metrics." + distanceAttributeName,
                                          kodachi::DoubleAttribute(distanceBuilder.build()));
                    }
                }
            }
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(DistanceMetricSetOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(DistanceMetricSetOp, "DistanceMetricSet", 0, 1);
}

