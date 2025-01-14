// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnGeolib/op/FnGeolibOp.h>

#include <FnAttribute/FnAttribute.h>
#include <FnPluginSystem/FnPlugin.h>
#include <FnPluginManager/FnPluginManager.h>
#include <FnGeolibServices/FnBuiltInOpArgsUtil.h>
#include <FnAsset/FnDefaultAssetPlugin.h>

#include <math.h>

namespace {
using namespace FnAttribute;

class MoonrayLightGeometryOp : public Foundry::Katana::GeolibOp
{
public:
    static FnPlugStatus setHost(FnPluginHost* host)
    {
        Foundry::Katana::DefaultAssetPlugin::setHost(host);
        return Foundry::Katana::GeolibOp::setHost(host);
    }

    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface);

    static GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;
        FnOpDescriptionBuilder builder;
        builder.setSummary("Add geometry to show Moonray lights in viewer.");
        builder.setHelp("Adds representation of light-emitting surface and any "
                        "texture map applied to light. Only environment lights now.");
        builder.setNumInputs(0);
        // actually it adds a child location, this is the closest I can find to describe that:
        builder.describeOutputAttr(
            OutputAttrDescription(AttrTypeDescription::kTypeGroupAttribute, "emit"));

        return builder.build();
    }
};

// Used to turn textured map geometry off when muted
bool
muted(Foundry::Katana::GeolibCookInterface &interface)
{
#if 0
    // this does not work as MuteResolver has not been run yet:
    auto muteAttr = StringAttribute(interface.getAttr("info.light.muteState"));
    return muteAttr.isValid() && muteAttr != "muteEmpty";
#else
    return IntAttribute(interface.getAttr("info.light.mute")).getValue(0,false) &&
        not IntAttribute(interface.getAttr("info.light.solo")).getValue(0,false);
#endif
}

// may want to use this to turn textured map geometry on/off
bool
visible_in_camera(const GroupAttribute& params)
{
    return StringAttribute(params.getChildByName("visible_in_camera")).getValue("",false)=="force on";
}

// For Env Lights make a child dome called "emit" that will display the texture using
// normal geometry renders. This is only done if the light is enabled
void
makeEnvLightGeometry(Foundry::Katana::GeolibCookInterface &interface,
                     const GroupAttribute& params)
{
    if (muted(interface)) return;
    Foundry::Katana::StaticSceneCreateOpArgsBuilder builder(false);
    static const std::string location("emit");

    builder.setAttrAtLocation(location, "type", StringAttribute("polymesh"));

    // should it set "bound"?

    int div = 1;
    if (IntAttribute(params.getChildByName("sample_upper_hemisphere_only")).getValue(0,false))
        div = 2;

    const int rows = 16;
    const int cols = 32;
    const int npoints = (cols + 1) * (rows/div + 1);
    std::vector<float> P(npoints * 3);
    std::vector<float> st(npoints * 2);
    const int npoly = cols * rows / div;
    std::vector<int> sI(npoly + 1);
    std::vector<int> I(4 * npoly);

    int point = 0;
    int poly = 0;
    for (int row = 0; row <= rows / div; ++row) {
        for (int col = 0; col <= cols; ++col) {
            float r = -sinf(row*M_PI/rows);
            float y = cosf(row*M_PI/rows);
            float x = r * sinf(col*2*M_PI/cols);
            float z = r * cosf(col*2*M_PI/cols);
            P[3*point+0] = x;
            P[3*point+1] = y;
            P[3*point+2] = z;
            st[2*point+0] = 1.0f - float(col) / cols;
            st[2*point+1] = 1.0f - float(row) / rows;
            if (row && col) { // add the quad to lower-left of point
                sI[poly] = 4*poly;
                I[4*poly+0] = point-cols-1;
                I[4*poly+1] = point-cols-2;
                I[4*poly+2] = point-1;
                I[4*poly+3] = point;
                ++poly;
            }
            ++point;
        }
    }
    sI[poly] = 4*poly;

    GroupBuilder gb;
    gb.set("point.P", FloatAttribute(P.data(), P.size(), 3));
    // normals are not needed for emit shader, but set them anyway to get a smooth sphere
    for (float& i: P) i = -i; gb.set("point.N", FloatAttribute(P.data(), P.size(), 3));
    gb.set("poly.startIndex", IntAttribute(sI.data(), sI.size(), 1));
    gb.set("poly.vertexList", IntAttribute(I.data(), I.size(), 1));
    gb.set("arbitrary.st.scope", StringAttribute("vertex"));
    gb.set("arbitrary.st.inputType", StringAttribute("point2"));
    gb.set("arbitrary.st.index", IntAttribute(I.data(), I.size(), 1));
    gb.set("arbitrary.st.indexedValue", FloatAttribute(st.data(), st.size(), 2));
    builder.setAttrAtLocation(location, "geometry", gb.build());

    StringAttribute textureAttr = params.getChildByName("texture");
    FnPlatform::StringView texture = textureAttr.getValueCStr();

    if (Foundry::Katana::DefaultAssetPlugin::isAssetId(texture.data())) {
        const std::string textureAsset =
                Foundry::Katana::DefaultAssetPlugin::resolveAsset(texture.data());

        textureAttr = StringAttribute(textureAsset);
    }

    GroupBuilder mb;
    mb.set("hydraSurfaceShader", StringAttribute("emit"));
    mb.set("hydraSurfaceParams.diffuseTexture", textureAttr);
    builder.setAttrAtLocation(location, "material", mb.build());

    FloatAttribute color(params.getChildByName("color"));
    if (not color.isValid()) { // Katana default is gray, moonray default is white
        const float white[3] = {1,1,1};
        color = FloatAttribute(white, 3, 3);
    }
    builder.setAttrAtLocation(location, "viewer.default.drawOptions.color", color);

    interface.execOp("StaticSceneCreate", builder.build());
}

// For mesh lights, copy the necessary parts of the source geometry to a new "mesh" attribute
void
makeMeshLightGeometry(Foundry::Katana::GeolibCookInterface &interface,
                      const GroupAttribute& params)
{
    StringAttribute a(params.getChildByName("geometry"));
    std::string geometry(a.getValue("", false));
    if (geometry.empty()) return;
    a = params.getChildByName("parts");
    std::vector<std::string> parts;
    for (const std::string& s : a.getNearestSample(0.0))
        if (s.size()) parts.emplace_back(s);

    // prefetch
    for (const std::string& part : parts)
        interface.prefetch(geometry + '/' + part);
    interface.prefetch(geometry);

    // do the facesets first so they interrupt asap
    bool noparts = false; // set to true if the part list does not match anything
    if (not parts.empty()) {
        std::vector<int> faces;
        for (const std::string& part : parts) {
            IntAttribute a(interface.getAttr("geometry.faces", geometry + '/' + part));
            auto&& array = a.getNearestSample(0.0);
            faces.reserve(faces.size() + array.size());
            for (int i : array) faces.push_back(i);
        }
        noparts = faces.empty();
        if (not noparts)
            interface.setAttr("mesh.faces", IntAttribute(&faces[0], faces.size(), 1));
    }
    if (not noparts) {
        interface.copyAttr("mesh.poly", "geometry.poly", false, geometry);
        interface.copyAttr("mesh.point", "geometry.point", false, geometry);
    }
    interface.setAttr("mesh_xform", FnKat::GetGlobalXFormGroup(interface, geometry));
}

void
MoonrayLightGeometryOp::cook(Foundry::Katana::GeolibCookInterface &interface)
{
    if (FnKat::GetInputLocationType(interface) != "light") return;

    // interface.stopChildTraversal(); // can lights contain lights?

    const GroupAttribute material(interface.getAttr("material"));
    if (not material.isValid()) return;

    StringAttribute typeAttr;
    GroupAttribute params;
    if (StringAttribute(material.getChildByName("style")) == "network") {
        const StringAttribute nodeName(
            material.getChildByName("terminals.moonrayLight"));
        if (nodeName.isValid()) {
            typeAttr =
                material.getChildByName("nodes." + nodeName.getValue() + ".type");
            params =
                material.getChildByName("nodes." + nodeName.getValue() + ".parameters");
        }
    } else {
        typeAttr = material.getChildByName("moonrayLightShader");
        params = material.getChildByName("moonrayLightParams");
    }

    // Make geometry object
    if (typeAttr == "EnvLight") {
        makeEnvLightGeometry(interface, params);
    } else if (typeAttr == "MeshLight") {
        makeMeshLightGeometry(interface, params);
    }
}

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(MoonrayLightGeometryOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayLightGeometryOp, "MoonrayLightGeometry", 0, 1);
}

