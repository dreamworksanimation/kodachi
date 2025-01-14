// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <OpenEXR/ImathVec.h>

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>
#include <FnGeolib/op/FnGeolibOp.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>
#include <FnLogging/FnLogging.h>
#include <FnPluginSystem/FnPlugin.h>

#include <cassert>

using namespace FnAttribute;
using namespace FnGeolibServices;

namespace
{

void
generateNormals(std::vector<float>& result,
                bool smooth,
                std::vector<Imath::V3f>& pointNormalSums, // sum of normals at a point
                std::vector<unsigned int>& pointRefs,       // count of times this point was referenced
                                                            // this count is used to average the normals
                                                            // !!! make sure the above vectors are
                                                            // initialized to zeros
                const IntAttribute::array_type& vertexList, // vertex list
                const std::vector<Imath::V3f>& points,
                const std::vector<unsigned int>& indices /*index into the vertexList*/)
{
    //result.reserve(result.size() + (vertices.size() * 3));

    // use cross product of any two vectors resulting
    // from the vertices to calculate the vertex normals
    unsigned int prev = 0;
    unsigned int next = 0;

    unsigned int ptCount = points.size();

    for (unsigned int i = 0; i < ptCount; ++i) {
        prev = (i == 0) ? ptCount - 1 : i - 1;
        next = (i == (ptCount - 1)) ? 0 : i + 1;

        // vector from point to the previous point
        const Imath::V3f v1 = points[prev] - points[i];
        const Imath::V3f v2 = points[next] - points[i];

        Imath::V3f normal = v1.cross(v2);
        normal.normalize();

        if (smooth) {
            unsigned int idx = vertexList[indices[i]];
            pointNormalSums[idx] += normal;
            pointRefs[idx]++;
        } else {
            result[indices[i]*3]     = normal.x;
            result[indices[i]*3 + 1] = normal.y;
            result[indices[i]*3 + 2] = normal.z;
        }
    }
}

class GenerateNormalsOp : public GeolibOp
{
public:
    static void setup(GeolibSetupInterface &interface)
    {
        interface.setThreading(GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(GeolibCookInterface &interface)
    {
        // User facing attributes
        StringAttribute celAttr        = interface.getOpArg("CEL");
        if (celAttr.isValid()) {
            FnGeolibCookInterfaceUtils::MatchesCELInfo info;
            FnGeolibCookInterfaceUtils::matchesCEL(info, interface,
                    celAttr);

            if (!info.canMatchChildren) {
                interface.stopChildTraversal();
            }

            if (!info.matches) {
                return;
            }
        }

        const FnAttribute::StringAttribute type =
                interface.getAttr("type");
        std::string typeStr = type.getValue("", false);
        if (typeStr == "polymesh" || typeStr == "subdmesh") {
            interface.stopChildTraversal();
        }

        // gather necessary information
        const GroupAttribute geometryAttr = GetGlobalAttr(interface, "geometry",
                interface.getInputLocationPath());

        // points
        FloatAttribute points = geometryAttr.getChildByName("point.P");
        if (!points.isValid()) {
            //ReportWarning(interface, "No valid points found.");
            return;
        }

        // all time samples for points attr
        int64_t numTimeSamples = points.getNumberOfTimeSamples();
        assert(numTimeSamples > 0);
        std::vector<FloatAttribute::array_type> pointsSamples;
        std::vector<float> pointsSampleTimes;
        pointsSamples.reserve(numTimeSamples);
        pointsSampleTimes.reserve(numTimeSamples);
        for (auto i = 0; i < numTimeSamples; ++i) {
            pointsSampleTimes.push_back(points.getSampleTime(i));
            pointsSamples.push_back(points.getNearestSample(pointsSampleTimes.back()));
        }

        // startIndex (of each face)
        IntAttribute startIndex = geometryAttr.getChildByName("poly.startIndex");
        if (!startIndex.isValid()) {
            //ReportWarning(interface, "No valid start index array found.");
            return;
        }
        IntAttribute::array_type startIndexList = startIndex.getNearestSample(0.0f);

        // vertexList (faces of the whole mesh)
        IntAttribute vertex = geometryAttr.getChildByName("poly.vertexList");
        if (!vertex.isValid()) {
            //ReportWarning(interface, "No valid vertex list array found.");
            return;
        }
        IntAttribute::array_type vertexList = vertex.getNearestSample(0.0f);

        // op arg: do we smooth normals?
        const IntAttribute generatePointNormalsAttr = interface.getOpArg("generate_point_normals");
        bool generatePointN = generatePointNormalsAttr.getValue(0, false);

        // op arg: do we smooth normals?
        const IntAttribute smoothAttr = interface.getOpArg("smooth_normals");
        bool smooth = smoothAttr.getValue(0, false) || generatePointN;  // we'll need to smooth for point N

        // time sampled output vectors -------------------------
        // output: vertex.N
        std::vector<std::vector<float>> output_normals;
        output_normals.resize(numTimeSamples);
        // sum of normals at a point
        std::vector<std::vector<Imath::V3f>> pointNormalSums;
        pointNormalSums.resize(numTimeSamples);
        // number of times a point is referenced,+
        // used to average the normals at the point
        std::vector<std::vector<unsigned int>> pointRefs;
        pointRefs.resize(numTimeSamples);

        for (auto i = 0; i < numTimeSamples; ++i) {
            output_normals[i] .resize(vertexList.size() * 3);
            pointNormalSums[i].resize(points.getNumberOfTuples(), Imath::V3f(0,0,0));
            pointRefs[i]      .resize(points.getNumberOfTuples(), 0);
        }

        // for each face in the faceset
        for (int startIdx = 0; startIdx < startIndexList.size() - 1; startIdx++) {

            // look up the starting index
            const unsigned int start = startIndexList[startIdx];

            // this tells us whether it's a quad or tri
            // it is expected that startIndexList has a size of number of faces + 1
            // so this should be a legal access
            const unsigned int faceSize = startIndexList[startIdx + 1] - start;

            std::vector<unsigned int> index;
            index.reserve(faceSize);
            for (unsigned int i = 0; i < faceSize; ++i) {
                index.push_back(start + i);
            }

            unsigned int idx = 0;
            for (auto sample = 0; sample < numTimeSamples; ++sample) {

                std::vector<Imath::V3f> points;
                points.reserve(faceSize);

                for (unsigned int i = 0; i < faceSize; ++i) {
                    idx = vertexList[start + i] * 3;
                    points.push_back(Imath::V3f(pointsSamples[sample][idx],
                                                pointsSamples[sample][idx + 1],
                                                pointsSamples[sample][idx + 2]));
                }

                generateNormals(output_normals[sample], smooth,
                                pointNormalSums[sample],
                                pointRefs[sample],
                                vertexList, points, index);
            }
        }

        if (smooth) {
            std::vector<std::vector<float>> output_pointN;
            output_pointN.resize(numTimeSamples);

            // average the normals
            for (auto sample = 0; sample < numTimeSamples; sample++) {
                output_pointN[sample].resize(points.getNumberOfValues());

                for (auto idx = 0; idx < points.getNumberOfTuples(); ++idx) {

                    pointNormalSums[sample][idx] /= pointRefs[sample][idx];
                    pointNormalSums[sample][idx].normalize();
                }

                for (auto i = 0; i < vertexList.size(); ++i) {
                    const unsigned int idx = vertexList[i];
                    output_normals[sample][i*3]     = pointNormalSums[sample][idx].x;
                    output_normals[sample][i*3 + 1] = pointNormalSums[sample][idx].y;
                    output_normals[sample][i*3 + 2] = pointNormalSums[sample][idx].z;
                }
            }
        }

        // set vertex.N
        if (numTimeSamples > 1) {
            std::vector<const float*> output_normals_raw;
            output_normals_raw.reserve(numTimeSamples);
            if (generatePointN) {
                for (auto i = 0; i < numTimeSamples; ++i) {
                    std::vector<float> point_normals_raw;
                    point_normals_raw.reserve(points.getNumberOfValues());
                    for (auto j = 0; j < pointNormalSums[i].size(); ++j) {
                        point_normals_raw.push_back(pointNormalSums[i][j].x);
                        point_normals_raw.push_back(pointNormalSums[i][j].y);
                        point_normals_raw.push_back(pointNormalSums[i][j].z);
                    }

                    output_normals_raw.push_back(point_normals_raw.data());
                }

                interface.setAttr("geometry.point.N",
                        FloatAttribute(pointsSampleTimes.data(), numTimeSamples,
                                       output_normals_raw.data(), points.getNumberOfValues(), 3));
            } else {

                for (auto i = 0; i < numTimeSamples; ++i) {
                    output_normals_raw.push_back(output_normals[i].data());
                }

                interface.setAttr("geometry.vertex.N",
                        FloatAttribute(pointsSampleTimes.data(), numTimeSamples,
                                       output_normals_raw.data(), vertex.getNumberOfValues(), 3));
            }
        } else {
            if (generatePointN) {
                std::vector<float> point_normals_raw;
                point_normals_raw.reserve(points.getNumberOfValues());
                for (auto i = 0; i < pointNormalSums.front().size(); ++i) {
                    point_normals_raw.push_back(pointNormalSums.front()[i].x);
                    point_normals_raw.push_back(pointNormalSums.front()[i].y);
                    point_normals_raw.push_back(pointNormalSums.front()[i].z);
                }

                interface.setAttr("geometry.point.N", FloatAttribute(point_normals_raw.data(),
                                                                     point_normals_raw.size(), 3));
            } else {
                interface.setAttr("geometry.vertex.N", FloatAttribute(output_normals.front().data(),
                                                                      output_normals.front().size(), 3));
            }
        }
    }

    static GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        const std::string kOpHelp("");
        const std::string kOpSummary("");

        FnOpDescriptionBuilder builder;

        builder.setHelp(kOpHelp);
        builder.setSummary(kOpSummary);
        builder.setNumInputs(0);

        return builder.build();
    }
};

DEFINE_GEOLIBOP_PLUGIN(GenerateNormalsOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(GenerateNormalsOp, "GenerateNormals", 0, 1);
}

