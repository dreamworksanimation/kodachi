// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi_moonray/kodachi_geometry/GenerateUtil.h>
#include <kodachi_moonray/kodachi_geometry/KodachiGeometry.h>
#include <kodachi_moonray/kodachi_geometry/PrimitiveAttributeUtil.h>
#include <kodachi_moonray/kodachi_runtime_wrapper/KodachiRuntimeWrapper.h>

#include <rendering/geom/Api.h>
#include <rendering/geom/ProceduralLeaf.h>
#include <scene/rdl2/rdl2.h>
#include <scene_rdl2/common/platform/Platform.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <string>
#include <vector>

#include "attributes.cc"

using namespace arras;

namespace {

/*

[Attribute Rate Conversions]
USD           | KATANA                      | MOONRAY
----------------------------------------------------------------------------------
              | MESH                        |                | Count
----------------------------------------------------------------------------------
face varying  | vertex                      | face varying   | indices
varying       | point                       | varying        | vertices / points
vertex        | point (interpType = subdiv) | vertex         | vertices / points
uniform       | face                        | uniform        | faces
constant      | primitive                   | constant       | 1

*/
arras::shading::AttributeRate
rateFunc(const kodachi::StringAttribute& scopeAttr,
         const kodachi::StringAttribute& interpAttr)
{
    static const kodachi::StringAttribute kScopePrimitiveAttr("primitive");
    static const kodachi::StringAttribute kScopeFaceAttr("face");
    static const kodachi::StringAttribute kScopePointAttr("point");
    static const kodachi::StringAttribute kScopeVertexAttr("vertex");
    static const kodachi::StringAttribute kScopePartAttr("part");

    static const kodachi::StringAttribute kInterpSubdivAttr("subdiv");

    arras::shading::AttributeRate rate = arras::shading::AttributeRate::RATE_UNKNOWN;
    if (scopeAttr == kScopePrimitiveAttr) {
        rate = arras::shading::AttributeRate::RATE_CONSTANT;
    } else if (scopeAttr == kScopeFaceAttr) {
        rate = arras::shading::AttributeRate::RATE_UNIFORM;
    } else if (scopeAttr == kScopePointAttr) {
        // vertex and varying both map to 'point', while vertex has interpolationType = 'subdiv'
        if (interpAttr == kInterpSubdivAttr) {
            rate = arras::shading::AttributeRate::RATE_VERTEX;
        } else {
            rate = arras::shading::AttributeRate::RATE_VARYING;
        }
    } else if (scopeAttr == kScopeVertexAttr) {
        rate = arras::shading::AttributeRate::RATE_FACE_VARYING;
    } else if (scopeAttr == kScopePartAttr) {
        rate = arras::shading::AttributeRate::RATE_PART;
    }

    return rate;
}

const rdl2::String sDefaultPartName{};

arras::geom::LayerAssignmentId
createPerFaceAssignmentId(const rdl2::Geometry* rdlGeometry,
                          const kodachi::GroupAttribute& kodachiGeometryAttr,
                          const rdl2::Layer* rdlLayer,
                          const int32_t faceCount) {
    // per face assignment id
    const int meshAssignmentId = rdlLayer->getAssignmentId(rdlGeometry,
            sDefaultPartName);

    const kodachi::GroupAttribute partsAttr =
            kodachiGeometryAttr.getChildByName("parts");

    if (partsAttr.isValid()) {
        std::vector<int> faceAssignmentIds(faceCount, meshAssignmentId);

        for (const auto partPair : partsAttr) {
            const rdl2::String partName(partPair.name);
            const kodachi::IntAttribute facesAttr(partPair.attribute);
            const int partAssignmentId =
                    rdlLayer->getAssignmentId(rdlGeometry, partName);

            const auto faces = facesAttr.getNearestSample(0.f);
            for (const auto i : faces) {
                if (i < faceCount) {
                    faceAssignmentIds[i] = partAssignmentId;
                }
            }
        }

        return arras::geom::LayerAssignmentId(std::move(faceAssignmentIds));
    }

    return arras::geom::LayerAssignmentId(meshAssignmentId);
}

template <class face_vertex_count_t>
face_vertex_count_t
createFaceVertexCount(const kodachi::IntAttribute startIndexAttr)
{
    const auto startIndex = startIndexAttr.getNearestSample(0.f);

    const size_t count = startIndex.size() - 1;

    face_vertex_count_t faceVertexCount(count);

    for (size_t i = 0; i < count; ++i) {
        faceVertexCount[i] = startIndex[i + 1] - startIndex[i];
    }

    return faceVertexCount;
}

using namespace arras;
using namespace arras::geom;
using namespace arras::shading;

class KodachiMeshProcedural : public ProceduralLeaf
{
public:
    KodachiMeshProcedural(const geom::State &state)
        : ProceduralLeaf(state)
        , mSubdMesh(nullptr)
        , mPolygonMesh(nullptr)
    {}

    void generate(const GenerateContext &generateContext,
            const XformSamples &parent2render);

    void update(const UpdateContext &updateContext,
            const XformSamples &parent2render);

private:
    VertexBuffer<Vec3fa, InterleavedTraits> getVertexData(const rdl2::Geometry* rdlGeometry,
                                                          const GenerateContext &generateContext,
                                                          const kodachi::GroupAttribute& kodachiGeometryAttr,
                                                          PrimitiveAttributeTable &primitiveAttributeTable);

    std::unique_ptr<SubdivisionMesh> createSubdMesh(
        const kodachi_moonray::KodachiGeometry* kodachiGeometry,
        const kodachi::GroupAttribute& kodachiGeometryAttr,
        const GenerateContext &generateContext);

    std::unique_ptr<PolygonMesh> createPolyMesh(
        const kodachi_moonray::KodachiGeometry* kodachiGeometry,
        const kodachi::GroupAttribute& kodachiGeometryAttr,
        const GenerateContext &generateContext);

    SubdivisionMesh* mSubdMesh;
    PolygonMesh* mPolygonMesh;

    static const std::string sPrimitiveName;
};

const std::string KodachiMeshProcedural::sPrimitiveName("generated_mesh");


void
KodachiMeshProcedural::generate(
        const GenerateContext &generateContext,
        const XformSamples &parent2render)
{
    const kodachi_moonray::KodachiGeometry *kodachiGeometry =
        static_cast<const kodachi_moonray::KodachiGeometry*>(
                generateContext.getRdlGeometry());

    kodachi::GroupAttribute kodachiGeometryAttr = kodachiGeometry->mKodachiAttr;

    if (!kodachiGeometryAttr.isValid()) {
        const auto clientWrapper = std::move(kodachiGeometry->mClientWrapper);

        if (!clientWrapper) {
            kodachiGeometry->error("KodachiGeometry does not have an Attribute or ClientWrapper");
            return;
        }

        const arras::rdl2::String& scenegraphLocation =
                kodachiGeometry->get<arras::rdl2::String>("scenegraph_location");

        if (scenegraphLocation.empty()) {
            kodachiGeometry->error("scenegraph location not set");
            return;
        }

        const auto locationAttrs = clientWrapper->cookLocation(scenegraphLocation);
        if (!locationAttrs.isValid()) {
            kodachiGeometry->error("KodachiRuntime: location does not exist");
            return;
        }

        kodachiGeometryAttr = locationAttrs.getChildByName("rdl2.sceneObject.kodachiGeometry");

        if (!kodachiGeometryAttr.isValid()) {
            const kodachi::StringAttribute errorMessageAttr(locationAttrs.getChildByName("errorMessage"));
            if (errorMessageAttr.isValid()) {
                kodachiGeometry->error(errorMessageAttr.getValue());
            } else {
                kodachiGeometry->error("Could not cook kodachiGeometry attributes");
            }
            return;
        }
    }

    const kodachi::IntAttribute isSubdAttr =
            kodachiGeometryAttr.getChildByName("is_subd");

    const bool isSubd = isSubdAttr.getValue(true, false);

    std::unique_ptr<Primitive> primitive;
    if (!isSubd) {
        primitive = createPolyMesh(kodachiGeometry, kodachiGeometryAttr,
                generateContext);
    } else {
        primitive = createSubdMesh(kodachiGeometry, kodachiGeometryAttr,
            generateContext);
    }

    if (primitive) {
        // may need to convert the primitive to instance to handle
        // rotation motion blur
        std::unique_ptr<Primitive> p = convertForMotionBlur(
            generateContext, std::move(primitive),
            (kodachiGeometry->get(attrUseRotationMotionBlur) &&
            parent2render.size() > 1));
        addPrimitive(std::move(p),
            generateContext.getMotionBlurParams(), parent2render);

    }

    if (kodachiGeometry->mReleaseAttr) {
        kodachiGeometry->mKodachiAttr = kodachi::GroupAttribute{};
    }
}


void
KodachiMeshProcedural::update(
        const UpdateContext &updateContext,
        const XformSamples &parent2render)
{
    const std::vector<const std::vector<float>*> &vertexDatas =
        updateContext.getMeshVertexDatas();

    XformSamples prim2render = computePrim2Render(getState(), parent2render);
    if (mSubdMesh != nullptr) {
        mSubdMesh->updateVertexData(
            *vertexDatas[0], prim2render);
    } else {
        mPolygonMesh->updateVertexData(
            *vertexDatas[0], prim2render);
    }

    mDeformed = true;
}


VertexBuffer<Vec3fa, InterleavedTraits>
KodachiMeshProcedural::getVertexData(const rdl2::Geometry* rdlGeometry,
                                     const GenerateContext &generateContext,
                                     const kodachi::GroupAttribute& kodachiGeometryAttr,
                                     PrimitiveAttributeTable &primitiveAttributeTable)
{
    const kodachi::FloatAttribute vertexAttr =
            kodachiGeometryAttr.getChildByName("point.P");

    const kodachi::FloatAttribute velocityAttr =
            kodachiGeometryAttr.getChildByName("point.v");

    const kodachi::GroupAttribute accelerationAttr =
            kodachiGeometryAttr.getChildByName("acceleration");

    const int64_t vertNumValues = vertexAttr.getNumberOfValues();
    const size_t  vertCount = vertNumValues / 3;
    if (vertCount == 0) {
        rdlGeometry->error("'point.P' is empty");
        return {};
    }

    bool pos1Valid = (vertexAttr.getNumberOfTimeSamples() > 1);
    bool vel0Valid = (velocityAttr.getNumberOfValues() == vertNumValues);
    bool vel1Valid = (velocityAttr.getNumberOfTimeSamples() > 1);
    bool acc0Valid = false;
    if (accelerationAttr.isValid()) {
        const kodachi::FloatAttribute accValueAttr = accelerationAttr.getChildByName("value");
        if (accValueAttr.getNumberOfValues() == vertNumValues) {
            acc0Valid = true;
        } else {
            const kodachi::IntAttribute accIndexAttr = accelerationAttr.getChildByName("index");
            if (accIndexAttr.getNumberOfValues() == (vertNumValues / 3)) {
                acc0Valid = true;
            }
        }
    }

    const auto motionBlurData = kodachi_moonray::computeMotionBlurData(
            generateContext, static_cast<rdl2::MotionBlurType>(rdlGeometry->get(attrMotionBlurType)),
            pos1Valid, vel0Valid, vel1Valid, acc0Valid);

    const auto& motionSteps = motionBlurData.mMotionSteps;

    // Copy vertices
    VertexBuffer<Vec3fa, InterleavedTraits> vertices(vertCount, motionSteps.size());
    {
        for (size_t m = 0; m < motionSteps.size(); ++m) {
            const auto vertexSample = vertexAttr.getNearestSample(motionSteps[m]);
            for (size_t i = 0; i < vertCount; ++i) {
                const size_t idx = i * 3;
                vertices(i, m) = Vec3fa(vertexSample[idx], vertexSample[idx + 1], vertexSample[idx + 2], 0.f);
            }
        }
    }

    // Add velocity data
    if (motionBlurData.mUseVelocity) {
        const kodachi::FloatAttribute velocityScaleAttr =
                kodachiGeometryAttr.getChildByName("velocity_scale");

        std::vector<std::vector<Vec3f>> velocities;

        for (size_t i = 0; i < motionSteps.size(); ++i) {
            const auto velocitySample = velocityAttr.getNearestSample(motionSteps[i]);
            velocities.push_back(kodachi_moonray::toVec3fVector(velocitySample));

            if (velocityScaleAttr.isValid()) {
                const float velocityScale = velocityScaleAttr.getValue();
                for (auto& v : velocities.back()) {
                    v *= velocityScale;
                }
            }
        }

        primitiveAttributeTable.addAttribute(StandardAttributes::sVelocity,
                                             RATE_VERTEX, std::move(velocities));
    }

    // Add acceleration data
    if (motionBlurData.mUseAcceleration) {
        std::vector<arras::math::Vec3f> acceleration;
        {
            const kodachi::FloatAttribute accelerationValueAttr =
                    accelerationAttr.getChildByName("value");
            if (accelerationValueAttr.isValid()) {
                acceleration = kodachi_moonray::toVec3fVector(
                        accelerationValueAttr.getNearestSample(0.f));
            } else {
                const kodachi::IntAttribute indexAttr =
                        accelerationAttr.getChildByName("index");
                const kodachi::FloatAttribute indexedValueAttr =
                        accelerationAttr.getChildByName("indexedValue");

                const auto index = indexAttr.getNearestSample(0.f);
                const auto indexedValue = indexedValueAttr.getNearestSample(0.f);

                acceleration.reserve(index.size() * 3);

                for (auto i : index) {
                    acceleration.emplace_back(indexedValue[i * 3],
                                              indexedValue[(i * 3) + 1],
                                              indexedValue[(i * 3) + 2]);
                }
            }
        }

        primitiveAttributeTable.addAttribute(StandardAttributes::sAcceleration,
                                             RATE_VERTEX, std::move(acceleration));
    }

    return vertices;
}

// TODO - creases and corners... not supported by Moonray at this time
std::unique_ptr<SubdivisionMesh>
KodachiMeshProcedural::createSubdMesh(
    const kodachi_moonray::KodachiGeometry* kodachiGeometry,
    const kodachi::GroupAttribute& kodachiGeometryAttr,
    const GenerateContext &generateContext)
{
    // get subd scheme
    SubdivisionMesh::Scheme scheme = SubdivisionMesh::Scheme::CATMULL_CLARK;
    {
        const kodachi::IntAttribute subdSchemeAttr =
                kodachiGeometryAttr.getChildByName("subd_scheme");
        if (subdSchemeAttr.isValid()) {
            if (subdSchemeAttr.getValue() == 0) {
                scheme = SubdivisionMesh::Scheme::BILINEAR;
            }
        }
    }

    SubdivisionMesh::FaceVertexCount faceVertexCount;
    SubdivisionMesh::IndexBuffer indexBuffer;
    {
        // Set the vert per face count
        const kodachi::IntAttribute startIndexAttr =
                kodachiGeometryAttr.getChildByName("poly.startIndex");

        if (startIndexAttr.getNumberOfValues() == 0) {
            kodachiGeometry->error("'poly.startIndex' attr not valid");
            return nullptr;
        }

        const kodachi::IntAttribute vertexListAttr =
                kodachiGeometryAttr.getChildByName("poly.vertexList");

        if (vertexListAttr.getNumberOfValues() == 0) {
            kodachiGeometry->error("'poly.vertexList' attr not valid");
            return nullptr;
        }

        faceVertexCount = createFaceVertexCount<SubdivisionMesh::FaceVertexCount>(startIndexAttr);

        if (faceVertexCount.empty()) {
            kodachiGeometry->error("faceVertexCount is empty");
            return nullptr;
        }

        // Store the vert indices in order of face list to build the mesh
        const auto vertexList = vertexListAttr.getNearestSample(0.f);

        indexBuffer = SubdivisionMesh::IndexBuffer(vertexList.begin(), vertexList.end());
    }

    PrimitiveAttributeTable primitiveAttributeTable;

    // Get the vertices, velocities, uvs, normals etc.
    SubdivisionMesh::VertexBuffer vertices = getVertexData(kodachiGeometry,
            generateContext, kodachiGeometryAttr, primitiveAttributeTable);

    if (vertices.empty()) {
        return nullptr;
    }

    const size_t faceCount = faceVertexCount.size();

    // per face assignment id
    LayerAssignmentId layerAssignmentId =
        createPerFaceAssignmentId(kodachiGeometry, kodachiGeometryAttr,
                                  generateContext.getRdlLayer(), faceCount);

    // process arbitrary data
    {
        const kodachi::GroupAttribute arbAttr =
                kodachiGeometryAttr.getChildByName("arbitrary");

        const auto& requestedAttributes = generateContext.getRequestedAttributes();

        kodachi_moonray::processArbitraryData(
                arbAttr, primitiveAttributeTable, requestedAttributes,
                generateContext.getMotionSteps(), kodachiGeometry, rateFunc);

        // add UVs if present and not already added
        if (requestedAttributes.count(arras::shading::StandardAttributes::sSurfaceST) == 0) {
            arras::shading::AttributeKeySet additionalAttributes;
            additionalAttributes.insert(arras::shading::StandardAttributes::sSurfaceST);

            kodachi_moonray::processArbitraryData(
                    arbAttr, primitiveAttributeTable, additionalAttributes,
                    generateContext.getMotionSteps(), kodachiGeometry, rateFunc);
        }
    }

    // build the primitive
    std::unique_ptr<SubdivisionMesh> primitive = createSubdivisionMesh(
        scheme, std::move(faceVertexCount), std::move(indexBuffer),
        std::move(vertices), std::move(layerAssignmentId),
        std::move(primitiveAttributeTable));

    // set additional primitive attributes
    {
        // mesh resolution
        {
            int meshResolution = kodachiGeometry->get(attrMeshResolution);

            const rdl2::SceneVariables& vars =
                    kodachiGeometry->getSceneClass().getSceneContext()->getSceneVariables();
            if (vars.get(rdl2::SceneVariables::sEnableMaxGeomResolution)) {
                meshResolution = std::min(meshResolution,
                        vars.get(rdl2::SceneVariables::sMaxGeomResolution));
            }

            primitive->setMeshResolution(meshResolution);
        }

        // adaptive error
        {
            // get adaptive error (only used when adaptive tessellation got enabled)
            float adaptiveError = kodachiGeometry->get(attrAdaptiveError);
            // TODO rotation motion blur involves instancing logic that would
            // break adaptive tessellation right now.
            // Remove this switching logic once we have instancing supports
            // adaptive tessellation
            if (kodachiGeometry->get(attrUseRotationMotionBlur)) {
                adaptiveError = 0.0f;
            }

            primitive->setAdaptiveError(adaptiveError);
        }

        // parts
        {
            const kodachi::GroupAttribute partsAttr =
                    kodachiGeometryAttr.getChildByName("parts");

            const size_t partCount = partsAttr.getNumberOfChildren();

            // Fill in table of faces->parts
            SubdivisionMesh::FaceToPartBuffer faceToPart(faceCount, partCount);

            for (size_t p = 0; p < partCount; ++p) {
                const kodachi::IntAttribute facesAttr = partsAttr.getChildByIndex(p);
                const auto faces = facesAttr.getNearestSample(0.f);
                for (const auto i : faces) {
                    faceToPart[i] = p;
                }
            }

            primitive->setParts(partCount + 1, std::move(faceToPart));
        }

        primitive->setName(sPrimitiveName);
        primitive->setIsSingleSided(
                kodachiGeometry->getSideType() == rdl2::Geometry::SINGLE_SIDED);
        primitive->setIsNormalReversed(kodachiGeometry->getReverseNormals());
        primitive->setModifiability(Primitive::Modifiability::DEFORMABLE);
        primitive->setCurvedMotionBlurSampleCount(
                kodachiGeometry->get(attrCurvedMotionBlurSampleCount));
    }

    mSubdMesh = primitive.get();

    return primitive;
}


std::unique_ptr<PolygonMesh>
KodachiMeshProcedural::createPolyMesh(
    const kodachi_moonray::KodachiGeometry* kodachiGeometry,
    const kodachi::GroupAttribute& kodachiGeometryAttr,
    const GenerateContext &generateContext)
{
    // Set the vert per face count
    PolygonMesh::FaceVertexCount faceVertexCount;
    PolygonMesh::IndexBuffer indexBuffer;
    {
        // Set the vert per face count
        const kodachi::IntAttribute startIndexAttr =
                kodachiGeometryAttr.getChildByName("poly.startIndex");

        if (startIndexAttr.getNumberOfValues() == 0) {
            kodachiGeometry->error("'poly.startIndex' attr not valid");
            return nullptr;
        }

        const kodachi::IntAttribute vertexListAttr =
                kodachiGeometryAttr.getChildByName("poly.vertexList");

        if (vertexListAttr.getNumberOfValues() == 0) {
            kodachiGeometry->error("'poly.vertexList' attr not valid");
            return nullptr;
        }

        faceVertexCount = createFaceVertexCount<PolygonMesh::FaceVertexCount>(startIndexAttr);

        if (faceVertexCount.empty()) {
            kodachiGeometry->error("faceVertexCount is empty");
            return nullptr;
        }

        // Store the vert indices in order of face list to build the mesh
        const auto vertexList = vertexListAttr.getNearestSample(0.f);

        indexBuffer = PolygonMesh::IndexBuffer(vertexList.begin(), vertexList.end());
    }

    PrimitiveAttributeTable primitiveAttributeTable;

    // Get the vertices, velocities, uvs, normals etc.
    PolygonMesh::VertexBuffer vertices = getVertexData(kodachiGeometry,
            generateContext, kodachiGeometryAttr, primitiveAttributeTable);

    if (vertices.empty()) {
        return nullptr;
    }

    const size_t faceCount = faceVertexCount.size();

    // per face assignment id
    LayerAssignmentId layerAssignmentId =
        createPerFaceAssignmentId(kodachiGeometry, kodachiGeometryAttr,
                                  generateContext.getRdlLayer(), faceCount);

    // process arbitrary data
    {
        const kodachi::GroupAttribute arbAttr =
                kodachiGeometryAttr.getChildByName("arbitrary");

        const auto& requestedAttributes = generateContext.getRequestedAttributes();

        kodachi_moonray::processArbitraryData(
                arbAttr, primitiveAttributeTable, requestedAttributes,
                generateContext.getMotionSteps(), kodachiGeometry, rateFunc);

        // add UVs and normals if present and not already added
        arras::shading::AttributeKeySet additionalAttributes;

        if (requestedAttributes.count(arras::shading::StandardAttributes::sSurfaceST) == 0) {
            additionalAttributes.insert(arras::shading::StandardAttributes::sSurfaceST);
        }

        if (requestedAttributes.count(arras::shading::StandardAttributes::sNormal) == 0) {
            additionalAttributes.insert(arras::shading::StandardAttributes::sNormal);
        }

        if (!additionalAttributes.empty()) {
            kodachi_moonray::processArbitraryData(
                    arbAttr, primitiveAttributeTable, additionalAttributes,
                    generateContext.getMotionSteps(), kodachiGeometry, rateFunc);
        }
    }

    // get resolution
    int meshResolution = kodachiGeometry->get(attrMeshResolution);
    const rdl2::SceneVariables& vars =
            kodachiGeometry->getSceneClass().getSceneContext()->getSceneVariables();
    if (vars.get(rdl2::SceneVariables::sEnableMaxGeomResolution)) {
        meshResolution = std::min(meshResolution,
                vars.get(rdl2::SceneVariables::sMaxGeomResolution));
    }

    // Get the number of parts
    size_t partCount = 0;
    PolygonMesh::FaceToPartBuffer faceToPart;
    {
        const kodachi::GroupAttribute partsAttr =
                kodachiGeometryAttr.getChildByName("parts");

        partCount = partsAttr.getNumberOfChildren();

        if (partCount > 0) {
            // Fill in table of faces->parts
            // Following the conventions of AbcGeometry, declare 1 more part than
            // is specified to be the "default" part. Assign all faces not
            // in a part to the default part.
            faceToPart = PolygonMesh::FaceToPartBuffer(faceCount, partCount);

            for (size_t p = 0; p < partCount; ++p) {
                const kodachi::IntAttribute facesAttr = partsAttr.getChildByIndex(p);
                const auto faces = facesAttr.getNearestSample(0.f);
                for (const auto i : faces) {
                    faceToPart[i] = p;
                }
            }

            ++partCount;
        }
    }

    removeUnassignedFaces(generateContext.getRdlLayer(), layerAssignmentId, faceToPart,
        faceVertexCount, indexBuffer, &primitiveAttributeTable);
    
    // check if mesh is valid
    if (faceVertexCount.empty() || indexBuffer.empty()) {
        // either the input mesh is invalid or the mesh doesn't have
        // any assigned material, skip generating the primitive
        return nullptr;
    }
    
    // build the primitive
    std::unique_ptr<PolygonMesh> primitive = createPolygonMesh(
        std::move(faceVertexCount), std::move(indexBuffer),
        std::move(vertices), std::move(layerAssignmentId),
        std::move(primitiveAttributeTable));

    // set additional primitive attributes
    {
        // mesh resolution
        {
            int meshResolution = kodachiGeometry->get(attrMeshResolution);

            const rdl2::SceneVariables& vars =
                    kodachiGeometry->getSceneClass().getSceneContext()->getSceneVariables();
            if (vars.get(rdl2::SceneVariables::sEnableMaxGeomResolution)) {
                meshResolution = std::min(meshResolution,
                        vars.get(rdl2::SceneVariables::sMaxGeomResolution));
            }

            primitive->setMeshResolution(meshResolution);
        }

        // adaptive error
        {
            // get adaptive error (only used when adaptive tessellation got enabled)
            float adaptiveError = kodachiGeometry->get(attrAdaptiveError);
            // TODO rotation motion blur involves instancing logic that would
            // break adaptive tessellation right now.
            // Remove this switching logic once we have instancing supports
            // adaptive tessellation
            if (kodachiGeometry->get(attrUseRotationMotionBlur)) {
                adaptiveError = 0.0f;
            }

            primitive->setAdaptiveError(adaptiveError);
        }

        // set additional primitive attributes
        primitive->setName(sPrimitiveName);
        primitive->setIsSingleSided(
                kodachiGeometry->getSideType() == rdl2::Geometry::SINGLE_SIDED);
        primitive->setIsNormalReversed(kodachiGeometry->getReverseNormals());
        primitive->setParts(partCount, std::move(faceToPart));
        primitive->setSmoothNormal(kodachiGeometry->get(attrSmoothNormal));
        primitive->setCurvedMotionBlurSampleCount(
                kodachiGeometry->get(attrCurvedMotionBlurSampleCount));
    }

    mPolygonMesh = primitive.get();

    return primitive;
}

} // anonymous namespace

RDL2_DSO_CLASS_BEGIN(KodachiMeshGeometry, kodachi_moonray::KodachiGeometry)

public:
    RDL2_DSO_DEFAULT_CTOR(KodachiMeshGeometry)

    arras::geom::Procedural* createProcedural() const override
    {
        static std::once_flag sAttributeBootstrapFlag;

        std::call_once(sAttributeBootstrapFlag, []()
        {
            const char* kodachiRoot = ::getenv("KODACHI_ROOT");
            if (!kodachiRoot) {
                throw std::runtime_error(
                        "'KODACHI_ROOT' environment variable not set");
            }

            if (!kodachi::Bootstrap(kodachiRoot)) {
                throw std::runtime_error(
                        "Failed to bootstrap Kodachi::Attribute");
            }
        });

        return new KodachiMeshProcedural(geom::State{});
    }

    void destroyProcedural() const override
    {
        delete mProcedural;
    }

    bool deformed() const override
    {
        return mDeformed || mProcedural->deformed();
    }

    void resetDeformed() override
    {
        mDeformed = false;
        mProcedural->resetDeformed();
    }

RDL2_DSO_CLASS_END(KodachiMeshGeometry)

