// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>

#include <FnGeolib/op/FnGeolibOp.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnGeolibServices/FnGeolibCookInterfaceUtilsService.h>

#include <FnLogging/FnLogging.h>

#include <FnPluginSystem/FnPlugin.h>

#include <cmath>
#include <map>

using namespace FnAttribute;
using namespace FnGeolibServices;

namespace
{

enum DATA_FLAGS {
    NONE               = 0,
    POINT_P            = 1 << 0,
    POLY_STARTINDEX    = 1 << 1,
    POLY_VERTEXLIST    = 1 << 2,
    ARBITRARY_ST_INDEX = 1 << 3,
    ARBITRARY_ST_VALUE = 1 << 4,
    ARBITRARY_NORMAL   = 1 << 5
};

struct GeometryData {
    // points
    FloatAttribute::array_type points;
    // poly
    IntAttribute::array_type   startIndices;
    IntAttribute::array_type   vertices;
    // uv
    IntAttribute::array_type   uvIndices;
    FloatAttribute::array_type uv;
    // normals
    FloatAttribute::array_type normals;

    DATA_FLAGS flags = NONE;
};

bool
getFloatAttr(const std::string& attrName,
             const GroupAttribute& geometryAttr,
             FloatAttribute::array_type& out,
             int& flags,
             DATA_FLAGS dataFlag)
{
    FloatAttribute data = geometryAttr.getChildByName(attrName);
    if (data.isValid()) {
        out = data.getNearestSample(0.f);
        return true;
    } else {
        flags &= ~dataFlag;
        return false;
    }
}

bool
getIntAttr(const std::string& attrName,
           const GroupAttribute& geometryAttr,
           IntAttribute::array_type& out,
           int& flags,
           DATA_FLAGS dataFlag)
{
    IntAttribute data = geometryAttr.getChildByName(attrName);
    if (data.isValid()) {
        out = data.getNearestSample(0.f);
        return true;
    } else {
        flags &= ~dataFlag;
        return false;
    }
}

// flags: use DATA_FLAGS to decide which attributes to retrieve
// flags will also be set to indicate which data failed to be retrieved
// returns false on any failure, true otherwise
bool
gatherGeometryAttributes(const GroupAttribute& geometryAttr,
                         GeometryData& outData, int& flags)
{
    bool result = true;
    if (flags & POINT_P) {
        result &= getFloatAttr("point.P", geometryAttr, outData.points,
                     flags, POINT_P);
    }

    if (flags & POLY_STARTINDEX) {
        result &= getIntAttr("poly.startIndex", geometryAttr, outData.startIndices,
                   flags, POLY_STARTINDEX);
    }

    if (flags & POLY_VERTEXLIST) {
        result &= getIntAttr("poly.vertexList", geometryAttr, outData.vertices,
                   flags, POLY_VERTEXLIST);
    }

    if (flags & ARBITRARY_ST_INDEX) {
        result &= getIntAttr("arbitrary.st.index", geometryAttr, outData.uvIndices,
                   flags, ARBITRARY_ST_INDEX);
    }

    if (flags & ARBITRARY_ST_VALUE) {
        result &= getFloatAttr("arbitrary.st.indexedValue", geometryAttr, outData.uv,
                     flags, ARBITRARY_ST_VALUE);
    }

    if (flags & ARBITRARY_NORMAL) {
        result &= getFloatAttr("arbitrary.normal.value", geometryAttr, outData.normals,
                     flags, ARBITRARY_NORMAL);
    }
    return result;
}

// divide
void
facesetDivideUdim(const int division, GeolibCookInterface& interface,
                  const GroupAttribute& geometryAttr,
                  const IntAttribute::array_type& faces,
                  const std::string& name)
{
    // get necessary attributes
    int flags = POINT_P | POLY_STARTINDEX | POLY_VERTEXLIST |
                ARBITRARY_ST_INDEX | ARBITRARY_ST_VALUE;
    GeometryData data;

    bool result = gatherGeometryAttributes(geometryAttr, data, flags);

    if (!result) {
        ReportWarning(interface, "Failed to retrieve some data: " + flags);
        return;
    }

    // uv's sorted by udim buckets
    std::map<int, std::vector<int>> udimSorted;

    unsigned int idx = 0;
    // unsigned int tile = 0;
    unsigned int u = 0, v = 0;
    for (const auto faceI : faces) {
        // look up the starting index
        const unsigned int start    = data.startIndices[faceI];
        // const unsigned int faceSize = data.startIndices[faceI + 1] - start;

        idx = data.uvIndices[start] * 2;
        u = data.uv[idx];
        v = data.uv[idx + 1];

        // store the face under its udim tile bucket
        // ASSUMPTIONS:
        // - of course, that this is a udim
        // - the udim is base 10 (v*10)
        // - face does not go cross tiles (so we only looked at one vertex)
        udimSorted[1000 + (u+1) + (v*10)].push_back(faceI);
    }

    std::string childName = "";
    FnGeolibServices::StaticSceneCreateOpArgsBuilder sscb(false);
    for (const auto& bucket : udimSorted) {
        childName = name + "_" + std::to_string(bucket.first);
        sscb.setAttrAtLocation(childName, "type", StringAttribute("faceset"));
        sscb.setAttrAtLocation(childName, "geometry.faces",
                               IntAttribute(bucket.second.data(),
                                            bucket.second.size(), 1));
    }

    interface.execOp("StaticSceneCreate", sscb.build());
}

void
facesetDivideSimple(const int division, GeolibCookInterface& interface,
                    const IntAttribute::array_type& faces,
                    const std::string& name)
{
    // brute force division
    unsigned int sectionLength = std::ceil(faces.size() / division);

    std::string childName = "";

    FnGeolibServices::StaticSceneCreateOpArgsBuilder sscb(false);

    int pos = 0;
    int len = sectionLength;

    while (pos < faces.size()) {
        childName = name + "_" + std::to_string(pos);
        sscb.setAttrAtLocation(childName, "type", StringAttribute("faceset"));

        if (pos + sectionLength > faces.size()) {
            len = faces.size() - pos;
        } else if (faces.size() - (pos + sectionLength) <= (sectionLength / 2)) {
            // if the remainder of this section is less than half of a section length,
            // lets just accumulate it into this section
            len = sectionLength + (faces.size() - (pos + sectionLength));
        }

        sscb.setAttrAtLocation(childName, "geometry.faces",
                               IntAttribute(faces.data() + pos, len, 1));
        pos += len;
    }

    interface.execOp("StaticSceneCreate", sscb.build());
}

class FacesetDivideOp : public GeolibOp
{
public:
    static void setup(GeolibSetupInterface &interface)
    {
        interface.setThreading(GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(GeolibCookInterface &interface)
    {
        StringAttribute celAttr = interface.getOpArg("CEL");
        FnGeolibCookInterfaceUtils::MatchesCELInfo info;
        bool celMatchesMesh = false;   // whether the cel matches the parent subdmesh
        if (celAttr.isValid()) {
            FnGeolibCookInterfaceUtils::matchesCEL(info, interface, celAttr);

            celMatchesMesh = info.matches;

            if (!info.canMatchChildren) {
                // no child can match, no need to go on
                interface.stopChildTraversal();

                // if children can't match and we also don't match, return
                // don't return if children can match because we'll go through them
                // below
                if (!celMatchesMesh) {
                    return;
                }
            }
        }

        // currently works on subdmesh with faceset children only
        if (GetInputLocationType(interface) != "subdmesh") {
            return;
        }

        // get potential children (facesets)----------------------------------------------------
        StringAttribute children = interface.getPotentialChildren();
        if (!children.isValid()) {
            return;
        }
        StringAttribute::array_type childList = children.getNearestSample(0.0f);

        // op args------------------------------------------------------------------------------
        IntAttribute divisionAttr = interface.getOpArg("division");
        // minimum of division 1
        int division = std::max(1, divisionAttr.getValue(1, false));
        if (division == 1) {
            // no work needs to be done
            return;
        }


        // loop through children and split face sets--------------------------------------------
        for (int i = 0; i < childList.size(); ++i) {
            // check the CEL
            // if it matches the mesh (parent), don't skip it
            if (celAttr.isValid() && !celMatchesMesh) {
                FnGeolibCookInterfaceUtils::matchesCEL(info, interface, celAttr,
                                                       childList[i]);
                if (!info.matches) {
                    continue;
                }
            }

            // gather necessary information
            const GroupAttribute geometryAttr = GetGlobalAttr(interface, "geometry",
                                                              childList[i]);
            if (!geometryAttr.isValid()) {
                //ReportWarning(interface, "[FacesetDivide] 'geometry' attribute group not found.");
                continue;
            }

            // faces (of the faceset) *required*
            IntAttribute faces = geometryAttr.getChildByName("faces");
            if (!faces.isValid()) {
                //ReportWarning(interface, "[FacesetDivide] 'geometry.faces' attribute not found.");
                continue;
            }
            IntAttribute::array_type facesList = faces.getNearestSample(0.0f);

            std::string name = childList[i];
            auto pos = name.rfind("/") + 1;
            name = name.substr(pos, name.size() - pos);

            // do the division----------------------------------------------------------------------
            //facesetDivideSimple(division, interface, facesList, name);
            facesetDivideUdim(division, interface, geometryAttr, facesList, name);

            interface.deleteChild(name);
        } // end child loop
    }

    static GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        const std::string opHelp    = "";
        const std::string opSummary = "Splits a faceset into multiple facesets.";

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {

    }
};

DEFINE_GEOLIBOP_PLUGIN(FacesetDivideOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(FacesetDivideOp, "FacesetDivide", 0, 1);
}

