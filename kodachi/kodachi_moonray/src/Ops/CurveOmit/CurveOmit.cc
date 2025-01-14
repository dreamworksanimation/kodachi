// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// kodachi
#include <kodachi/op/Op.h>

#include <kodachi/attribute/ArbitraryAttribute.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionPlugin.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

// std
#include <set>

namespace {

KdLogSetup("CurveOmit");

// returns a new attribute based on a 'keep list' indexing into
// the incoming attribute
template <class ATTR_T>
ATTR_T
omitAttribute(const ATTR_T& inAttr,
              // list of indices to inAttr that points to the data to keep
              const std::vector<int32_t>& keepList,
              const int64_t tupleSize = 1)
{
    const auto samples = inAttr.getSamples();
    const auto sampleTimes = samples.getSampleTimes();
    const std::size_t numSamples = sampleTimes.size();

    std::vector<typename ATTR_T::value_type> outData;
    outData.reserve(keepList.size() * tupleSize * numSamples);
    for (const auto& sample : samples) {
        for (const int32_t idx : keepList) {
            const auto iter = std::begin(sample) + idx * tupleSize;
            outData.insert(outData.end(), iter, iter + tupleSize);
        }
    }

    return kodachi::ZeroCopyAttribute<ATTR_T>::create(
                                sampleTimes, std::move(outData), tupleSize);
}

// string specialization
template <>
kodachi::StringAttribute
omitAttribute(const kodachi::StringAttribute& inAttr,
              // list of indices to inAttr that points to the data to keep
              const std::vector<int32_t>& keepList,
              const int64_t tupleSize)
{
    const auto samples = inAttr.getSamples();
    const auto sampleTimes = samples.getSampleTimes();
    const std::size_t numSamples = sampleTimes.size();
    const std::size_t numValues = samples.getNumberOfValues();

    std::vector<const char*> outData;
    outData.reserve(keepList.size() * tupleSize * numSamples);
    for (const auto& sample : samples) {
        for (const int32_t idx : keepList) {
            const auto iter = std::begin(sample) + idx * tupleSize;
            outData.insert(outData.end(), iter, iter + tupleSize);
        }
    }

    std::vector<const char**> values(numSamples);
    for (std::size_t i = 0; i < numSamples; ++i) {
        values[i] = outData.data() + i * numValues;
    }

    return kodachi::StringAttribute(sampleTimes.data(),
                                    sampleTimes.size(),
                                    values.data(),
                                    keepList.size() * tupleSize,
                                    tupleSize);
}

kodachi::GroupAttribute
curveOmit(const kodachi::GroupAttribute& geometryAttr)
{
    // *** omit list ***
    // list of integer indices to cull out
    // currently, this has the scope of per-points (can omit individual CV's)
    const kodachi::IntAttribute omitListAttr =
            geometryAttr.getChildByName("omitList");
    if (!omitListAttr.isValid()) {
        KdLogDebug(" >>> Curve Omit: Empty omit list, nothing to do.");
        return {};
    }

    const auto omitListSamples = omitListAttr.getSamples();
    const auto& omitListT0 = omitListSamples.front();
    std::set<int32_t> omitList(omitListT0.begin(), omitListT0.end());
    if (omitList.empty()) {
        KdLogDebug(" >>> Curve Omit: Empty omit list, nothing to do.");
        return {};
    }

    KdLogDebug(" >>> Curve Omit: Running curve omit.");

    // *** Geometry Attribute ***
    // points
    const kodachi::FloatAttribute pointsAttr = geometryAttr.getChildByName("point.P");

    // num vertices (per curve)
    const kodachi::IntAttribute numVertsAttr = geometryAttr.getChildByName("numVertices");
    if (numVertsAttr.getNumberOfValues() <= 0) {
        KdLogWarn(" >>> Curve Omit: 'numVertices' attr is empty or invalid.");
        return {};
    }

    // widths (per point)
    const kodachi::FloatAttribute widthAttr = geometryAttr.getChildByName("point.width");

    const int32_t basis =
            kodachi::IntAttribute(geometryAttr.getChildByName("basis")).getValue(0, false);

    // if the resulting CV's can't satisfy cubic or bezier requirements,
    // we'll need to force it to be linear
    bool forceLinear = false;

    // num verts - new num vertices will be updated depending on which cv's are omitted
    // whole curves are deleted if all cv's are omitted in the curve
    const auto numVertsSamples = numVertsAttr.getSamples();
    const auto& numVerts = numVertsSamples.front();
    std::vector<int32_t> outNumVerts;
    outNumVerts.reserve(numVerts.size());

    // list of indices to keep (points)
    // used to omit each attribute later
    std::vector<int32_t> keepList;
    keepList.reserve(pointsAttr.getNumberOfTuples());

    // keep list that points to numVertices
    // in cases where whole curves are removed
    std::vector<int32_t> curveKeepList;
    curveKeepList.reserve(numVerts.size());

    int32_t pIdx = 0; // index into points
    int32_t cIdx = 0; // index into curves (numVertices)
    for (const int32_t cvCount : numVerts) {
        int32_t resultCv = 0; // resulting cv's that we kept for this curve

        // for cv's in this curve
        for (int32_t cv = 0; cv < cvCount; ++cv) {
            // keep the point index
            // if we don't find it in the omit list
            if (omitList.find(pIdx) == omitList.end()) {
                keepList.push_back(pIdx);
                resultCv++; // keeping this cv
            }
            pIdx++;
        }

        // if we end up omitting all cv's of this curve,
        // remove the curve entirely
        // if there's only one CV left (invalid), also remove this curve
        if (resultCv > 1) {
            // TODO: should we seperate the linear and bezier
            // curves into separate geometry locations, so we don't need
            // to lose unnecessary detail

            // for Moonray, cubic curves (bezier, b-spline) must have
            // at least 4 cv's
            // if basis is 1 (bezier), the cv's must satisfy 3*k+1 requirement
            if (resultCv < 4 ||
                    (basis == 1 && ((resultCv - 1) % 3) != 0)) {
                forceLinear = true;
            }

            outNumVerts.push_back(resultCv);
            curveKeepList.push_back(cIdx);
        } else if (resultCv == 1) {
            // remove the last CV we just pushed since we're
            // deleting this invalid curve
            keepList.erase(keepList.end() - 1);
        }
        cIdx++;
    }

    // *** output Gb ***
    kodachi::GroupBuilder geometryGb;
    geometryGb.setGroupInherit(false).update(geometryAttr);

    // *** omit attributes ***
    // numVertices
    {
        // all curves have been removed, no need to process anything else
        // as this location will be deleted
        if (outNumVerts.empty()) {
            geometryGb.set("numVertices", kodachi::IntAttribute());
            return geometryGb.build();
        }

        geometryGb.set("numVertices",
                kodachi::ZeroCopyIntAttribute::create(std::move(outNumVerts)));
    }

    // point.P
    if (pointsAttr.isValid()) {
        geometryGb.set("point.P", omitAttribute(pointsAttr, keepList, 3));
    }

    // point.width
    if (widthAttr.isValid()) {
        geometryGb.set("point.width", omitAttribute(widthAttr, keepList));
    }

    // degree
    if (forceLinear) {
        geometryGb.set("degree", kodachi::IntAttribute(1));
        geometryGb.set("basis", kodachi::IntAttribute(0));
    }

    // arbitrary attrs
    {
        // arbitary attrs
        const kodachi::GroupAttribute arbAttrsGroup = geometryAttr.getChildByName("arbitrary");
        for (const auto child : arbAttrsGroup) {
            const kodachi::GroupAttribute arbAttrGroupAttr(child.attribute);

            const kodachi::ArbitraryAttr arbAttr(arbAttrGroupAttr);
            if (!arbAttr.isValid()) {
                continue;
            }

            // scope
            bool isUniform;
            if (arbAttr.mScope == kodachi::ArbitraryAttr::UNIFORM) {
                // for uniform scope, we use the curveKeepList
                isUniform = true;
            } else if (arbAttr.mScope == kodachi::ArbitraryAttr::VERTEX ||
                    arbAttr.mScope == kodachi::ArbitraryAttr::POINT) {
                // for point scope, we use the keepList
                isUniform = false;
            } else {
                // don't need to process primitive scope
                continue;
            }

            // if the attr is indexed, just omit the index list
            const bool isIndexed = arbAttr.isIndexed();
            if (isIndexed) {
                const std::string attrName =
                        kodachi::concat("arbitrary.", child.name, ".index");

                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getIndex(),
                                (isUniform ? curveKeepList : keepList)));

                continue;
            }

            // otherwise omit the values by type
            const std::string attrName =
                    kodachi::concat("arbitrary.", child.name, ".value");
            const int64_t tupleSize = arbAttr.getTupleSize();

            switch (arbAttr.getValueType()) {
            case kodachi::kAttrTypeInt:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::IntAttribute>(),
                                (isUniform ? curveKeepList : keepList), tupleSize));
                break;
            case kodachi::kAttrTypeFloat:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::FloatAttribute>(),
                                (isUniform ? curveKeepList : keepList), tupleSize));
                break;
            case kodachi::kAttrTypeDouble:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::DoubleAttribute>(),
                                (isUniform ? curveKeepList : keepList), tupleSize));
                break;
            case kodachi::kAttrTypeString:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::StringAttribute>(),
                                (isUniform ? curveKeepList : keepList), tupleSize));
                break;
            }
        } // arbitrary attribute loop
    } // arbitrary attrs

    geometryGb.del("omitList");
    return geometryGb.build();
}

// Attribute function for performing curve omit on arbitrary geometry attribute
// expects input attrs:
//  - omitList (required)
//  - numVertices (required)
//  - point.P
//  - point.width
//  - basis
//  - arbitrary
// returns: GroupAttribute updated geometry attributes
//          if all curves have been removed, returns a single empty numVertices attr
class CurveOmitAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        kodachi::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            KdLogDebug(" >>> Curve Omit Attribute Func: Running curve omit.");
            return curveOmit(rootAttr);
        }
        KdLogDebug(" >>> Curve Omit Attribute Func: Input is invalid.");
        return {};
    }
};

class CurveOmit : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kDefaultCELAttr(
                R"(/root/world/geo//*{@type=="curves"})");

        kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        if (!celAttr.isValid()) {
            celAttr = kDefaultCELAttr;
        }

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, celAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");

        const kodachi::GroupAttribute resultAttr = curveOmit(geometryAttr);
        if (!resultAttr.isValid()) {
            return;
        }

        const kodachi::IntAttribute numVertsAttr =
                resultAttr.getChildByName("numVertices");
        if (numVertsAttr.getNumberOfValues() <= 0) {
            // all curves have been removed
            KdLogDebug(" >>> Curve Omit Op: All curves have been omitted.");
            interface.deleteSelf();
            return;
        }

        interface.setAttr("geometry", resultAttr, false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Omits CV's on a curves location based on the geometry.omitList attribute.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(CurveOmit)
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(CurveOmitAttrFunc)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(CurveOmit, "CurveOmit", 0, 1);
    REGISTER_PLUGIN(CurveOmitAttrFunc, "CurveOmitAttrFunc", 0, 1);
}

