// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <FnAttribute/FnGroupBuilder.h>

#include <algorithm>
#include <cmath>
#include <random>

#include <OpenEXR/ImathMatrix.h>

namespace {

constexpr double TWO_PI = 2.0 * M_PI;
constexpr size_t MAT_SIZE = 16;

FnAttribute::GroupAttribute
createDebugPlaneGeometry(float rad)
{
    FnAttribute::GroupBuilder gb;
    std::array<float, 12> pArr { -rad, 0.f, rad,
                                  rad, 0.f, rad,
                                 -rad, 0.f, -rad,
                                  rad, 0.f, -rad };

    gb.set("point.P", FnAttribute::FloatAttribute(pArr.data(), pArr.size(), 3));

    std::array<int, 4> vertArr { 2, 3, 1, 0 };
    gb.set("poly.vertexList", FnAttribute::IntAttribute(vertArr.data(), vertArr.size(), 1));

    std::array<int, 2> startArr { 0, 4 };
    gb.set("poly.startIndex", FnAttribute::IntAttribute(startArr.data(), startArr.size(), 1));

    gb.set("arbitrary.st.scope", FnAttribute::StringAttribute("vertex"));
    gb.set("arbitrary.st.inputType", FnAttribute::StringAttribute("point2"));
    gb.set("arbitrary.st.outputType", FnAttribute::StringAttribute("point2"));

    std::array<int, 4> idxArr { 3, 2, 1, 0 };
    gb.set("arbitrary.st.index", FnAttribute::IntAttribute(idxArr.data(), idxArr.size(), 1));

    std::array<float, 8> indexedValueArr { 0.f, 0.f,
                                           1.f, 0.f,
                                           1.f, 1.f,
                                           0.f, 1.f };
    gb.set("arbitrary.st.indexedValue",
           FnAttribute::FloatAttribute(indexedValueArr.data(), indexedValueArr.size(), 2));

    return gb.build();
}

struct InstanceDiskOp : public Foundry::Katana::GeolibOp
{
    static void setup(Foundry::Katana::GeolibSetupInterface& interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface& interface)
    {
        using namespace FnAttribute;

        const StringAttribute sourceAttr(interface.getOpArg("source"));
        if (!sourceAttr.isValid()) {
            std::cerr << "Source not provided\n";
            return;
        }

        const FloatAttribute radiusAttr(interface.getOpArg("radius"));
        const float radius = radiusAttr.getValue(1.f, false);

        const IntAttribute debugModeAttr(interface.getOpArg("debugMode"));
        const bool debugMode = debugModeAttr.getValue(false, false);

        if (debugMode) {
            interface.setAttr("type", FnAttribute::StringAttribute("polymesh"));
            interface.setAttr("geometry", createDebugPlaneGeometry(radius));
            return;
        }

        const IntAttribute numInstancesAttr(interface.getOpArg("numInstances"));
        const IntAttribute::array_type instancesPerSourceArr = numInstancesAttr.getNearestSample(0.f);

        if (instancesPerSourceArr.empty()) {
            return;
        }

        if (sourceAttr.getNumberOfValues() != instancesPerSourceArr.size()) {
            std::cerr << "Mismatch in 'source' size and 'numInstances' size\n";
            return;
        }

        std::vector<int> indices;
        {
            int idx = 0;
            for (const size_t numInstances : instancesPerSourceArr) {
                std::fill_n(std::back_inserter(indices), numInstances, idx++);
            }
        }

        std::vector<Imath::M44d> matrices(indices.size());

        const uint_fast64_t locationSeed = std::hash<std::string>()(interface.getInputLocationPath());
        std::mt19937_64 randomEngine(locationSeed);
        std::uniform_real_distribution<> dist(0.0, std::nextafter(1.0, 2.0));
        for (auto& matrix : matrices) {
            const double offsetAngle = dist(randomEngine) * TWO_PI;
            const double rotateAngle = dist(randomEngine) * TWO_PI;
            const double rad = std::sqrt(dist(randomEngine)) * radius;

            static const Imath::V3d sYAxis(0.0, 1.0, 0.0);

            // rotate around the y-axis, then add x and z offset
            matrix.setAxisAngle(sYAxis, rotateAngle);
            matrix[3][0] = rad * std::cos(offsetAngle);
            matrix[3][2] = rad * std::sin(offsetAngle);
        }

        interface.setAttr("type", FnAttribute::StringAttribute("instance array"));

        GroupBuilder geometryBuilder;
        geometryBuilder.set("instanceSource", sourceAttr);

        geometryBuilder.set("instanceIndex", FnAttribute::IntAttribute(indices.data(), indices.size(), 1));

        geometryBuilder.set("instanceMatrix", FnAttribute::DoubleAttribute(matrices.front().getValue(),
                                                                           matrices.size() * MAT_SIZE,
                                                                           MAT_SIZE));

        interface.setAttr("geometry", geometryBuilder.build());
    }
};

DEFINE_GEOLIBOP_PLUGIN(InstanceDiskOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(InstanceDiskOp, "InstanceDisk", 0, 1);
}

