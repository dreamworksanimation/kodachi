// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnAttribute/FnGroupBuilder.h>

#include <cassert>
#include <cmath>
#include <random>

#include <OpenEXR/ImathMatrix.h>
#include <OpenEXR/ImathQuat.h>
#include <OpenEXR/ImathColorAlgo.h>

namespace {

constexpr float TWO_PI = 2.f * M_PI;

inline Imath::M44f
arrayToImathMatrix44(const double* arr)
{
    if (arr == nullptr) {
        return {};
    }

    return { static_cast<float>(arr[ 0]), static_cast<float>(arr[ 1]),
             static_cast<float>(arr[ 2]), static_cast<float>(arr[ 3]),
             static_cast<float>(arr[ 4]), static_cast<float>(arr[ 5]),
             static_cast<float>(arr[ 6]), static_cast<float>(arr[ 7]),
             static_cast<float>(arr[ 8]), static_cast<float>(arr[ 9]),
             static_cast<float>(arr[10]), static_cast<float>(arr[11]),
             static_cast<float>(arr[12]), static_cast<float>(arr[13]),
             static_cast<float>(arr[14]), static_cast<float>(arr[15]) };
}

struct VisualizeVectorsOp : public FnKat::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(
                   Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        using namespace FnAttribute;

        const GroupAttribute geometryAttr = interface.getAttr("geometry");
        const FloatAttribute pointAttr = geometryAttr.getChildByName("point.P");
        if (!pointAttr.isValid()) {
            std::cerr << "point.P attribute required\n";
            return;
        }

        const FloatAttribute::array_type points = pointAttr.getNearestSample(0.f);
        const int64_t numPoints = pointAttr.getNumberOfTuples();

        const StringAttribute vectorAttrNameAttr(interface.getOpArg("attrName"));
        const FloatAttribute vectorAttr = interface.getAttr(vectorAttrNameAttr.getValue());
        const int64_t numVectors = vectorAttr.getNumberOfTuples();

        // length of displaying vectors
        const FloatAttribute lengthAttr(interface.getOpArg("length"));
        float length = lengthAttr.getValue(1.f, false);

        const FloatAttribute::array_type vectors = vectorAttr.getNearestSample(0.f);

        GroupBuilder geometryBuilder;
        geometryBuilder.set("degree", IntAttribute(1));
        geometryBuilder.set("constantWidth", FloatAttribute(0.01));

        // create curves representing the vectors pointing from the points
        std::vector<int> numVertices(numVectors, 2);
        geometryBuilder.set("numVertices", IntAttribute(numVertices.data(), numVertices.size(), 1));

        std::vector<Imath::V3f> pointP;
        pointP.reserve(numVectors * 2);

        // varying per point
        if (numVectors == numPoints) {
            for (size_t i = 0; i < numPoints; ++i) {
                const size_t pIdx = 3 * i;
                pointP.emplace_back(points[pIdx], points[pIdx + 1], points[pIdx + 2]);

                const auto& pt = pointP.back();
                pointP.emplace_back(pt.x + (vectors[pIdx]*length),
                                    pt.y + (vectors[pIdx + 1]*length),
                                    pt.z + (vectors[pIdx + 2]*length));
            }
        } else {
            const IntAttribute vertexAttr = geometryAttr.getChildByName("poly.vertexList");
            if (!vertexAttr.isValid()) {
                std::cerr << "poly.vertexList attribute was required, but not found.\n";
                return;
            }
            const IntAttribute::array_type verts = vertexAttr.getNearestSample(0.f);

            if (numVectors != vertexAttr.getNumberOfTuples()) {
                std::cerr << "vector attr must be either point varying or vertex varying.\n";
                return;
            }

            for (size_t i = 0; i < vertexAttr.getNumberOfTuples(); ++i) {
                const size_t pIdx = 3 * verts[i];
                pointP.emplace_back(points[pIdx], points[pIdx + 1], points[pIdx + 2]);

                const size_t vIdx = 3 * i;
                const auto& pt = pointP.back();
                pointP.emplace_back(pt.x + (vectors[vIdx]*length),
                                    pt.y + (vectors[vIdx + 1]*length),
                                    pt.z + (vectors[vIdx + 2]*length));
            }
        }

        geometryBuilder.set("point.P", FloatAttribute(pointP.front().getValue(), pointP.size() * 3, 3));

        static const std::string childName("vectors");
        FnGeolibServices::StaticSceneCreateOpArgsBuilder sscb(false);

        sscb.setAttrAtLocation(childName, "type", StringAttribute("curves"));
        sscb.setAttrAtLocation(childName, "geometry", geometryBuilder.build());

        interface.execOp("StaticSceneCreate", sscb.build());
    }
};

struct GenerateRandomColorsOp : public FnKat::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(
                   Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        using namespace FnAttribute;

        const StringAttribute attrName = interface.getOpArg("attrName");
        const StringAttribute colorName = interface.getOpArg("colorName");
        const DataAttribute attrData = interface.getAttr(attrName.getValue());
        const int64_t numColors = attrData.getNumberOfTuples();

        std::vector<Imath::V3f> colors;
        colors.reserve(numColors);

        const size_t locationHash =
                             std::hash<std::string>()(interface.getInputLocationPath());
                std::mt19937_64 randomEngine(locationHash);
        std::uniform_real_distribution<float> dist(0.f, std::nextafter(1.f, 2.f));
        for (int64_t i = 0; i < numColors; ++i) {
            colors.push_back(Imath::hsv2rgb(Imath::V3f(dist(randomEngine),1,0.5)));
        }

        GroupBuilder gb;
        gb.set("scope", StringAttribute("point"));
        gb.set("outputType", StringAttribute("color3"));
        gb.set("value", FloatAttribute(colors.front().getValue(), colors.size() * 3, 3));

        interface.setAttr("geometry.arbitrary." + colorName.getValue(), gb.build());
    }
};

struct PointCloudToInstanceArrayOp : public FnKat::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(
                   Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        using namespace FnAttribute;

        const std::string inputType = FnKat::GetInputLocationType(interface);
        if (inputType != "pointcloud") {
            return;
        }

        const StringAttribute sourceAttr = interface.getOpArg("instanceSource");
        if (!sourceAttr.isValid()) {
            FnKat::ReportWarning(interface, "'instanceSource' attr is missing");
        }
        const IntAttribute    indexAttr = interface.getOpArg("instanceIndex");

        const GroupAttribute geometryAttr = interface.getAttr("geometry");
        const FloatAttribute pointAttr = geometryAttr.getChildByName("point.P");
        const int64_t numPoints = pointAttr.getNumberOfTuples();

        const GroupAttribute normalAttr = geometryAttr.getChildByName("arbitrary.normal");
        const FloatAttribute normalValAttr = normalAttr.getChildByName("value");
        assert(numPoints == normalValAttr.getNumberOfTuples());

        const GroupAttribute dPduAttr = geometryAttr.getChildByName("arbitrary.dPdu");
        const FloatAttribute dPduValAttr = dPduAttr.getChildByName("value");
        assert(numPoints == dPduValAttr.getNumberOfTuples());

        const int64_t numSamples = pointAttr.getNumberOfTimeSamples();
        std::vector<float> sampleTimes;
        sampleTimes.reserve(numSamples);

        // use location hash as seed for now
        const size_t locationHash =
                     std::hash<std::string>()(interface.getInputLocationPath());
        std::mt19937_64 randomEngine(locationHash);

        std::uniform_real_distribution<float> dist(0.f, 1.f);

        // vector of size(time samples) of vectors of size(points)
        std::vector<std::vector<Imath::M44d>> matrices;
        matrices.resize(numSamples);

        for (auto t = 0; t < numSamples; ++t) {

            // reseed the random engine so that the random #
            // generated for each point is the same across the time samples
            randomEngine.seed(locationHash);

            matrices[t].reserve(numPoints);

            const float sampleTime = pointAttr.getSampleTime(t);
            sampleTimes.push_back(sampleTime);

            // assumes same or at least similar sample times,
            // based off the points attr
            const FloatAttribute::array_type points  = pointAttr    .getNearestSample(sampleTime);
            const FloatAttribute::array_type normals = normalValAttr.getNearestSample(sampleTime);
            const FloatAttribute::array_type dPdus   = dPduValAttr  .getNearestSample(sampleTime);

            // loop through each time sample
            for (size_t i = 0; i < numPoints; ++i) {
                const size_t pIdx = 3 * i;
                const Imath::V3d normal(normals[pIdx], normals[pIdx + 1], normals[pIdx + 2]);

                const Imath::V3d dPdu(dPdus[pIdx], dPdus[pIdx + 1], dPdus[pIdx + 2]);

                static const Imath::V3d sXAxis(1,0,0);
                static const Imath::V3d sYAxis(0,1,0);

                // rotates from the y-axis to the normal
                Imath::Quatd quatRotateToNormal;
                quatRotateToNormal.setRotation(sYAxis, normal);

                // project the x-axis onto the normal plane
                const Imath::V3d rotatedXAxis = quatRotateToNormal.rotateVector(sXAxis);
                // find angle between rotated x-axis and dpdu
                // dpdu is already on the normal plane
                const float dPduAngle = std::acos(rotatedXAxis.dot(dPdu.normalized()));

                // add random rotation about the y-axis
                const float angle = dPduAngle + dist(randomEngine) * TWO_PI;
                Imath::Quatd quatRotateAroundYAxis;
                quatRotateAroundYAxis.setAxisAngle(sYAxis, angle);

                const Imath::M33d rotationMatrix = (quatRotateToNormal * quatRotateAroundYAxis).toMatrix33();

                const Imath::V3d translation(points[pIdx], points[pIdx + 1], points[pIdx + 2]);
                matrices[t].emplace_back(rotationMatrix, translation);
            }
        }

        interface.setAttr("type", FnAttribute::StringAttribute("instance array"));

        GroupBuilder geometryBuilder;
        geometryBuilder.deepUpdate(geometryAttr);

        geometryBuilder.set("instanceSource", sourceAttr);

        if (indexAttr.isValid()) {
            geometryBuilder.set("instanceIndex", indexAttr);
        } else {
            // assume 0's
            std::vector<int32_t> indicies(numPoints, 0);
            geometryBuilder.set("instanceIndex", IntAttribute(indicies.data(), indicies.size(), 1));
        }

        std::vector<const double*> matricesData;
        matricesData.reserve(numSamples);
        for (auto t = 0; t < numSamples; ++t) {
            matricesData[t] = matrices[t].front().getValue();
        }
        geometryBuilder.set("instanceMatrix",
                DoubleAttribute(sampleTimes.data(), numSamples,
                                matricesData.data(), matrices.front().size() * 16, 16));

        interface.setAttr("geometry", geometryBuilder.build());
    }
};

struct InstanceArrayToPointCloudOp : public FnKat::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(
                   Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        using namespace FnAttribute;

        const std::string inputType = FnKat::GetInputLocationType(interface);
        if (inputType != "instance array") {
            return;
        }

        const GroupAttribute geometryAttr = interface.getAttr("geometry");

        const DoubleAttribute instanceMatrixAttr = geometryAttr.getChildByName("instanceMatrix");
        const auto numSamples   = instanceMatrixAttr.getNumberOfTimeSamples();
        const auto numInstances = instanceMatrixAttr.getNumberOfTuples();
        const auto tupleSize    = instanceMatrixAttr.getTupleSize();

        std::vector<float> sampleTimes;
        sampleTimes.reserve(numSamples);

        // output
        std::vector<std::vector<float>> outPoints;
        outPoints.resize(numSamples);
        std::vector<const float*> outPointsRaw;
        outPointsRaw.reserve(numSamples);

        for (auto t = 0; t < numSamples; ++t) {
            const auto time = instanceMatrixAttr.getSampleTime(t);
            sampleTimes.push_back(time);

            const auto instanceMatrixVec = instanceMatrixAttr.getNearestSample(time);

            outPoints[t].reserve(numInstances*3);

            for (std::size_t idx = 0; idx < numInstances; ++idx) {
                const Imath::M44f mat =
                        arrayToImathMatrix44(&instanceMatrixVec[idx*tupleSize]);

                // Extract position
                const Imath::V3f pos = mat.translation();
                outPoints[t].push_back(pos.x);
                outPoints[t].push_back(pos.y);
                outPoints[t].push_back(pos.z);
            }

            outPointsRaw.push_back(outPoints[t].data());
        }

        interface.setAttr("type", FnAttribute::StringAttribute("pointcloud"));

        GroupBuilder geometryBuilder;
        geometryBuilder.deepUpdate(geometryAttr);

        geometryBuilder.set("point.P",
                FloatAttribute(sampleTimes.data(), sampleTimes.size(),
                               outPointsRaw.data(), numInstances*3, 3));

        interface.setAttr("geometry", geometryBuilder.build());
    }
};

DEFINE_GEOLIBOP_PLUGIN(VisualizeVectorsOp);
DEFINE_GEOLIBOP_PLUGIN(GenerateRandomColorsOp);
DEFINE_GEOLIBOP_PLUGIN(PointCloudToInstanceArrayOp);
DEFINE_GEOLIBOP_PLUGIN(InstanceArrayToPointCloudOp);

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(VisualizeVectorsOp, "VisualizeVectors", 0, 1);
    REGISTER_PLUGIN(GenerateRandomColorsOp, "GenerateRandomColors", 0, 1);
    REGISTER_PLUGIN(PointCloudToInstanceArrayOp, "PointCloudToInstanceArray", 0, 1);
    REGISTER_PLUGIN(InstanceArrayToPointCloudOp, "InstanceArrayToPointCloud", 0, 1);
}


