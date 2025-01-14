// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>
#include <FnAttribute/FnGroupBuilder.h>

#include <cmath>
#include <random>

namespace {

constexpr float TWO_PI = 2.f * M_PI;

template <class Attr, class T = typename Attr::value_type>
T getAttrValue(const FnAttribute::Attribute& attr, const T& defValue)
{
    Attr castAttr(attr);

    return castAttr.getValue(defValue, false);
}

struct CurveClumpCreateOp : public FnKat::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(
                   Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        using namespace FnAttribute;

        interface.setAttr("type", StringAttribute("curves"));

        const int numCurves =
                getAttrValue<IntAttribute>(interface.getOpArg("numCurves"), 1);
        const int k =
                getAttrValue<IntAttribute>(interface.getOpArg("k"), 1);

        const float baseRadius =
                getAttrValue<FloatAttribute>(interface.getOpArg("baseRadius"), 0.1f);
        const float topRadius =
                        getAttrValue<FloatAttribute>(interface.getOpArg("topRadius"), 1.f);
        const float width =
                getAttrValue<FloatAttribute>(interface.getOpArg("width"), 0.001f);

        float baseWidth = 0;
        float tipWidth = 0;
        const FloatAttribute baseWidthAttr = interface.getOpArg("baseWidth");
        const FloatAttribute tipWidthAttr = interface.getOpArg("tipWidth");
        const bool usePerPointWidth = baseWidthAttr.isValid() && tipWidthAttr.isValid();

        const float averageHeight = getAttrValue<FnAttribute::FloatAttribute>(interface.getOpArg("averageHeight"), 1.f);
        const float variance = getAttrValue<FnAttribute::FloatAttribute>(interface.getOpArg("variance"), 0.1f);
        const float stdDev = std::sqrt(variance);

        const float maxSegmentOffset = getAttrValue<FnAttribute::FloatAttribute>(interface.getOpArg("maxSegmentOffset"), 0.1f);

        // build random bezier curves
        FnAttribute::GroupBuilder geometryBuilder;
        geometryBuilder.set("degree", IntAttribute(3));

        if (usePerPointWidth) {
            baseWidth = baseWidthAttr.getValue();
            tipWidth = tipWidthAttr.getValue();
        } else {
            geometryBuilder.set("constantWidth", FloatAttribute(width));
        }


        const int32_t cvsPerCurve = 3*k + 1;
        {
            const std::vector<int32_t> numVertices(numCurves, cvsPerCurve);
            geometryBuilder.set("numVertices",
                                IntAttribute(numVertices.data(),
                                             numVertices.size(),
                                             1));
        }

        std::vector<float> points;
        points.reserve(cvsPerCurve * 3 * numCurves);

        std::vector<float> widths;
        if (usePerPointWidth) {
            widths.reserve(cvsPerCurve * numCurves);
        }

        // use location hash as seed for now
        const size_t locationHash =
                     std::hash<std::string>()(interface.getInputLocationPath());
        std::mt19937_64 randomEngine(locationHash);

        std::uniform_real_distribution<float> uniformDist(0.f, std::nextafter(1.f, 2.f));
        std::normal_distribution<float> curveHeightDist(averageHeight, stdDev);

        const float radiusDelta = (topRadius - baseRadius) / float(cvsPerCurve - 1);
        const float widthDelta = (tipWidth - baseWidth) / float(cvsPerCurve - 1) ;

        for (int i = 0; i < numCurves; ++i) {
            const float angle = uniformDist(randomEngine) * TWO_PI;
            const float r = std::sqrt(uniformDist(randomEngine)) * baseRadius;
            const float x = r * std::cos(angle);
            const float z = r * std::sin(angle);

            // base point
            points.push_back(x);
            points.push_back(0);
            points.push_back(z);

            if (usePerPointWidth) {
                widths.push_back(baseWidth);
            }

            const float curveLength = curveHeightDist(randomEngine);
            const float heightDelta = curveLength / float(cvsPerCurve - 1);

            float xOffset = 0.f;
            float yOffset = 0.f;

            for (unsigned j = 1; j < cvsPerCurve; ++j) {
                // the maximum offset from the origin at this height
                const float offsetRad = baseRadius + (j * radiusDelta);

                std::uniform_real_distribution<float> xOffsetDist(std::max(-offsetRad, xOffset - maxSegmentOffset), std::min(offsetRad, xOffset + maxSegmentOffset));
                std::uniform_real_distribution<float> yOffsetDist(std::max(-offsetRad, yOffset - maxSegmentOffset), std::min(offsetRad, yOffset + maxSegmentOffset));
                xOffset = xOffsetDist(randomEngine);
                yOffset = yOffsetDist(randomEngine);
                points.push_back(x + xOffset);
                points.push_back(j * heightDelta);
                points.push_back(z + yOffset);

                if (usePerPointWidth) {
                    widths.push_back(baseWidth + (j * widthDelta));
                }
            }
        }

        geometryBuilder.set("point.P", FloatAttribute(points.data(),
                                                      points.size(),
                                                      3));

        if (usePerPointWidth) {
            geometryBuilder.set("point.width", FloatAttribute(widths.data(),
                                                              widths.size(),
                                                              1));
        }

        interface.setAttr("geometry", geometryBuilder.build());
    }
};

DEFINE_GEOLIBOP_PLUGIN(CurveClumpCreateOp);

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(CurveClumpCreateOp, "CurveClumpCreate", 0, 1);
}

