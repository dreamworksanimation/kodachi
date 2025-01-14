// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// kodachi
#include <kodachi/op/Op.h>

#include <kodachi/attribute/ArbitraryAttribute.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

// std
#include <set>

namespace {

KdLogSetup("InstanceOmit");

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
instanceOmit(const kodachi::GroupAttribute& geometryAttr)
{
    // *** omit list ***
    // list of integer indices to cull out
    const kodachi::IntAttribute omitListAttr =
            geometryAttr.getChildByName("omitList");
    if (!omitListAttr.isValid()) {
        KdLogDebug(" >>> Instance Omit: Empty omit list, nothing to do.");
        return {};
    }

    const auto omitListSamples = omitListAttr.getSamples();
    const auto& omitListT0 = omitListSamples.front();
    std::set<int32_t> omitList(omitListT0.begin(), omitListT0.end());
    if (omitList.empty()) {
        KdLogDebug(" >>> Instance Omit: Empty omit list, nothing to do.");
        return {};
    }

    KdLogDebug(" >>> Instance Omit: Running instance omit.");

    // *** Geometry Attribute ***
    // instance indices
    const kodachi::IntAttribute instanceIndexAttr = geometryAttr.getChildByName("instanceIndex");
    if (instanceIndexAttr.getNumberOfValues() <= 0) {
        KdLogWarn(" >>> Instance Omit: 'instanceIndex' attr is empty or invalid.");
        return {};
    }

    // instance matrices (same number as indices)
    const kodachi::DoubleAttribute instanceMatrixAttr = geometryAttr.getChildByName("instanceMatrix");
    if (instanceMatrixAttr.getNumberOfValues() <= 0) {
        KdLogWarn(" >>> Instance Omit: 'instanceMatrix' attr is empty or invalid.");
        return {};
    }

    // number of indices and size of matrix
    const auto indexSamples = instanceIndexAttr.getSamples();
    const auto& numIndex = indexSamples.front();
    const int matSize = 16;

    // list of indices to keep
    // used to omit each attribute later
    std::vector<int32_t> keepList;
    keepList.reserve(indexSamples.size());

    int32_t idx = 0; // counter
    for (const int32_t i : numIndex) {
        // check if index is in omitList, if not add to keepList
        if (omitList.find(idx) == omitList.end()) {
            keepList.push_back(idx);
        }

        idx++;
    }

    // *** output Gb ***
    kodachi::GroupBuilder geometryGb;
    geometryGb.setGroupInherit(false).update(geometryAttr);

    // instance indices
    if (instanceIndexAttr.isValid()) {
        geometryGb.set("instanceIndex", omitAttribute(instanceIndexAttr, keepList));
    }

    // instance matrices (4x4 transformation matrix)
    if (instanceMatrixAttr.isValid()) {
        geometryGb.set("instanceMatrix", omitAttribute(instanceMatrixAttr, keepList, matSize));
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

            // scope - for now, all arbitrary attrs are primitive-scope, meaning one per instance.
            // in the future if this changes we will have to account for different scopes

            // if the attr is indexed, just omit the index list
            const bool isIndexed = arbAttr.isIndexed();
            if (isIndexed) {
                const std::string attrName =
                        kodachi::concat("arbitrary.", child.name, ".index");

                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getIndex(), keepList));

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
                                keepList, tupleSize));
                break;
            case kodachi::kAttrTypeFloat:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::FloatAttribute>(),
                                keepList, tupleSize));
                break;
            case kodachi::kAttrTypeDouble:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::DoubleAttribute>(),
                                keepList, tupleSize));
                break;
            case kodachi::kAttrTypeString:
                geometryGb.set(attrName,
                        omitAttribute(arbAttr.getValues<kodachi::StringAttribute>(),
                                keepList, tupleSize));
                break;
            default:
                KdLogWarn(" >>> Instance Omit: Unrecognized attr type for arbitrary attrs.");
            }
        } // arbitrary attribute loop
    } // arbitrary attrs

    geometryGb.del("omitList");
    return geometryGb.build();
}

class InstanceOmit : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kDefaultCELAttr(
                R"(/root/world/geo//*{@type=="instance array"})");

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

        const kodachi::GroupAttribute resultAttr = instanceOmit(geometryAttr);
        if (!resultAttr.isValid()) {
            return;
        }

        const kodachi::IntAttribute instanceIndexAttr =
                resultAttr.getChildByName("instanceIndex");
        if (instanceIndexAttr.getNumberOfValues() <= 0) {
            // all instances have been removed
            KdLogDebug(" >>> Instance Omit Op: All instances have been omitted.");
            interface.deleteSelf();
            return;
        }

        interface.setAttr("geometry", resultAttr, false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Omits instances based on the geometry.omitList attribute.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(InstanceOmit)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(InstanceOmit, "InstanceOmit", 0, 1);
}

