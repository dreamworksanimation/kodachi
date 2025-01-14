// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/ArbitraryAttribute.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <unordered_map>

namespace
{

KdLogSetup("CurveSplitOp");

struct CurveStructure {
    std::vector<float> points;
    std::vector<int32_t> numVerts;
    std::vector<float> widths;
    std::vector<int32_t> omitList;

    kodachi::GroupBuilder arbitraryAttrs;
};

template<typename T,
         typename = typename std::enable_if<
                    std::is_base_of<kodachi::DataAttribute, T>::value>::type>
void
splitAttr(const kodachi::ArbitraryAttr& attr,
          const kodachi::GroupAttribute& arbAttr,
          const kodachi::string_view attrName,
          const kodachi::IntAttribute::accessor_type& numVertSamples,
          std::unordered_map<int32_t, CurveStructure>& childCurveMap)
{
    const bool isUniform =
            (attr.mScope == kodachi::ArbitraryAttr::UNIFORM);
    const int64_t tupleSize = attr.getTupleSize();

    const auto attrSamples = attr.getValues<T>().getSamples();
    const auto& attrSample = attrSamples.front();
    std::unordered_map<int32_t, std::vector<typename T::value_type>> childAttrMap;

    auto it = attrSample.begin();

    for (const auto numV : numVertSamples.front()) {
        const size_t size = isUniform ? tupleSize :
                            numV * tupleSize;
        childAttrMap[numV].insert(
                childAttrMap[numV].end(), it, it + size);
        it += size;
    }

    for (const auto& pair : childAttrMap) {
        kodachi::GroupBuilder gb;
        gb.update(arbAttr);
        gb.set(attr.getValueName(),
                kodachi::ZeroCopyAttribute<T>::create(pair.second, tupleSize));
        childCurveMap[pair.first].arbitraryAttrs.set(attrName, gb.build());
    }
}

// acts on locations with the curveOperations.split attribute
// for each child curve location, splits them out into smaller curve locations
// categorized by the numVertices of the curves
// TODO: we can explore different ways to control the split, ie. with user input, etc
class CurveSplitOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.getAttr("curveOperations.split").isValid()) {

            interface.deleteAttr("curveOperations.split");

            const auto potentialChildrenSamples =
                    interface.getPotentialChildren().getSamples();

            // acts on child locations
            if (potentialChildrenSamples.isValid() &&
                    potentialChildrenSamples.getNumberOfTimeSamples() > 0 &&
                    potentialChildrenSamples.getNumberOfValues() > 0) {

                // whether or not to delete the original child location
                const bool deleteOriginal =
                        kodachi::IntAttribute(interface.getOpArg("delete_original")).getValue(true, false);
                // if true, randomly colorizes (viewer options) each child location
                const bool colorize =
                        kodachi::IntAttribute(interface.getOpArg("colorize")).getValue(true, false);

                for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                    interface.prefetch(childName);

                    const kodachi::StringAttribute typeAttr =
                            interface.getAttr("type", childName);
                    if (typeAttr != "curves") {
                        continue;
                    }

                    const kodachi::GroupAttribute geometryAttr =
                            interface.getAttr("geometry", childName);

                    if (!geometryAttr.isValid()) {
                        continue;
                    }

                    const auto numVertSamples =
                            kodachi::IntAttribute(geometryAttr.getChildByName("numVertices")).getSamples();
                    const auto pointSamples =
                            kodachi::FloatAttribute(geometryAttr.getChildByName("point.P")).getSamples();
                    const auto widthSamples =
                              kodachi::FloatAttribute(geometryAttr.getChildByName("point.width")).getSamples();

                    // TODO: omit list

                    // geometry of split children
                    std::unordered_map<int32_t, CurveStructure> childCurveMap;

                    // numVerts
                    for (const auto numV : numVertSamples.front()) {
                        childCurveMap[numV].numVerts.push_back(numV);
                    }

                    // points
                    for (const auto& sample : pointSamples) {
                        auto it = sample.begin();

                        for (const auto numV : numVertSamples.front()) {
                            const size_t size = numV * 3;
                            childCurveMap[numV].points.insert(
                                    childCurveMap[numV].points.end(), it, it + size);
                            it += size;
                        }
                    }

                    // widths
                    for (const auto& sample : widthSamples) {
                        auto it = sample.begin();

                        for (const auto numV : numVertSamples.front()) {
                            childCurveMap[numV].widths.insert(
                                    childCurveMap[numV].widths.end(), it, it + numV);
                            it += numV;
                        }
                    }

                    // arbitrary attrs
                    kodachi::GroupAttribute arbitraryAttrs =
                            geometryAttr.getChildByName("arbitrary");
                    for (const auto& arbAttr : arbitraryAttrs) {
                        kodachi::ArbitraryAttr attr(arbAttr.attribute);
                        if (!attr.isValid() || attr.isIndexed() ||
                                attr.mScope == kodachi::ArbitraryAttr::CONSTANT) {
                            continue;
                        }

                        switch (attr.getValueType()) {
                        case kodachi::kAttrTypeInt:
                            splitAttr<kodachi::IntAttribute>(attr, arbAttr.attribute, arbAttr.name,
                                                             numVertSamples, childCurveMap);
                            break;
                        case kodachi::kAttrTypeFloat:
                            splitAttr<kodachi::FloatAttribute>(attr, arbAttr.attribute, arbAttr.name,
                                                               numVertSamples, childCurveMap);
                            break;
                        case kodachi::kAttrTypeDouble:
                            splitAttr<kodachi::DoubleAttribute>(attr, arbAttr.attribute, arbAttr.name,
                                                                numVertSamples, childCurveMap);
                            break;
                        default:
                            KdLogWarn("[Expanding Arbitrary Attributes] Unexpected type encountered.");
                            break;
                        }
                    }

                    const kodachi::GroupAttribute xformAttr =
                            interface.getAttr("xform", childName);

                    // create the split locations
                    for (auto& pair : childCurveMap) {
                        static const kodachi::StringAttribute kAttributeSetCELAttr("//*");
                        kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
                        asb.setCEL(kAttributeSetCELAttr);
                        asb.setAttr("geometry.numVertices",
                                kodachi::ZeroCopyIntAttribute::create(pair.second.numVerts));
                        asb.setAttr("geometry.point.P",
                                kodachi::ZeroCopyFloatAttribute::create(
                                        pointSamples.getSampleTimes(), pair.second.points, 3));
                        asb.setAttr("geometry.point.width",
                                kodachi::ZeroCopyFloatAttribute::create(
                                        widthSamples.getSampleTimes(), pair.second.widths));
                        asb.setAttr("geometry.arbitrary", pair.second.arbitraryAttrs.build());
                        asb.setAttr("type", kodachi::StringAttribute("curves"));
                        asb.setAttr("geometry.degree", geometryAttr.getChildByName("degree"));
                        asb.setAttr("geometry.basis", geometryAttr.getChildByName("basis"));
                        asb.setAttr("geometry.closed", geometryAttr.getChildByName("closed"));
                        asb.setAttr("geometry.knots", geometryAttr.getChildByName("knots"));

                        asb.setAttr("xform", xformAttr);

                        if (colorize) {
                            std::ranlux24 randGenerator(pair.first);
                            std::normal_distribution<float> dist(5, 3);

                            std::vector<float> color = {
                                    dist(randGenerator)/10.5f,
                                    dist(randGenerator)/10.5f,
                                    dist(randGenerator)/10.5f, 1.0f };
                            asb.setAttr("viewer.default.drawOptions.color",
                                    kodachi::FloatAttribute(color.data(), 4, 4));
                        }

                        interface.createChild(kodachi::concat(childName, "_", std::to_string(pair.first)),
                                "AttributeSet", asb.build());
                    }

                    if (deleteOriginal) {
                        interface.deleteChild(childName);
                    }
                }
            }
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

DEFINE_KODACHIOP_PLUGIN(CurveSplitOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(CurveSplitOp, "CurveSplitOp", 0, 1);
}


