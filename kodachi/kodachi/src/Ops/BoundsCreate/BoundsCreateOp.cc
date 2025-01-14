// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>

#include <FnGeolib/op/FnGeolibOp.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>

#include <FnLogging/FnLogging.h>

#include <FnPluginSystem/FnPlugin.h>

// std
#include <limits>

using namespace FnAttribute;
using namespace FnGeolibServices;

namespace
{

class BoundsCreateOp : public GeolibOp
{
public:
    static void setup(GeolibSetupInterface &interface)
    {
        interface.setThreading(GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(GeolibCookInterface &interface)
    {
        // User facing attributes
        StringAttribute celAttr = interface.getOpArg("CEL");
        if (celAttr.isValid()) {
            FnGeolibCookInterfaceUtils::MatchesCELInfo info;
            FnGeolibCookInterfaceUtils::matchesCEL(info, interface, celAttr);

            if (!info.canMatchChildren) {
                interface.stopChildTraversal();
            }

            if (!info.matches) {
                return;
            }
        }

        // gather necessary information
        const GroupAttribute geometryAttr = GetGlobalAttr(interface, "geometry",
                interface.getInputLocationPath());

        // if it's a faceset, we're expecting the following required attributes
        bool isFaceset = GetInputLocationType(interface) == "faceset";

        // ***** required attrs *****************************************************
        // faces (of the faceset)
        IntAttribute faces = geometryAttr.getChildByName("faces");
        if (!faces.isValid()) {
            if (isFaceset)
                ReportWarning(interface, "Cannot calculate bounds, no faces found.");
            return;
        }
        IntAttribute::array_type facesList = faces.getNearestSample(0.0f);

        // points
        FloatAttribute points = geometryAttr.getChildByName("point.P");
        if (!points.isValid()) {
            if (isFaceset)
                ReportWarning(interface, "Cannot calculate bounds, no points found.");
            return;
        }
        FloatAttribute::array_type pointsList = points.getNearestSample(0.0f);

        // startIndex (of each face)
        IntAttribute startIndex = geometryAttr.getChildByName("poly.startIndex");
        if (!startIndex.isValid()) {
            if (isFaceset)
                ReportWarning(interface, "Cannot calculate bounds, no start indices found.");
            return;
        }
        IntAttribute::array_type startIndexList = startIndex.getNearestSample(0.0f);

        // vertexList (faces of the whole mesh)
        IntAttribute vertex = geometryAttr.getChildByName("poly.vertexList");
        if (!vertex.isValid()) {
            if (isFaceset)
                ReportWarning(interface, "Cannot calculate bounds, no vertices found.");
            return;
        }
        IntAttribute::array_type vertexList = vertex.getNearestSample(0.0f);

        double bounds[6] = {
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::min(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::min(),
                std::numeric_limits<double>::max(),
                std::numeric_limits<double>::min()
        };

        // gather the points in the faceset and calculate the extents
        for (const auto& faceI : facesList) {

            // look up the starting index
            const unsigned int start = startIndexList[faceI];

            // this tells us the number of points in this face
            // it is expected that startIndexList has a size of number of faces + 1
            // so this should be a legal access
            const unsigned int faceSize = startIndexList[faceI + 1] - start;

            unsigned int idx = 0;
            for (unsigned int i = 0; i < faceSize; ++i) {
                idx = vertexList[start + i] * 3;

                // x min
                bounds[0] = std::min(bounds[0], static_cast<double>(pointsList[idx]));
                // x max
                bounds[1] = std::max(bounds[1], static_cast<double>(pointsList[idx]));
                // y min
                bounds[2] = std::min(bounds[2], static_cast<double>(pointsList[idx+1]));
                // y max
                bounds[3] = std::max(bounds[3], static_cast<double>(pointsList[idx+1]));
                // z min
                bounds[4] = std::min(bounds[4], static_cast<double>(pointsList[idx+2]));
                // z max
                bounds[5] = std::max(bounds[5], static_cast<double>(pointsList[idx+2]));
            }
        }

        FnAttribute::DoubleAttribute boundAttr(&bounds[0], 6, 1);
        interface.setAttr("bound", boundAttr);
    }

    static GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        const std::string opHelp    = "Generates bounds for any location"
                                      "with the 'geometry.faces' attribute.";
        const std::string opSummary = "Generates bounds for facesets";

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {

    }
};

DEFINE_GEOLIBOP_PLUGIN(BoundsCreateOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(BoundsCreateOp, "BoundsCreate", 0, 1);
}

