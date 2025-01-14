// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include <kodachi/op/Op.h>

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyDataBuilder.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {
KdLogSetup("KPOPWidthScale");

class KPOPWidthScale : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root//*{@type==\"rdl2\"}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        static const kodachi::StringAttribute kCurvesTypeAttr("curves");
        static const kodachi::StringAttribute kPointcloudTypeAttr("pointcloud");


        const kodachi::StringAttribute kodachiTypeAttr =
                interface.getAttr("rdl2.meta.kodachiType");

        if (kodachiTypeAttr != kCurvesTypeAttr && kodachiTypeAttr != kPointcloudTypeAttr) {
            return;
        }

        const kodachi::FloatAttribute pscaleAttr =
                interface.getAttr("geometry.arbitrary.pscale.value");
        const kodachi::FloatAttribute radiusMultAttr =
                interface.getAttr("moonrayStatements.radius_mult");

        // Return early if neither of these attributes are valid
        if (!pscaleAttr.isValid() && !radiusMultAttr.isValid()) {
            return;
        }

        bool applyPscale = false;
        bool applyRadiusMult = false;
        if (radiusMultAttr.isValid()) {
            applyRadiusMult = true;
        }

        const kodachi::FloatAttribute widthAttr =
                interface.getAttr("geometry.point.width");
        const kodachi::FloatAttribute constantWidthAttr =
                interface.getAttr("geometry.constantWidth");

        if (widthAttr.isValid()) {
            if (pscaleAttr.isValid()) {
                const int64_t numPscaleValues = pscaleAttr.getNumberOfValues();

                if (numPscaleValues != 1 && numPscaleValues != widthAttr.getNumberOfValues()) {
                    KdLogWarn("Cannot apply 'pscale' arbitrary attribute. It must be of primitive or vertex scope");
                } else {
                    applyPscale = true;
                }
            }

            kodachi::ZeroCopyFloatBuilder widthBuilder(1);

            const auto widthSamples = widthAttr.getSamples();

            for (const auto& widthSample : widthSamples) {
                const float sampleTime = widthSample.getSampleTime();

                std::vector<float> widths(widthSample.begin(), widthSample.end());

                if (applyPscale) {
                    const auto pScaleSample = pscaleAttr.getNearestSample(sampleTime);
                    if (pScaleSample.size() == 1) {
                        const float pScale = pScaleSample[0];

                        for (auto& width : widths) {
                            width *= pScale;
                        }
                    } else {
                        for (size_t i = 0; i < widths.size(); ++i) {
                            widths[i] *= pScaleSample[i];
                        }
                    }
                }

                if (applyRadiusMult) {
                    const float radiusMult = radiusMultAttr.getValue(1.0, false);
                    for (auto& width : widths) {
                        width *= radiusMult;
                    }
                }

                widthBuilder.set(std::move(widths), sampleTime);
            }
            interface.setAttr("geometry.point.width", widthBuilder.build());
        } else if (constantWidthAttr.isValid()) {
            if (pscaleAttr.isValid()) {
                applyPscale = true;
            }

            kodachi::ZeroCopyFloatBuilder widthBuilder(1);

            const auto constantWidthSamples = constantWidthAttr.getSamples();
            for (const auto& constantWidthSample : constantWidthSamples) {
                const float sampleTime = constantWidthSample.getSampleTime();
                std::vector<float> constantWidths(constantWidthSample.begin(), constantWidthSample.end());

                if (applyPscale) {
                    const auto pScaleSample = pscaleAttr.getNearestSample(sampleTime);
                    for (auto& constantWidth : constantWidths) {
                        constantWidth *= pScaleSample[0];
                    }
                }

                if (applyRadiusMult) {
                    const float radiusMult = radiusMultAttr.getValue(1.0, false);
                    for (auto& constantWidth : constantWidths) {
                        constantWidth *= radiusMult;
                    }
                }

                widthBuilder.set(std::move(constantWidths), sampleTime);
            }
            interface.setAttr("geometry.constantWidth", widthBuilder.build());
        }

        // If there are no widths, create a widths attribute.
        // Use number of points for number of widths
        if (!widthAttr.isValid() && !constantWidthAttr.isValid()) {
            float scale = 1.0;
            if (pscaleAttr.isValid()) {
                const auto pScaleSample = pscaleAttr.getNearestSample(0);
                const float pScale = pScaleSample[0];
                scale *= pScale;
                applyPscale = true;
            }

            if (radiusMultAttr.isValid()) {
                const float radiusMult = radiusMultAttr.getValue(1.0, false);
                scale *= radiusMult;
                applyRadiusMult = true;
            }

            const kodachi::FloatAttribute pointsAttr =
                    interface.getAttr("geometry.point.P");

            // pscale is provided as a radius, so multiply it by 2 to set the
            // widthy properly
            kodachi::FloatAttribute widthListAttr;
            kodachi::FloatVector widthList(pointsAttr.getNumberOfValues(), 2.0f * scale);
            widthListAttr = kodachi::ZeroCopyFloatAttribute::create(std::move(widthList));
            interface.setAttr("geometry.point.width", widthListAttr);
        }

        if (applyPscale) {
            interface.deleteAttr("geometry.arbitrary.pscale");
        }

        if (applyRadiusMult) {
            interface.deleteAttr("moonrayStatements.radius_mult");
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Applies 'pscale' and/or 'radius_mult' arbitrary attribute to widths of curves and points locations");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPWidthScale)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPWidthScale, "KPOPWidthScale", 0, 1);
}

