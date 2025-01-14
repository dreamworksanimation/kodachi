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
#include <kodachi/attribute/ZeroCopyDataBuilder.h>

// OpenEXR
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrixAlgo.h>

// kodachi
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/Op.h>

namespace {
KdLogSetup("PruneBySize");

const std::string kImmediateExecutionMode = "immediate";
const std::string kDeferredExecutionMode = "deferred";
const std::string kDimCheckMode = "compare dimensions";
const std::string kVolumeCheckMode = "compare volume";
const std::string kDontCheck = "don't check";
const std::string kGreaterThan = "greater than";
const std::string kLessThan = "less than";

class PruneBySizeOp: public kodachi::GeolibOp {
public:

    static void setup(kodachi::GeolibSetupInterface &interface)
    {
        interface.setThreading(kodachi::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::GeolibCookInterface &interface)
    {
        const kodachi::StringAttribute modeAttr = interface.getOpArg("mode");
        if (!modeAttr.isValid()) {
            return;
        }
        const bool volumeCheck = (modeAttr.getValueCStr() == kVolumeCheckMode);

        KdLogDebug(interface.getInputLocationPath());

        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        if (!celAttr.isValid()) {
            KdLogError("Invalid CEL");
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

        const kodachi::StringAttribute executionModeAttr = interface.getOpArg("executionMode");
        if (!executionModeAttr.isValid()) {
            return;
        }

        bool prune = false;
        const kodachi::DoubleAttribute boundAttr = interface.getAttr("bound");
        if (boundAttr.isValid()) {
            // compare volume
            if (volumeCheck) {
                const kodachi::StringAttribute volCompAttr = interface.getOpArg("volume");
                if (!volCompAttr.isValid()) {
                    return;
                }
                const bool greaterThan = (volCompAttr.getValueCStr() == kGreaterThan);

                const kodachi::DoubleAttribute sizeCompAttr = interface.getOpArg("vComp");
                if (!sizeCompAttr.isValid()) {
                    return;
                }
                const double sizeComp = sizeCompAttr.getValue();
                kodachi::DoubleAttribute size;

                // calculate volume
                const kodachi::DoubleAttribute xFormAttr =
                        kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(kodachi::GetGlobalXFormGroup(interface)).first;
                if (xFormAttr.isValid()) {

                    kodachi::ZeroCopyDoubleBuilder volumeBuilder;
                    for (const auto currentSample : xFormAttr.getSamples()) {
                        const auto sampleTime = currentSample.getSampleTime();

                        // Get the current location's worldspace transform
                        const Imath::M44d xForm = currentSample.getAs<Imath::M44d, 16>();
                        const auto bound = boundAttr.getNearestSample(sampleTime);

                        // We're just going to get the scale of the xform
                        // and apply it to the bounds
                        Imath::V3d scale(1, 1, 1);
                        Imath::extractScaling(xForm, scale);

                        const Imath::V3d boundMin = Imath::V3d(bound[0],
                                                               bound[2],
                                                               bound[4]);
                        const Imath::V3d boundMax = Imath::V3d(bound[1],
                                                               bound[3],
                                                               bound[5]);

                        const Imath::V3d diag = (boundMax - boundMin) * scale;
                        volumeBuilder.push_back(diag.x * diag.y * diag.z, sampleTime);
                    }
                    size = volumeBuilder.build();
                }

                if (!size.isValid()) {
                    return;
                }

                if ((greaterThan && (size.getValue() > sizeComp)) ||
                   (!greaterThan && (size.getValue() < sizeComp)))
                    prune = true;
            }
            // compare dimensions
            else {
                const auto nearestSample = boundAttr.getNearestSample(0);

                // check x
                const kodachi::StringAttribute xCheckAttr = interface.getOpArg("xLength");
                if (!xCheckAttr.isValid()) return;
                const std::string xCheck = xCheckAttr.getValueCStr();
                if (xCheck != kDontCheck) {
                    const kodachi::DoubleAttribute xCompAttr = interface.getOpArg("xComp");
                    if (!xCompAttr.isValid()) return;
                    const double xComp = xCompAttr.getValue();
                    const double x1 = nearestSample[0];
                    const double x2 = nearestSample[1];
                    const double xDiff = fabs(x2 - x1);
                    if (((xCheck == kGreaterThan) && (xDiff > xComp)) ||
                        ((xCheck != kGreaterThan) && (xDiff < xComp)))
                        prune = true;
                }

                // check y
                const kodachi::StringAttribute yCheckAttr = interface.getOpArg("yLength");
                if (!yCheckAttr.isValid()) return;
                const std::string yCheck = yCheckAttr.getValueCStr();
                if (yCheck != kDontCheck) {
                    const kodachi::DoubleAttribute yCompAttr = interface.getOpArg("yComp");
                    if (!yCompAttr.isValid()) return;
                    const double yComp = yCompAttr.getValue();
                    const double y1 = nearestSample[2];
                    const double y2 = nearestSample[3];
                    const double yDiff = fabs(y2 - y1);
                    if (((yCheck == kGreaterThan) && (yDiff > yComp)) ||
                        ((yCheck != kGreaterThan) && (yDiff < yComp)))
                        prune = true;
                }

                // check z
                const kodachi::StringAttribute zCheckAttr = interface.getOpArg("zLength");
                if (!zCheckAttr.isValid()) return;
                const std::string zCheck = zCheckAttr.getValueCStr();
                if (zCheck != kDontCheck) {
                    const kodachi::DoubleAttribute zCompAttr = interface.getOpArg("zComp");
                    if (!zCompAttr.isValid()) return;
                    const double zComp = zCompAttr.getValue();
                    const double z1 = nearestSample[4];
                    const double z2 = nearestSample[5];
                    const double zDiff = fabs(z2 - z1);
                    if (((zCheck == kGreaterThan) && (zDiff > zComp)) ||
                        ((zCheck != kGreaterThan) && (zDiff < zComp)))
                        prune = true;
                }
            }

            if (prune) {
                if (executionModeAttr == kImmediateExecutionMode) {
                    KdLogDebug("deleting self.");
                    interface.deleteSelf();
                } else /* (executionMode == kDeferredExecutionMode) */{
                    interface.setAttr("deferredPrune", kodachi::IntAttribute(1));
                    interface.stopChildTraversal();
                }
                return;
            }
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(PruneBySizeOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(PruneBySizeOp, "PruneBySizeOp", 0, 2);
}

