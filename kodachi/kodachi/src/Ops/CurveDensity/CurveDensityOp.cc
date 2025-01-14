// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionPlugin.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

// std
#include <random>
#include <unordered_set>

namespace
{

KdLogSetup("CurveDensityOp");

kodachi::GroupAttribute
reduceDensity(const kodachi::GroupAttribute& geometryAttr)
{
    // density percentage that drives curve culling
    // 0 is no curves and 1 is the full amount of curves (no culling)
    const kodachi::FloatAttribute densityAttr =
            geometryAttr.getChildByName("density");
    if (!densityAttr.isValid()) {
        KdLogDebug(" >>> Curve Density: missing 'density'.");
        return {};
    }
    // density clamped to [0, 1]
    const float density = std::max(0.0f, std::min(1.0f, densityAttr.getValue()));
    KdLogDebug(" >>> Curve Density: " << densityAttr.getValue());

    if (density > (1.0f - std::numeric_limits<float>::epsilon()) &&
            density < (1.0f + std::numeric_limits<float>::epsilon())) {
        // close enough to 1.0
        KdLogDebug(" >>> Curve Density: 'density' is 1, nothing to do.");
        return {};
    }

    // points - only used for seed for random engine, so we don't need to check its validity
    const kodachi::FloatAttribute pointsAttr = geometryAttr.getChildByName("point.P");
    // num vertices
    const kodachi::IntAttribute numVertsAttr = geometryAttr.getChildByName("numVertices");
    if (numVertsAttr.getNumberOfValues() == 0) {
        KdLogWarn(" >>> Curve Density: missing 'numVertices' attribute.");
        return {};
    }

    // *** omit list ***
    // list of integer indices to cull out
    // currently, this is the has the scope of per-points (can omit individual CV's)
    std::vector<int32_t> omitList;

    const auto numVertSamples = numVertsAttr.getSamples();
    const auto& numVertSample  = numVertSamples.front();

    // *** random engine ***
    // determine seed so that the same curve geometry should get
    // culled deterministically, including animated geometry
    const uint64_t seed = [&]()->uint64_t {
        // first determine if there is a specified seed curveOperations.seed
        const kodachi::Attribute seedAttr =
                geometryAttr.getChildByName("seed");
        if (seedAttr.isValid()) {
            return seedAttr.getHash().uint64();
        }

        // next we'll use the ref_P attribute if provided
        const kodachi::GroupAttribute refPAttr =
                geometryAttr.getChildByName("arbitrary.ref_P");
        if (refPAttr.isValid()) {
            return refPAttr.getHash().uint64();
        }

        // finally, we'll just use generic geometry attributes
        kodachi::GroupAttribute hashGroup(
                "point.P.size", kodachi::IntAttribute(pointsAttr.getNumberOfValues()),
                "numVertices",  numVertsAttr,
                "basis",        geometryAttr.getChildByName("basis"),
                "degree",       geometryAttr.getChildByName("degree"),
                false);
        return hashGroup.getHash().uint64();
    }();

    std::ranlux24 randGenerator(seed);
    static const float kMaxUnderOne = 0x1.fffffep-1;

    // uniform real distribution [0-1)
    std::uniform_real_distribution<float> realDistribution(0.0f, 1.0f);

    // *** perform culling ***
    // point index
    int32_t pIdx = 0;
    size_t killCount = 0;
    // now loop through the curves and uniformly decide whether or not it stays or is omitted
    for (const int32_t numVert : numVertSample) {
        // This min ensures the uniform distribution doesn't return 1.0
        // It compensates for a bug in the STL standard where generate_canonical
        // can occasionally return 1.0 - we need a range [0, 1)
        // (http://open-std.org/JTC1/SC22/WG21/docs/lwg-active.html#2524)
        // It is safe to remove the min once the bug is fixed
        float randVal = std::min(realDistribution(randGenerator),
                kMaxUnderOne);

        // if density is 0, we always omit since randVal is always >= 0
        // if density is 1, we never omit since randVal should never reach 1 based on the min above
        // if density is between 0 and 1, the larger the density the less likely we omit
        // the curve
        if (randVal >= density) {
            // omitList is point-scoped, toss all the points in this curve
            for (int32_t v = 0; v < numVert; ++v) {
                omitList.emplace_back(pIdx);
                pIdx++;
            }
            killCount++;
        } else {
            pIdx += numVert;
        }
    }

    KdLogDebug(" >>> Curve Density: culling " << killCount << " curves, " <<
            omitList.size() << " cv's total.");

    // *** OUTPUT ***
    return kodachi::GroupAttribute(
            "omitList", kodachi::ZeroCopyIntAttribute::create(omitList),
            false);
}

// Attribute function for performing reduceCurves on arbitrary geometry attribute
// expects input attrs:
//  - density [0,1] (default: 1)
//  - seed (optional, used for seed of random engine if specified)
//  - numVertices (required)
//  - point.P (optional, used for seed of random engine as the default)
//  - basis (optional, used for seed of random engine as the default)
//  - degree (optional, used for seed of random engine as the default)
//  - arbitrary.ref_P (optional, used for seed for random engine if available)
// returns: GroupAttribute containing 'omitList' int attribute
class CurveDensityAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        kodachi::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            KdLogDebug(" >>> Curve Density Attribute Func: Running curve density reduction.");
            return reduceDensity(rootAttr);
        }
        KdLogDebug(" >>> Curve Density Attribute Func: Input is invalid.");
        return {};
    }
};

// based on curveOperations.density [0-1]
// populates geometry.omitList with a uniform random distribution
// where 0 is no curves and 1 is the full amount of curves
class CurveDensityOp : public kodachi::Op
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

        // density percentage that drives curve culling
        // 0 is no curves and 1 is the full amount of curves (no culling)
        const kodachi::FloatAttribute densityAttr =
                                          kodachi::GetGlobalAttr(interface, "curveOperations.density");
        if (!densityAttr.isValid()) {
            // nothing to do
            return;
        }

        // since density is retrieved globally, reset the value at this location
        interface.setAttr("curveOperations.density", kodachi::FloatAttribute(1.0f));

        kodachi::GroupBuilder keyBuilder;
        keyBuilder.set("density", densityAttr);

        // *** Geometry Attribute ***
        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
            KdLogWarn(" >>> Curve Density Op: Missing 'geometry' attribute.");
            return;
        }
        keyBuilder.update(geometryAttr);

        // curveOperations.seed can be specified to be used as seed for random engine
        const kodachi::Attribute seedAttr =
                  kodachi::GetGlobalAttr(interface, "curveOperations.seed");
        if (seedAttr.isValid()) {
            keyBuilder.set("seed", seedAttr);
        }

        KdLogDebug(" >>> Curve Density Op: Running curve density reduction.");
        const kodachi::GroupAttribute resultGroupAttr = reduceDensity(keyBuilder.build());
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

DEFINE_KODACHIOP_PLUGIN(CurveDensityOp)
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(CurveDensityAttrFunc)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(CurveDensityOp, "CurveDensityOp", 0, 1);
    REGISTER_PLUGIN(CurveDensityAttrFunc, "CurveDensityAttrFunc", 0, 1);
}


