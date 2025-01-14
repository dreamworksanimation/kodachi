// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/ZeroCopyAttribute.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <kodachi_moonray/embree_util/EmbreeUtil.h>
#include <kodachi_moonray/kodachi_geometry/GenerateUtil.h>

// Imath
#include <OpenEXR/ImathVec.h>
#include <OpenEXR/ImathMatrix.h>

// std
#include <unordered_map>

namespace
{

KdLogSetup("EmbreeRTCScene");

// Simple example op using the embree_util::EmbreeScene to perform a ray occlusion
// test
class EmbreeRTCSceneCreateOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        using EmbreeScene = kodachi_moonray::embree_util::EmbreeScene;

        // CAMERA
        const kodachi::StringAttribute cameraAttr =
                interface.getOpArg("camera");
        const std::string cameraLocation = cameraAttr.getValue("", false);
        if (cameraLocation.empty() ||
                !interface.doesLocationExist(cameraLocation)) {
            KdLogWarn(" >>> EmbreeRTCScene Op: invalid camera.");
            return;
        }
        interface.prefetch(cameraLocation);

        // SAMPLE TIMES
        // using a set here will order the time and remove duplicates
        std::set<float> shutterTimes { 0.0f };
        if (kodachi::GetNumSamples(interface) > 1) {
            shutterTimes.insert({ kodachi::GetShutterOpen(interface),
                                  kodachi::GetShutterClose(interface)});
        }
        std::vector<float> sampleTimes(shutterTimes.begin(), shutterTimes.end());

        // MESHES
        const kodachi::StringAttribute meshesAttr =
                interface.getOpArg("meshes");
        if (!meshesAttr.isValid()) {
            return;
        }

        EmbreeScene embreeScene;

        // populate the rtc scene
        const auto samples = meshesAttr.getSamples();

        for (const kodachi::string_view mesh : samples.front()) {
            if (interface.doesLocationExist(mesh)) {
                interface.prefetch(mesh);
                embreeScene.addGeometry(interface.getAttr("geometry", mesh),
                        kodachi::GetGlobalXFormGroup(interface, mesh.data()),
                        sampleTimes);
            }
        }

        embreeScene.commit();

        // GET RAY
        const kodachi::GroupAttribute currentXform =
                kodachi::GetGlobalXFormGroup(interface);
        const kodachi::GroupAttribute camXform =
                kodachi::GetGlobalXFormGroup(interface, cameraLocation);

        const kodachi::DoubleAttribute currentXformAttr =
                kodachi::XFormUtil::CalcTransformMatrixAtTimes(currentXform,
                        sampleTimes.data(), sampleTimes.size()).first;
        const auto currentXformSamples = currentXformAttr.getSamples();

        const kodachi::DoubleAttribute camXformAttr =
                kodachi::XFormUtil::CalcTransformMatrixAtTimes(camXform,
                        sampleTimes.data(), sampleTimes.size()).first;
        const auto camXformSamples = camXformAttr.getSamples();

        std::unordered_map<float, EmbreeScene::Ray> rays;
        {
            Imath::V3f v;
            Imath::M44d m;

            const float timeRange = sampleTimes.back() - sampleTimes.front();

            kodachi::AttributeSetOpArgsBuilder asb;
            asb.setCEL(kodachi::StringAttribute("//*"));
            asb.setAttr("type", kodachi::StringAttribute("curves"));
            std::vector<float> testCurveP;
            std::vector<int32_t> testCurveNumV;
            std::vector<float> testCurveW;

            for (const float t : sampleTimes) {

                EmbreeScene::Ray& ray = rays[t];

                kodachi_moonray::setXformMatrix(m, camXformSamples.getNearestSample(t).data());
                v = m.translation();
                ray.org_x = v.x;
                ray.org_y = v.y;
                ray.org_z = v.z;
                Imath::V3f o(ray.org_x, ray.org_y, ray.org_z);
                testCurveP.insert(testCurveP.end(),
                        {o.x,o.y,o.z});

                kodachi_moonray::setXformMatrix(m, currentXformSamples.getNearestSample(t).data());
                v = m.translation() - v;

                ray.tnear = 0.0f;
                ray.tfar = v.length();

                v.normalize();
                ray.dir_x = v.x;
                ray.dir_y = v.y;
                ray.dir_z = v.z;

                Imath::V3f dir(ray.dir_x, ray.dir_y, ray.dir_z);
                dir.normalize();
                dir *= ray.tfar;
                dir += o;
                testCurveP.insert(testCurveP.end(),
                        {dir.x, dir.y, dir.z});
                testCurveNumV.push_back(2);
                testCurveW.push_back(0.1f);
                testCurveW.push_back(0.1f);


                // normalized time given in [0,1] range
                ray.time = timeRange > 0.0f ?
                        (t - sampleTimes.front()) / timeRange : 0.0f;

                ray.mask = -1;
                ray.id = 0;
                ray.flags = 0;

                ray.geomID = RTC_INVALID_GEOMETRY_ID;
                ray.primID = RTC_INVALID_GEOMETRY_ID;
                ray.instID = RTC_INVALID_GEOMETRY_ID;

                // ray.mask can be used to mask out certain geometry

                // query
                bool hit = embreeScene.isOccluded(ray);
                KdLogDebug("At time " << t << ": "
                        << std::boolalpha << hit << std::noboolalpha);
            }

            asb.setAttr("geometry.point.P",
                    kodachi::ZeroCopyFloatAttribute::create(testCurveP, 3));
            asb.setAttr("geometry.point.width",
                    kodachi::ZeroCopyFloatAttribute::create(testCurveW));
            asb.setAttr("geometry.numVertices",
                    kodachi::ZeroCopyIntAttribute::create(testCurveNumV));
            interface.createChild("ray",
                                  "AttributeSet",
                                  asb.build());
        }

    } // end op

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

// Debug op for using embree with curves
class EmbreeCurvesTestOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        using EmbreeScene = kodachi_moonray::embree_util::EmbreeScene;

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

        // width
        const kodachi::FloatAttribute widthArg = interface.getOpArg("width");
        const float width = widthArg.getValue(0.0001f, false);

        const kodachi::FloatAttribute widthFactorArg = interface.getOpArg("width_factor");
        const float widthFactor = widthFactorArg.getValue(3.0f, false);

        const kodachi::FloatAttribute tfarArg = interface.getOpArg("tfar_difference");
        const float tfarDiff = tfarArg.getValue(0.1f, false);

        const kodachi::IntAttribute intersectArg = interface.getOpArg("intersect");
        const bool intersectMode = intersectArg.getValue(true, false);

        // CAMERA
        const kodachi::StringAttribute cameraAttr =
                interface.getOpArg("camera");
        const std::string cameraLocation = cameraAttr.getValue("", false);
        if (cameraLocation.empty() ||
                !interface.doesLocationExist(cameraLocation)) {
            KdLogWarn(" >>> EmbreeRTCScene Op: invalid camera.");
            return;
        }
        interface.prefetch(cameraLocation);

        // SAMPLE TIMES
        // using a set here will order the time and remove duplicates
        std::set<float> shutterTimes { 0.0f };
        if (kodachi::GetNumSamples(interface) > 1) {
            shutterTimes.insert({ kodachi::GetShutterOpen(interface),
                                  kodachi::GetShutterClose(interface)});
        }
        std::vector<float> sampleTimes(shutterTimes.begin(), shutterTimes.end());

        // MESHES
        const kodachi::StringAttribute meshesAttr =
                interface.getOpArg("meshes");
        if (!meshesAttr.isValid()) {
            return;
        }

        EmbreeScene embreeScene;

        // populate the rtc scene
        const auto samples = meshesAttr.getSamples();

        for (const kodachi::string_view mesh : samples.front()) {
            if (interface.doesLocationExist(mesh)) {
                interface.prefetch(mesh);
                const int32_t id = embreeScene.addGeometry(interface.getAttr("geometry", mesh),
                        kodachi::GetGlobalXFormGroup(interface, mesh.data()),
                        sampleTimes);
            }
        }

        embreeScene.commit();

        // XFORMS
        const kodachi::GroupAttribute curvesXform =
                kodachi::GetGlobalXFormGroup(interface);
        const kodachi::GroupAttribute camXform =
                kodachi::GetGlobalXFormGroup(interface, cameraLocation);

        const kodachi::DoubleAttribute curvesXformAttr =
                kodachi::XFormUtil::CalcTransformMatrixAtTimes(curvesXform,
                        sampleTimes.data(), sampleTimes.size()).first;
        const auto curvesXformSamples = curvesXformAttr.getSamples();

        const kodachi::DoubleAttribute camXformAttr =
                kodachi::XFormUtil::CalcTransformMatrixAtTimes(camXform,
                        sampleTimes.data(), sampleTimes.size()).first;
        const auto camXformSamples = camXformAttr.getSamples();

        // CURVE GEOMETRY
        const kodachi::FloatAttribute pointAttr = interface.getAttr("geometry.point.P");
        const auto pSamples = pointAttr.getSamples();

        const kodachi::IntAttribute numVertsAttr = interface.getAttr("geometry.numVertices");
        const auto nvSamples = numVertsAttr.getSamples();
        const auto& numVertices = nvSamples.front();

        // OUTPUT RAYS
        std::vector<float> testCurvePHit;
        std::vector<int32_t> testCurveNumVHit;
        std::vector<float> testCurveWHit;

        std::vector<float> testCurvePMiss;
        std::vector<int32_t> testCurveNumVMiss;
        std::vector<float> testCurveWMiss;

        std::vector<float> testCurvePNg;
        std::vector<int32_t> testCurveNumVNg;
        std::vector<float> testCurveWNg;

        Imath::M44d m;
        Imath::V3f v0;
        Imath::V3f v1;
        Imath::V3f vDir;

        float t = 0.0f;
        const auto& points = pSamples.getNearestSample(t);

        size_t pIdx = 0;

        for (auto cv : numVertices) {
            EmbreeScene::Ray ray;

            for (size_t c = 0; c < cv; ++c) {

                // ray origin
                kodachi_moonray::setXformMatrix(m, camXformSamples.getNearestSample(t).data());
                v0 = m.translation();
                ray.org_x = v0.x;
                ray.org_y = v0.y;
                ray.org_z = v0.z;

                // ray end (use the first cv of the curve)
                v1 = Imath::V3f(points[pIdx], points[pIdx + 1], points[pIdx + 2]);
                kodachi_moonray::setXformMatrix(m, curvesXformSamples.getNearestSample(t).data());
                v1 = v1*m;
                vDir = v1 - v0;

                ray.tnear = 0.0f;
                ray.tfar = vDir.length() - tfarDiff;

                vDir.normalize();
                ray.dir_x = vDir.x;
                ray.dir_y = vDir.y;
                ray.dir_z = vDir.z;

                ray.time = 0.0f;

                // query
                bool hit = false;
                if (intersectMode) {
                    uint32_t id = embreeScene.intersect(ray);
                    hit = (id != RTC_INVALID_GEOMETRY_ID);
                } else {
                    hit = embreeScene.isOccluded(ray);
                }

                if (hit) {
                    if (intersectMode) {
                        // hit distance written into tfar
                        v1 = v0 + (vDir * ray.tfar);

                        // unnormalized geometry normal in object space
                        Imath::V3f Ng(ray.Ng_x, ray.Ng_y, ray.Ng_z);
                        Ng = v1 + Ng;
                        testCurvePNg.insert(testCurvePNg.end(), {v1.x, v1.y, v1.z});
                        testCurvePNg.insert(testCurvePNg.end(), {Ng.x, Ng.y, Ng.z});
                        testCurveNumVNg.push_back(2);
                        testCurveWNg.push_back(width*widthFactor);
                    }
                    testCurvePHit.insert(testCurvePHit.end(), {ray.org_x, ray.org_y, ray.org_z});
                    testCurvePHit.insert(testCurvePHit.end(), {v1.x, v1.y, v1.z});
                    testCurveNumVHit.push_back(2);
                    testCurveWHit.push_back(width*widthFactor);
                    testCurveWHit.push_back(width*widthFactor);
                } else {
                    testCurvePMiss.insert(testCurvePMiss.end(), {ray.org_x, ray.org_y, ray.org_z});
                    testCurvePMiss.insert(testCurvePMiss.end(), {v1.x, v1.y, v1.z});
                    testCurveNumVMiss.push_back(2);
                    testCurveWMiss.push_back(width);
                    testCurveWMiss.push_back(width);
                }

                pIdx += 3;
            }
        }

        kodachi::AttributeSetOpArgsBuilder asb;
        asb.setCEL(kodachi::StringAttribute("//*"));
        asb.setAttr("type", kodachi::StringAttribute("curves"));
        asb.setAttr("geometry.point.P",
                kodachi::ZeroCopyFloatAttribute::create(testCurvePHit, 3));
        asb.setAttr("geometry.point.width",
                kodachi::ZeroCopyFloatAttribute::create(testCurveWHit));
        asb.setAttr("geometry.numVertices",
                kodachi::ZeroCopyIntAttribute::create(testCurveNumVHit));
        asb.setAttr("xform.origin", kodachi::DoubleAttribute(double(0)));
        interface.createChild("hits",
                              "AttributeSet",
                              asb.build());

        asb.setCEL(kodachi::StringAttribute("//*"));
        asb.setAttr("type", kodachi::StringAttribute("curves"));
        asb.setAttr("geometry.point.P",
                kodachi::ZeroCopyFloatAttribute::create(testCurvePMiss, 3));
        asb.setAttr("geometry.point.width",
                kodachi::ZeroCopyFloatAttribute::create(testCurveWMiss));
        asb.setAttr("geometry.numVertices",
                kodachi::ZeroCopyIntAttribute::create(testCurveNumVMiss));
        asb.setAttr("xform.origin", kodachi::DoubleAttribute(double(0)));
        interface.createChild("misses",
                              "AttributeSet",
                              asb.build());

        asb.setCEL(kodachi::StringAttribute("//*"));
        asb.setAttr("type", kodachi::StringAttribute("curves"));
        asb.setAttr("geometry.point.P",
                kodachi::ZeroCopyFloatAttribute::create(testCurvePNg, 3));
        asb.setAttr("geometry.point.width",
                kodachi::ZeroCopyFloatAttribute::create(testCurveWNg));
        asb.setAttr("geometry.numVertices",
                kodachi::ZeroCopyIntAttribute::create(testCurveNumVNg));
        asb.setAttr("xform.origin", kodachi::DoubleAttribute(double(0)));
        interface.createChild("normals",
                              "AttributeSet",
                              asb.build());

    }
};

DEFINE_KODACHIOP_PLUGIN(EmbreeRTCSceneCreateOp)
DEFINE_KODACHIOP_PLUGIN(EmbreeCurvesTestOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(EmbreeRTCSceneCreateOp, "EmbreeRTCSceneCreateOp", 0, 1);
    REGISTER_PLUGIN(EmbreeCurvesTestOp, "EmbreeCurvesTestOp", 0, 1);
}


