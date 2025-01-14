// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/CookInterfaceUtils.h>

namespace {

KdLogSetup("KPOPMeshWindingOrder");

using WindingOrderVec = std::vector<std::size_t>;

// Creates an index for remapping values in an array with per-vertex data
// Used when we need to reverse the winding order of a mesh
WindingOrderVec
createWindingOrderRemap(const kodachi::IntAttribute::accessor_type startIndexSamples,
                        const std::size_t numVertices)
{
    const auto& startIndex = startIndexSamples.front();

    // The indicies for a face are from [startIndex[i], startIndex[i+1])
    const std::size_t numFaces = startIndex.size() - 1;

    WindingOrderVec windingOrderRemap(numVertices);

    for (std::size_t i = 0; i < numFaces; ++i) {
        const std::size_t faceIndexBegin = startIndex[i];
        std::size_t faceIndexEnd = startIndex[i + 1];

        auto vertexBegin = std::begin(windingOrderRemap) + faceIndexBegin;
        auto vertexEnd   = std::begin(windingOrderRemap) + faceIndexEnd;

        // reverse the indices
        for (;vertexBegin != vertexEnd; ++vertexBegin) {
            *vertexBegin = --faceIndexEnd;
        }
    }

    return windingOrderRemap;
}

// Remaps Single and multi-sampled Number attributes without any interpolation
template <class attribute_type>
attribute_type
remapAttr(const attribute_type& attr,
          const WindingOrderVec& windingOrderRemap,
          const kodachi::string_view& attrName)
{
	// Nothing to work with, return!
	if (windingOrderRemap.empty()) {
		return { };
	}

    const auto samples = attr.getSamples();

    const std::size_t numValues = samples.getNumberOfValues();
    const auto sampleTimes = samples.getSampleTimes();
    const std::size_t numSamples = sampleTimes.size();

    // Don't trust tuple size from the attribute_type, since they could be
    // incorrectly set. Calculate it based on data size

    // first check if number of values is evenly divisible by the
    // windingOrderRemap size
    if (numValues % windingOrderRemap.size() != 0) {
        KdLogError("The number of values(" << numValues << ") in attr " << attrName <<
                   " is not divisible by the number of vertices(" << windingOrderRemap.size() << ")" <<
                   " required for remapping");
        // return an invalid attr
        return attribute_type();
    }

    const std::size_t tupleSize = numValues / windingOrderRemap.size();

    std::vector<typename attribute_type::value_type> remappedData;
    remappedData.reserve(numValues * numSamples);

    for (const auto sample : samples) {
        for(const std::size_t idx : windingOrderRemap) {
            const auto iter = std::begin(sample) + idx * tupleSize;
            remappedData.insert(remappedData.end(), iter, iter + tupleSize);
        }
    }

    return kodachi::ZeroCopyAttribute<attribute_type>::create(
                               sampleTimes, std::move(remappedData), tupleSize);
}

// Specialization for StringAttribute. Instead of using the ZeroCopy constructor,
// manipulate the existing const char* values and use the Multi-Sampled constructor
// to make copies of the strings.
template <>
kodachi::StringAttribute
remapAttr(const kodachi::StringAttribute& attr,
          const WindingOrderVec& windingOrderRemap,
          const kodachi::string_view& attrName)
{
    const auto samples = attr.getSamples();

    const std::size_t numValues = samples.getNumberOfValues();
    const auto sampleTimes = samples.getSampleTimes();
    const std::size_t numSamples = sampleTimes.size();

    if (numValues % windingOrderRemap.size() != 0) {
        KdLogError("The number of values(" << numValues << ") in string attr " << attrName <<
                   " is not divisible by the number of vertices(" << windingOrderRemap.size() << ")" <<
                   " required for remapping");
        // return an invalid attr
        return kodachi::StringAttribute();
    }

    // Don't trust tuple size from the attribute_type, since they could be
    // incorrectly set. Calculate it based on data size
    const std::size_t tupleSize = numValues / windingOrderRemap.size();

    std::vector<const char*> remappedData;
    remappedData.reserve(numValues * numSamples);

    for (const auto sample : samples) {
        for(const std::size_t idx : windingOrderRemap) {
            const auto iter = std::begin(sample) + idx * tupleSize;
            remappedData.insert(remappedData.end(), iter, iter + tupleSize);
        }
    }

    std::vector<const char**> values(numSamples);
    for (std::size_t i = 0; i < numSamples; ++i) {
        values[i] = remappedData.data() + i * numValues;
    }

    return kodachi::StringAttribute(sampleTimes.data(),
                                    sampleTimes.size(),
                                    values.data(),
                                    numValues,
                                    tupleSize);
}

class KPOPMeshWindingOrder : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and hasattr("rdl2.meta.isMesh")})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::IntAttribute reverseWindingOrderAttr =
                interface.getAttr("moonrayMeshStatements.reverse winding order");

        // Always remove the attr if it exists since we are handling it here.
        if (reverseWindingOrderAttr.isValid()) {
            interface.deleteAttr("moonrayMeshStatements.reverse winding order");
        }

        // If set to false there is nothing else to do.
        if (!reverseWindingOrderAttr.getValue(true, false)) {
            return;
        }

        // Make sure we have the require attributes before continuing
        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");

        const kodachi::IntAttribute startIndexAttr =
                geometryAttr.getChildByName("poly.startIndex");

        const kodachi::IntAttribute vertexListAttr =
                geometryAttr.getChildByName("poly.vertexList");

        if (!startIndexAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "Missing poly.startIndex attribute");
            return;
        }

        if (startIndexAttr.getNumberOfTimeSamples() != 1) {
            KdLogWarn("poly.startIndex is multi-sampled, using first sample");
        }

        if (!vertexListAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "Missing poly.vertexList attribute");
            return;
        }

        if (vertexListAttr.getNumberOfTimeSamples() != 1) {
            KdLogWarn("poly.vertexList is multi-sampled, using first sample");
        }

        kodachi::GroupBuilder geometryGb;
        geometryGb.setGroupInherit(false).update(geometryAttr);

        const WindingOrderVec windingOrderVec =
                createWindingOrderRemap(startIndexAttr.getSamples(),
                                        vertexListAttr.getNumberOfValues());

        // poly.vertexList
        {
            geometryGb.set("poly.vertexList", remapAttr(vertexListAttr, windingOrderVec, "poly.vertexList"));
        }

        // vertex.N
        {
            const kodachi::FloatAttribute vertexNormalAttr =
                    geometryAttr.getChildByName("vertex.N");

            if (vertexNormalAttr.isValid()) {
                geometryGb.set("vertex.N", remapAttr(vertexNormalAttr, windingOrderVec, "vertex.N"));
            }
        }

        // arbitrary attrs
        {
            const kodachi::GroupAttribute arbitraryAttrs =
                    geometryAttr.getChildByName("arbitrary");

            for (const auto child : arbitraryAttrs) {
                const kodachi::GroupAttribute arbAttr(child.attribute);
                const auto arbAttrName = child.name;
                const kodachi::StringAttribute scopeAttr = arbAttr.getChildByName("scope");
                // winding order only applied to attributes of scope vertex
                static const kodachi::StringAttribute kVertex("vertex");
                if (scopeAttr == kVertex) {
                    // if the attribute is indexed then remap the index
                    const kodachi::IntAttribute indexAttr = arbAttr.getChildByName("index");
                    if (indexAttr.isValid()) {
                        const std::string attrName =
                                kodachi::concat("arbitrary.", child.name, ".index");

                        geometryGb.set(attrName, remapAttr(indexAttr, windingOrderVec, arbAttrName));
                    } else {
                        // remap the value
                        const kodachi::Attribute valueAttr = arbAttr.getChildByName("value");
                        if (valueAttr.isValid()) {
                            const std::string attrName =
                                    kodachi::concat("arbitrary.", child.name, ".value");

                            const std::string sceneObjectName =
                                    interface.getInputLocationPath();

                            switch (valueAttr.getType()) {
                            case kFnKatAttributeTypeInt:
                                geometryGb.set(attrName,
                                        remapAttr(kodachi::IntAttribute(valueAttr),
                                                  windingOrderVec,
                                                  arbAttrName));
                                break;
                            case kFnKatAttributeTypeFloat:
                            {
                                geometryGb.set(attrName,
                                        remapAttr(kodachi::FloatAttribute(valueAttr),
                                                  windingOrderVec,
                                                  arbAttrName));
                                break;
                            }
                            case kFnKatAttributeTypeDouble:
                                geometryGb.set(attrName,
                                        remapAttr(kodachi::DoubleAttribute(valueAttr),
                                                  windingOrderVec,
                                                  arbAttrName));
                                break;
                            case kFnKatAttributeTypeString:
                                geometryGb.set(attrName,
                                        remapAttr(kodachi::StringAttribute(valueAttr),
                                                  windingOrderVec,
                                                  arbAttrName));
                                break;
                            }
                        }
                    }
                }
            }
        }

        interface.setAttr("geometry", geometryGb.build(), false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Reverses the winding order of vertex-scope attributes on Mesh types where 'moonrayMeshStatements.reverse winding order' is set.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPMeshWindingOrder)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPMeshWindingOrder, "KPOPMeshWindingOrder", 0, 1);
}

