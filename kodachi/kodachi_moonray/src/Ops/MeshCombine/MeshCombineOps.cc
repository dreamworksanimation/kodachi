// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute/ZeroCopyDataBuilder.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <kodachi_moonray/kodachi_geometry/GenerateUtil.h>
#include <kodachi_moonray/kodachi_geometry/PrimitiveAttributeUtil.h>

// Imath
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>

// std
#include <set>
#include <unordered_set>

namespace
{

KdLogSetup("MeshCombineOp");

// assumes points and xform has the same time samples
inline kodachi::FloatAttribute
transformPoints(const kodachi::FloatAttribute& points,
                const kodachi::DoubleAttribute& xform)
{
    const auto pointSamples = points.getSamples();

    std::vector<float> out;
    out.reserve(pointSamples.getNumberOfValues() *
            pointSamples.getNumberOfTimeSamples());

    std::vector<float> sampleTimes;
    sampleTimes.reserve(pointSamples.getNumberOfTimeSamples());

    for (const auto& sample : pointSamples) {

        sampleTimes.emplace_back(sample.getSampleTime());

        const auto& xformSample =
                xform.getNearestSample(sample.getSampleTime());

        Imath::M44d mat;
        kodachi_moonray::setXformMatrix(mat, xformSample.data());

        for (size_t i = 0; i < sample.size(); i += 3) {
            Imath::V3f pt(sample[i], sample[i+1], sample[i+2]);
            pt = pt * mat;
            out.insert(out.end(), { pt.x, pt.y, pt.z });
        }
    }
    return kodachi::ZeroCopyFloatAttribute::create(sampleTimes, out, 3);
}

using StringAttributeSet = std::unordered_set<kodachi::StringAttribute,
                                              kodachi::AttributeHash>;

// recursively find mesh locations underneath the provided locations
// and returns their geometries if they match the CEL
void
findMeshes(kodachi::OpCookInterface& interface,
           const kodachi::StringAttribute& cel,
           const kodachi::StringAttribute& locations,
           const kodachi::string_view root,
           kodachi::GroupBuilder& outGb)
{
    static const std::string kSubd("subdmesh");
    static const std::string kPoly("polymesh");

    if (locations.isValid()) {
        const auto samples = locations.getSamples();

        for (const kodachi::string_view name : samples.front()) {

            const std::string path = root.empty() ? name.data() :
                    kodachi::concat(root, "/", name);

            if (interface.doesLocationExist(path)) {
                interface.prefetch(path);

                kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
                celInfo.matches = true;
                celInfo.canMatchChildren = true;

                if (cel.isValid()) {
                    kodachi::CookInterfaceUtils::matchesCEL(
                            celInfo, interface, cel, path);
                }

                kodachi::StringAttribute type =
                        interface.getAttr("type", path);

                // currently just support meshes
                if (celInfo.matches && (type == kSubd || type == kPoly)) {
                    kodachi::GroupBuilder gb;
                    gb.set("geometry", interface.getAttr("geometry", path));
                    // xform needed to transform points to world space
                    gb.set("xform", kodachi::GetGlobalXFormGroup(interface, path));
                    outGb.set(path, gb.build());
                } else if (celInfo.canMatchChildren) {
                    // recurse on children
                    const kodachi::StringAttribute children =
                            interface.getPotentialChildren(path);
                    findMeshes(interface, cel, children, path, outGb);
                }
            }
        } // for each location
    }
}

kodachi::GroupAttribute
meshCombine(const kodachi::GroupAttribute& meshes,
            const kodachi::array_view<float> sampleTimes,
            const kodachi::GroupAttribute& arbitraryAttrWhitelistAttr)
{
    using namespace kodachi;
    using namespace kodachi_moonray;

    FloatAttribute inPoints;
    IntAttribute   inVertexList;
    IntAttribute   inStartIdx;
    GroupAttribute inArbitraryAttrs;

    std::vector<int32_t> outStartIdx { 0 };
    std::vector<int32_t> outVertexList;
    ZeroCopyFloatBuilder outPoints(3);

    std::map<string_view,
                std::unique_ptr<ArbitraryDataBuilderBase>> outArbitraryAttrMap;

    // if not empty, only process these arbitrary attrs
    std::unordered_set<string_view, StringViewHash> arbAttrWhiteSet;
    {
        if (arbitraryAttrWhitelistAttr.isValid()) {
            for (const auto& attr : arbitraryAttrWhitelistAttr) {
                StringAttribute attrName = attr.attribute;
                const string_view name(attrName.getValueCStr("", false));
                if (name.empty()) {
                    continue;
                }

                const auto pos = name.rfind(".");
                if (pos != string_view::npos) {
                    // view just the name of the arbitrary attribute,
                    // minus 'geometry.arbitrary'
                    arbAttrWhiteSet.emplace(name.data() + (pos + 1),
                                            name.size() - (pos + 1));
                } else {
                    arbAttrWhiteSet.emplace(name);
                }
            }
        }
    }

    for (const auto& mesh : meshes) {
        KdLogDebug("MeshCombine - Processing " << mesh.name);

        const GroupAttribute meshAttrs = mesh.attribute;
        if (!meshAttrs.isValid()) {
            KdLogDebug("     MeshCombine - invalid mesh encountered");
            continue;
        }

        // geometry
        const GroupAttribute geo = meshAttrs.getChildByName("geometry");
        if (!geo.isValid()) {
            KdLogDebug("     MeshCombine - invalid geometry encountered");
            continue;
        }

        const size_t pSize = outPoints.get(sampleTimes[0]).size() / 3;
        size_t pointCount = 0;
        // point.P
        {
            // we'll directly append the points list
            inPoints = geo.getChildByName("point.P");

            // interpolate to match all sample times
            inPoints = kodachi::interpToSamples(inPoints, sampleTimes, 3);

            // transform points with each mesh's xform so the combined
            // mesh will have each mesh in the correct position/orientation
            const GroupAttribute xform = meshAttrs.getChildByName("xform");

            const kodachi::DoubleAttribute xformMatrix =
                    kodachi::XFormUtil::CalcTransformMatrixAtTimes(xform,
                            sampleTimes.data(), sampleTimes.size()).first;
            inPoints = transformPoints(inPoints, xformMatrix);
            const auto pointSamples = inPoints.getSamples();

            pointCount = pointSamples.getNumberOfValues() / 3;

            // directly append at each time sample
            for (float t : sampleTimes) {
                const auto& input = pointSamples.getNearestSample(t);
                auto& output = outPoints.get(t);
                output.insert(output.end(),
                              input.begin(), input.end());
            }
        }

        // poly.vertexList
        size_t vertCount = 0;
        {
            // need to adjust the vertex indices to point accumulated points
            // list
            inVertexList = geo.getChildByName("poly.vertexList");
            const auto vertexSamples = inVertexList.getSamples();
            const auto& vertexList   = vertexSamples.front();

            vertCount = vertexSamples.getNumberOfValues();

            outVertexList.reserve(outVertexList.size() + vertexList.size());
            for (int32_t v : vertexList) {
                outVertexList.emplace_back(v + pSize);
            }
        }

        // poly.startIndex
        size_t faceCount = 0;
        {
            // for startIndex, we need to increment the index starting
            // from the last value
            inStartIdx = geo.getChildByName("poly.startIndex");
            const auto startIdxSamples = inStartIdx.getSamples();
            const auto& startIndices   = startIdxSamples.front();

            faceCount = startIdxSamples.getNumberOfValues() - 1;

            outStartIdx.reserve(outStartIdx.size() + startIndices.size());
            for (size_t i = 1; i < startIndices.size(); ++i) {
                outStartIdx.emplace_back(outStartIdx.back() +
                                        (startIndices[i] - startIndices[i-1]));
            }
        }

        // arbitrary attrs
        static const std::string kScope       ("scope");
        static const std::string kInputType   ("inputType");
        static const std::string kElementSize ("elementSize");
        static const std::string kValue       ("value");
        static const std::string kPrimitive   ("primitive");

        static const kodachi::StringAttribute kFaceScope("face");
        {
            std::string errorMsg("");

            inArbitraryAttrs = geo.getChildByName("arbitrary");
            for (const auto& arbAttrChild : inArbitraryAttrs) {

                if (!arbAttrWhiteSet.empty() &&
                        arbAttrWhiteSet.find(arbAttrChild.name) == arbAttrWhiteSet.end()) {
                    continue;
                }

                const GroupAttribute arbAttr = arbAttrChild.attribute;

                if (!validateArbitraryAttribute(arbAttr,
                        pointCount, vertCount, faceCount, errorMsg)) {
                    KdLogDebug("     MeshCombine - invalid arbitrary attribute: " <<
                            arbAttrChild.name << " ... " << errorMsg);
                    continue;
                }

                StringAttribute scopeAttr = arbAttr.getChildByName(kScope);

                // skip string typed attributes
                StringAttribute stringValue = arbAttr.getChildByName(kValue);
                if (stringValue.isValid()) {
                    KdLogDebug("     MeshCombine - Skipping string arbitrary attribute: " <<
                            arbAttrChild.name);
                    continue;
                }

                // primitive scope attributes are slightly different
                // they are no longer valid as primitive scope, so we must
                // modify them
                // set them to face scope
                // N is the repeat of the data to be appended
                size_t N = 1;
                if (scopeAttr == kPrimitive) {
                    scopeAttr = kFaceScope;
                    N = faceCount;
                }

                auto it = outArbitraryAttrMap.find(arbAttrChild.name);

                if (it == outArbitraryAttrMap.end()) {
                    // first time we're encountering this arbitrary attribute,
                    // create a builder for it and store it in the map
                    it = (outArbitraryAttrMap.emplace(
                            arbAttrChild.name,
                            initArbitraryDataBuilder(scopeAttr,
                                                     arbAttr.getChildByName(kInputType),
                                                     arbAttr.getChildByName(kElementSize)))).first;
                }

                // append the values directly
                // repeated for primitive scope -> face scope conversion
                it->second->append(arbAttr, sampleTimes, N);
            }
        }
    } // meshes loop

    GroupBuilder combinedMeshGb;

    {
        GroupBuilder arbAttrGb;
        for (const auto& pair : outArbitraryAttrMap) {
            arbAttrGb.set(pair.first, pair.second->build());
        }
        combinedMeshGb.set("arbitrary", arbAttrGb.build());
    }

    combinedMeshGb.set("point.P",         outPoints.build());
    combinedMeshGb.set("poly.vertexList", ZeroCopyIntAttribute::create(outVertexList));
    combinedMeshGb.set("poly.startIndex", ZeroCopyIntAttribute::create(outStartIdx));

    return combinedMeshGb.build();
}

class MeshCombineOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        // optional parent source path to find the meshes to merge
        kodachi::StringAttribute srcAttr = interface.getOpArg("sourceLocations");
        if (srcAttr.getNumberOfValues() < 1) {
            // by default, traverse the whole scene
            static const kodachi::StringAttribute kRoot("/root");
            srcAttr = kRoot;
        }

        // CEL to match the meshes
        const kodachi::StringAttribute celAttr = interface.getOpArg("CEL");
        if (!celAttr.isValid()) {
            kodachi::ReportWarning(interface, "'CEL' parameter is required.");
            return;
        }

        // name of result mesh
        const kodachi::StringAttribute nameAttr = interface.getOpArg("name");
        std::string name = nameAttr.getValue("", false);
        if (name.empty()) {
            static const std::string kDefaultName("combined_mesh");
            name = kDefaultName;
        }

        // arbitrary_attritbutes - if specified, only attempt to merge these
        // arbitrary attributes; if empty, attempt to merge all of them
        // by default string attributes are skipped
        const kodachi::GroupAttribute arbitraryAttrWhitelist =
                interface.getOpArg("arbitraryAttributes");

        // gather meshes to be combined
        kodachi::GroupBuilder meshesGb;

        // TODO: currently we ignore child facesets of the meshes
        // those should be brought over to the merged mesh result
        findMeshes(interface, celAttr, srcAttr, "", meshesGb);
        kodachi::GroupAttribute meshes = meshesGb.build();

        // motion blur attrs if needed
        const int numSamples = kodachi::GetNumSamples(interface);
        const float shutterOpen = kodachi::GetShutterOpen(interface);
        const float shutterClose = kodachi::GetShutterClose(interface);
        const bool mbEnabled = (numSamples > 1);

        std::vector<float> samples;
        if (mbEnabled) {
            // if motion blur is enabled, we'll match all meshes to the
            // required samples for the shutter times
            // using set here to avoid duplicates
            std::set<float> sampleTimeData;
            sampleTimeData.insert(std::floor(shutterOpen));
            sampleTimeData.insert(std::ceil(shutterOpen));
            sampleTimeData.insert(std::floor(shutterClose));
            sampleTimeData.insert(std::ceil(shutterClose));

            samples.insert(samples.end(), sampleTimeData.begin(), sampleTimeData.end());
        } else {
            samples.emplace_back(0.0f);
        }

        kodachi::AttributeSetOpArgsBuilder asb;
        asb.setCEL("//*");
        asb.setAttr("geometry", meshCombine(meshes, samples, arbitraryAttrWhitelist));

        // force the result to be polymesh
        static const kodachi::StringAttribute kPoly("polymesh");
        asb.setAttr("type", kPoly);

        // optional visibility settings
        // by default, keep the mesh visible
        const kodachi::FloatAttribute visibilityAttr = interface.getOpArg("visibility");
        if (!visibilityAttr.getValue(1.0f, false)) {
            // if not visible, turn off visibility in mesh statements
            static const kodachi::IntAttribute falseAttr(0);
            kodachi::GroupBuilder msStatementsGb;
            msStatementsGb.set("visible in camera"           , falseAttr);
            msStatementsGb.set("visible shadow"              , falseAttr);
            msStatementsGb.set("visible diffuse reflection"  , falseAttr);
            msStatementsGb.set("visible diffuse transmission", falseAttr);
            msStatementsGb.set("visible glossy reflection"   , falseAttr);
            msStatementsGb.set("visible glossy transmission" , falseAttr);
            msStatementsGb.set("visible mirror reflection"   , falseAttr);
            msStatementsGb.set("visible mirror transmission" , falseAttr);
            msStatementsGb.set("visible volume"              , falseAttr);
            asb.setAttr("moonrayStatements", msStatementsGb.build());
        }

        interface.createChild(name, "AttributeSet", asb.build());
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

DEFINE_KODACHIOP_PLUGIN(MeshCombineOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(MeshCombineOp, "MeshCombineOp", 0, 1);
}


