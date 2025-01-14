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
#include <OpenEXR/ImathMatrixAlgo.h>
#include <OpenEXR/ImathBox.h>
#include <OpenEXR/ImathBoxAlgo.h>

namespace {

class VolumeMetricSetOp : public kodachi::Op
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
        kodachi::FnGeolibCookInterfaceUtils::matchesCEL(info, interface, celAttr);
        if (!info.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!info.matches) {
            return;
        }

        const kodachi::DoubleAttribute boundAttr = interface.getAttr("bound");
        if (boundAttr.isValid()) {
            const kodachi::DoubleAttribute xFormAttr =
                    kodachi::XFormUtil::CalcTransformMatrixAtExistingTimes(kodachi::GetGlobalXFormGroup(interface)).first;
            if (xFormAttr.isValid()) {

                kodachi::ZeroCopyDoubleBuilder volumeBuilder;
                for (const auto currentSample : xFormAttr.getSamples()) {
                    const auto sampleTime = currentSample.getSampleTime();

                    // Get the current location's worldspace transform
                    const Imath::M44d xForm = currentSample.getAs<Imath::M44d, 16>();
                    const auto bound = boundAttr.getNearestSample(sampleTime);

                    // We're just goigg to get the scale of the xform
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
                interface.setAttr("metrics.volume", volumeBuilder.build());
            }
        }
    }
};

DEFINE_GEOLIBOP_PLUGIN(VolumeMetricSetOp)

}

void registerPlugins()
{
    REGISTER_PLUGIN(VolumeMetricSetOp, "VolumeMetricSet", 0, 1);
}

