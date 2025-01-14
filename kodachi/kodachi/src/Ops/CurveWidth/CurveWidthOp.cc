// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/ZeroCopyAttribute.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <kodachi/ExpressionMath.h>

// Imath
#include <OpenEXR/ImathVec.h>

namespace
{

kodachi::FloatAttribute
getDefaultKnots()
{
    static kodachi::FloatAttribute sDefault;
    if (!sDefault.isValid()) {
        static std::once_flag sOnceFlag;
        std::call_once(sOnceFlag,
        [&]() {
            std::vector<float> defaultValues { 0.0f, 0.0f, 1.0f, 1.0f };
            sDefault = kodachi::FloatAttribute(defaultValues.data(), defaultValues.size(), 1);
        }
        );
    }
    return sDefault;
}

kodachi::FloatAttribute
getDefaultRampValues()
{
    static kodachi::FloatAttribute sDefault;
    if (!sDefault.isValid()) {
        static std::once_flag sOnceFlag;
        std::call_once(sOnceFlag,
        [&]() {
            std::vector<float> defaultValues { 1.0f, 1.0f, 1.0f, 1.0f };
            sDefault = kodachi::FloatAttribute(defaultValues.data(), defaultValues.size(), 1);
        }
        );
    }
    return sDefault;
}

KdLogSetup("CurveWidthOp");

// width control for curve geometry
// with curveOperations.widthFactor set
// scales curve width based on maxWidth and ramp values normalized
// along the lenght of the curve
class CurveWidthOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCurves("curves");
        if (kodachi::StringAttribute(interface.getAttr("type")) != kCurves) {
            return;
        }

        // *** widthFactor attributes ***
        const kodachi::GroupAttribute widthFactorAttr =
                kodachi::GetGlobalAttr(interface, "curveOperations.widthFactor");
        if (!widthFactorAttr.isValid()) {
            return;
        }
        // can't just delete the attr since the attribute may not be on this location
        interface.setAttr("curveOperations.widthFactor", kodachi::NullAttribute());

        // max scaling factor
        const kodachi::FloatAttribute maxWidthAttr = widthFactorAttr.getChildByName("maxWidth");
        const float maxScaleFactor = maxWidthAttr.getValue(1.0f, false);

        // *** Ramp Attributes ***
        // interpolation mode - this is technically unused for now (defaulting to linear)
        const kodachi::StringAttribute interpAttr = widthFactorAttr.getChildByName("interpolation");
        // knots
        kodachi::FloatAttribute knotsAttr = widthFactorAttr.getChildByName("knots");
        if (!knotsAttr.isValid()) {
            knotsAttr = getDefaultKnots();
        }
        const auto knotSamples = knotsAttr.getSamples();
        const auto& knots = knotSamples.front();
        // per-knot ramp values
        kodachi::FloatAttribute valuesAttr = widthFactorAttr.getChildByName("values");
        if (!valuesAttr.isValid()) {
            valuesAttr = getDefaultRampValues();
        }
        const auto valueSamples = valuesAttr.getSamples();
        const auto& rampValues = valueSamples.front();

        // *** Geometry Attribute ***
        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
             kodachi::ReportWarning(interface, "Curve geometry missing 'geometry' attribute");
             return;
        }

        // points
        const kodachi::FloatAttribute pointsAttr = geometryAttr.getChildByName("point.P");
        if (pointsAttr.getNumberOfValues() == 0) {
            kodachi::ReportWarning(interface, "Curve geometry missing 'point.P'");
            return;
        }
        const auto pointSamples = pointsAttr.getSamples();

        // widths
        const kodachi::FloatAttribute widthsAttr = geometryAttr.getChildByName("point.width");
        if (widthsAttr.getNumberOfValues() == 0) {
            kodachi::ReportWarning(interface, "Curve geometry missing 'point.width'");
            return;
        }
        const auto widthSamples    = widthsAttr.getSamples();
        const auto widthSampleTimes = widthSamples.getSampleTimes();
        const std::size_t widthNumSamples = widthSampleTimes.size();

        // num vertices
        const kodachi::IntAttribute numVertsAttr = geometryAttr.getChildByName("numVertices");
        if (numVertsAttr.getNumberOfValues() == 0) {
            kodachi::ReportWarning(interface, "Curve geometry missing 'numVertices' attribute");
             return;
        }
        const auto numVertSamples = numVertsAttr.getSamples();
        const auto& numVertSample  = numVertSamples.front();

        // *** output ***
        std::vector<float> outWidths;
        outWidths.reserve(widthsAttr.getNumberOfValues() * widthNumSamples);

        // *** for each time sample ***
        for (const float time : widthSampleTimes) {

            // not assuming that they will have the same time samples
            const auto& widthsT = widthSamples.getNearestSample(time);
            const auto& pointsT = pointSamples.getNearestSample(time);
            uint pIdx = 0;
            uint wIdx = 0;

            // *** for each curve ***
            for (const int32_t numVert : numVertSample) {

                std::vector<Imath::V3f> curve;
                std::vector<float> lengths; // accumulating length of each CV
                curve.reserve(numVert);
                curve.emplace_back(pointsT[pIdx],
                                   pointsT[pIdx + 1],
                                   pointsT[pIdx + 2]);
                lengths.reserve(numVert);
                lengths.emplace_back(0);

                // calculate length of curve
                for (int32_t cv = 1; cv < numVert; ++cv) {
                    pIdx += 3;
                    curve.emplace_back(pointsT[pIdx],
                                       pointsT[pIdx + 1],
                                       pointsT[pIdx + 2]);

                    const float dist = (curve[cv] - curve[cv - 1]).length();
                    lengths.emplace_back(dist + lengths[cv - 1]);
                }
                pIdx += 3;

                const float totalLength = lengths.back();
                for (int32_t cv = 0; cv < numVert; ++cv) {
                    // normalize the accumulated lengths of each CV
                    const float lengthNormalized =
                            // check that the curve length is valid
                            // otherwise just default to zero
                            (totalLength <= std::numeric_limits<float>::epsilon()) ?
                            0 : (lengths[cv] / totalLength);

                    // find the surrounding knots indices
                    std::pair<size_t, size_t> knotIndices =
                            getKnotIndices(knots, lengthNormalized);

                    // interpolate the knot values based on interpolation type
                    // the ramp values are [0,1] factors of maxScaleFactor
                    float scaleFactor = interpolateKnotValues(
                            lengthNormalized - knots[knotIndices.first],
                            rampValues[knotIndices.first], rampValues[knotIndices.second],
                            interpAttr);
                    scaleFactor *= maxScaleFactor;

                    outWidths.emplace_back(widthsT[wIdx] * scaleFactor);
                    wIdx++;
                }

            } // num verts loop
        } // time samples loop

        // update width attr
        interface.setAttr("geometry.point.width",
                          kodachi::ZeroCopyFloatAttribute::create(widthSampleTimes,
                                                                  std::move(outWidths), 1),
                          false);

    } // end cook

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

    // given a value val [0, 1] and a list of knots
    // which are ordered values from [0, n, m, ... , 1] where 1 > ... > m > n > 0
    // return the pair of indices of the knots that encapsulate the
    // value val
    // first and last knot is always expected to be 0 and 1
    static std::pair<size_t, size_t>
    getKnotIndices(
            const kodachi::FloatAttribute::accessor_type::const_reference knots,
            float val)
    {
        const size_t numKnots = knots.size();

        for (size_t i = numKnots - 1; (i-- > 0); ) {
            // if val matches a knot, just use that knot
            if (val == knots[i]) {
                return {i, i};
            }
            // otherwise find the first knot smaller than val
            if (knots[i] < val) {
                return {i, i+1};
            }
        }

        // if we're here, something's terribly wrong
        KdLogError("Invalid range value encountered! Value: " << val);
        return {0, 0};
    }

    static float
    interpolateKnotValues(float t, float a, float b,
                const kodachi::StringAttribute& interpolation)
    {
        // currently not supporting other interpolation types
        // https://community.foundry.com/discuss/topic/136849/spline-ui-ris-api-broken#
        // TP 269936 - Support for additional interpolator types for use in float ramps and color ramps

        // defaulting to linear
        return kodachi::ExpressionMath::lerp(t, a, b);
    }
};

DEFINE_KODACHIOP_PLUGIN(CurveWidthOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(CurveWidthOp, "CurveWidthOp", 0, 1);
}


