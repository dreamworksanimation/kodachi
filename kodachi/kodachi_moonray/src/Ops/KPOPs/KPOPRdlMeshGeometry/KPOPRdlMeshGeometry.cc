// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/AttributeUtils.h>
#include <kodachi/attribute/InterpolatingGroupBuilder.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi_moonray/motion_blur_util/MotionBlurUtil.h>

#include <sstream>
#include <memory>
#include <unordered_set>

namespace {

using namespace kodachi_moonray;

KdLogSetup("KPOPRdlMeshGeometry");

// Creates a vector of the number of vertices per face in a mesh from the startIndex attr
// The indices for each polygon N are from startIndex(N) to startIndex(N+1)-1
// Therefore the number of verticies belonging to a polygon are
// startIndex(N+1) - startIndex(N)
kodachi::IntAttribute
createFaceVertexCount(const kodachi::IntAttribute& startIndexAttr)
{
    const auto startIndex = startIndexAttr.getNearestSample(0.f);

    const size_t count = startIndex.size() - 1;

    kodachi::IntArray faceVertexCount(new kodachi::Int[count]);

    for (size_t i = 0; i < count; ++i) {
        faceVertexCount[i] = startIndex[i + 1] - startIndex[i];
    }

    return kodachi::ZeroCopyIntAttribute::create(std::move(faceVertexCount), count);
}

class KPOPRdlMeshGeometry : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            const kodachi::IntAttribute useRdlGeometryAttr =
                    interface.getAttr("moonrayGlobalStatements.use_rdl_geometry");

            if (useRdlGeometryAttr.getValue(false, false)) {
                kodachi::GroupBuilder opArgsBuilder;
                opArgsBuilder.update(interface.getOpArg(""));
                opArgsBuilder.set("useRdlMesh", kodachi::IntAttribute(true));
                interface.replaceChildTraversalOp("", opArgsBuilder.build());
            }
        }

        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and hasattr("rdl2.meta.isMesh")})");
        static const kodachi::StringAttribute kSubdmeshAttr("subdmesh");
        static const kodachi::StringAttribute kFacesetAttr("faceset");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const bool useRdlMeshGeometry = kodachi::IntAttribute(
                interface.getOpArg("useRdlMesh")).getValue(false, false);

        // SceneClass and SceneObject name
        if (useRdlMeshGeometry) {
            static const kodachi::StringAttribute kRdlMeshGeometryAttr("RdlMeshGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kRdlMeshGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_RdlMeshGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        } else {
            static const kodachi::StringAttribute kKodachiMeshGeometryAttr("KodachiMeshGeometry");
            interface.setAttr("rdl2.sceneObject.sceneClass", kKodachiMeshGeometryAttr, false);

            const std::string objectName =
                    kodachi::concat(interface.getInputLocationPath(), "_KodachiMeshGeometry");
            interface.setAttr("rdl2.sceneObject.name", kodachi::StringAttribute(objectName), false);
        }

        const kodachi::StringAttribute typeAttr = interface.getAttr("rdl2.meta.kodachiType");
        const bool isSubd = typeAttr == kSubdmeshAttr;

        const float shutterOpen = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterOpen")).getValue();
        const float shutterClose = kodachi::FloatAttribute(
                interface.getAttr("rdl2.meta.shutterClose")).getValue();

        kodachi::InterpolatingGroupBuilder meshAttrsGb(shutterOpen, shutterClose);
        meshAttrsGb.setGroupInherit(false);

        const kodachi::GroupAttribute geometryAttr = interface.getAttr("geometry");
        if (!geometryAttr.isValid()) {
            interface.deleteSelf();
            return;
        }

        // at a minimum we need a point list, vertex list, and a face vertex count list
        const kodachi::FloatAttribute pointListAttr =
                geometryAttr.getChildByName("point.P");

        const kodachi::IntAttribute startIndexAttr =
                geometryAttr.getChildByName("poly.startIndex");

        const kodachi::IntAttribute vertexListAttr =
                geometryAttr.getChildByName("poly.vertexList");

        if (!pointListAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "Missing point.P attribute");
            return;
        }

        if (!startIndexAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "Missing poly.startIndex attribute");
            return;
        }

        if (!vertexListAttr.isValid()) {
            kodachi::ReportNonCriticalError(interface, "Missing poly.vertexList attribute");
            return;
        }

        // vertices_by_index, face_vertex_count, vertex_list, velocity_list, acceleration_list
        if (useRdlMeshGeometry) {
            meshAttrsGb.set("face_vertex_count", createFaceVertexCount(startIndexAttr));
            meshAttrsGb.set("vertices_by_index", vertexListAttr);

            kodachi::GroupAttribute motionBlurAttrs;

            if (!kodachi::IntAttribute(
                    interface.getAttr("rdl2.meta.mbEnabled")).getValue()) {
                motionBlurAttrs =
                        motion_blur_util::createStaticMotionBlurAttributes(pointListAttr);
            } else {
                const kodachi::Attribute initialMotionBlurTypeAttr =
                        interface.getAttr("moonrayStatements.motion_blur_type");

                const kodachi::FloatAttribute velocityAttr =
                        geometryAttr.getChildByName("point.v");

                const kodachi::GroupAttribute accelerationAttr =
                        geometryAttr.getChildByName("arbitrary.accel");

                if (accelerationAttr.isValid()) {
                    // Prevent acceleration from being added as a PrimitiveAttribute
                    interface.deleteAttr("geometry.arbitrary.accel");
                }

                const float fps = kodachi::FloatAttribute(
                        interface.getAttr("rdl2.meta.fps")).getValue();

                motionBlurAttrs = motion_blur_util::createMotionBlurAttributes(
                        initialMotionBlurTypeAttr,
                        pointListAttr,
                        velocityAttr,
                        accelerationAttr,
                        shutterOpen,
                        shutterClose,
                        fps);
            }

            const kodachi::StringAttribute errorMessageAttr =
                    motionBlurAttrs.getChildByName("errorMessage");

            if (errorMessageAttr.isValid()) {
                interface.setAttr("errorMessage", errorMessageAttr);
                return;
            } else {
                const kodachi::StringAttribute warningMessageAttr =
                        motionBlurAttrs.getChildByName("warningMessage");

                if (warningMessageAttr.isValid()) {
                    interface.setAttr("warningMessage", warningMessageAttr);
                }

                meshAttrsGb.update(motionBlurAttrs.getChildByName("attrs"));
            }

            // part_list, part_face_count_list, part_face_indicies
            const auto potentialChildrenSamples =
                    interface.getPotentialChildren().getSamples();
            if (potentialChildrenSamples.isValid()) {
                kodachi::StringVector partList;
                kodachi::IntVector partFaceCountList;
                kodachi::IntVector partFaceIndicies;
                partFaceIndicies.reserve(startIndexAttr.getNumberOfValues() - 1);

                for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                    if (kodachi::StringAttribute(interface.getAttr(
                            "rdl2.meta.kodachiType", childName)) != kFacesetAttr) {
                        continue;
                    }

                    partList.emplace_back(childName);

                    const auto facesSamples = kodachi::IntAttribute(
                            interface.getAttr("geometry.faces", childName)).getSamples();

                    if (!facesSamples.isValid()) {
                        continue;
                    }

                    const auto partFaces = facesSamples.front();

                    partFaceCountList.push_back(partFaces.size());
                    partFaceIndicies.insert(partFaceIndicies.end(),
                                            partFaces.begin(),
                                            partFaces.end());
                }

                if (!partList.empty()) {
                    meshAttrsGb.set("part_list",
                            kodachi::ZeroCopyAttribute<kodachi::StringAttribute>::create(std::move(partList)));
                    meshAttrsGb.set("part_face_count_list",
                            kodachi::ZeroCopyIntAttribute::create(std::move(partFaceCountList)));
                    meshAttrsGb.set("part_face_indices",
                            kodachi::ZeroCopyIntAttribute::create(std::move(partFaceIndicies)));
                }
            }
        } else {
            meshAttrsGb.setWithoutInterpolation("poly",
                    geometryAttr.getChildByName("poly"), false);

            meshAttrsGb.setWithoutInterpolation("point",
                    geometryAttr.getChildByName("point"), false);

            const kodachi::GroupAttribute accelerationAttr =
                    geometryAttr.getChildByName("arbitrary.accel");

            if (accelerationAttr.isValid()) {
                meshAttrsGb.setWithoutInterpolation("acceleration", accelerationAttr, false);

                // Prevent acceleration from being added as a PrimitiveAttribute
                interface.deleteAttr("geometry.arbitrary.accel");
            }

            // parts
            const auto potentialChildrenSamples =
                                    interface.getPotentialChildren().getSamples();
            if (potentialChildrenSamples.isValid()) {
                kodachi::GroupBuilder facesGb;

                for (const kodachi::string_view childName : potentialChildrenSamples.front()) {
                    if (kodachi::StringAttribute(interface.getAttr(
                            "rdl2.meta.kodachiType", childName)) == kFacesetAttr) {
                        facesGb.set(childName, interface.getAttr("geometry.faces", childName), false);
                    }
                }

                meshAttrsGb.setWithoutInterpolation("parts", facesGb.build(), false);
            }
        }

        // Rename st to surface_st. Even though Moonray documentation says
        // surface_st is for hair, all of the Moonshine procedurals use it
        // as the standard name for UVs.
        {
            const kodachi::GroupAttribute stAttr =
                    geometryAttr.getChildByName("arbitrary.st");

            if (stAttr.isValid()) {
                interface.deleteAttr("geometry.arbitrary.st");
                interface.setAttr("geometry.arbitrary.surface_st", stAttr);
            }
        }

        // Move polymesh normals to arbitrary.
        // submesh normals are always recomputed so no need to add them
        if (!isSubd) {
            const kodachi::FloatAttribute vertexNormalAttr =
                    geometryAttr.getChildByName("vertex.N");

            const kodachi::FloatAttribute pointNormalAttr =
                    geometryAttr.getChildByName("point.N");

            if (vertexNormalAttr.isValid()) {
                static const kodachi::StringAttribute kVertexScopeAttr("vertex");
                static const kodachi::StringAttribute kVector3Attr("vector3");

                interface.setAttr("geometry.arbitrary.normal",
                        kodachi::GroupAttribute("scope", kVertexScopeAttr,
                                                "inputType", kVector3Attr,
                                                "value", vertexNormalAttr,
                                false), false);
            } else if (pointNormalAttr.isValid()) {
                static const kodachi::StringAttribute kPointScopeAttr("point");
                static const kodachi::StringAttribute kVector3Attr("vector3");

                interface.setAttr("geometry.arbitrary.normal",
                        kodachi::GroupAttribute("scope", kPointScopeAttr,
                                                "inputType", kVector3Attr,
                                                "value", pointNormalAttr,
                                false), false);
            }
        }

        // is_subd
        meshAttrsGb.set("is_subd", kodachi::IntAttribute(isSubd));

        const kodachi::GroupAttribute meshAttrs = meshAttrsGb.build();

        // auto instancing attrs
        bool autoInstancingEnabled =
                interface.getAttr("rdl2.meta.autoInstancing.enabled").isValid();

        kodachi::GroupAttribute meshStatementsAttr =
                interface.getAttr("moonrayMeshStatements");
        {
            kodachi::GroupBuilder meshStatementsGb;
            meshStatementsGb
                .setGroupInherit(false)
                .update(meshStatementsAttr)
                .del("autoInstancing")
                .del("arbitrary outputs");

            // remove attributes not used by this type of mesh
            if (isSubd) {
                meshStatementsGb.del("smooth_normal");
            } else {
                meshStatementsGb.del("subd_scheme");
            }

            // perPartIDs is not an rdl attribute. Copy it to meta so we
            // can hold onto it's value for later.
            const kodachi::IntAttribute perPartIDsAttr =
                    meshStatementsAttr.getChildByName("perPartIDs");
            if (perPartIDsAttr.isValid()) {
                interface.setAttr("rdl2.meta.perPartIDs", perPartIDsAttr);
                meshStatementsGb.del("perPartIDs");
            }

            if (autoInstancingEnabled) {
                // adaptive error override
                // if adaptive error is set, determine if we should disable it
                // to allow this mesh to participate in auto-instancing
                const kodachi::FloatAttribute adaptiveErrorAttr =
                        meshStatementsAttr.getChildByName("adaptive error");

                if (adaptiveErrorAttr.getValue(0.f, false) > 0) {
                    const kodachi::GroupAttribute autoInstancingAttr =
                            meshStatementsAttr.getChildByName("autoInstancing");

                    const kodachi::IntAttribute disableAdaptiveErrorAttr =
                            autoInstancingAttr.getChildByName("disableAdaptiveError");
                    if (disableAdaptiveErrorAttr.getValue(true, false)) {
                        interface.setAttr("rdl2.sceneObject.instanceSource.attrs.adaptive_error",
                                kodachi::FloatAttribute(0.f));

                        const kodachi::IntAttribute clampMeshResolutionAttr =
                                autoInstancingAttr.getChildByName("clampMeshResolution");
                        if (clampMeshResolutionAttr.getValue(true, false)) {
                            const kodachi::FloatAttribute meshResolutionAttr =
                                    meshStatementsAttr.getChildByName("mesh_resolution");
                            const float meshResolution = meshResolutionAttr.getValue(2.f, false);

                            const kodachi::FloatAttribute clampAttr =
                                    autoInstancingAttr.getChildByName("meshResolution");

                            const float clampValue = clampAttr.getValue(4.f, false);

                            if (meshResolution > clampValue) {
                                interface.setAttr("rdl2.sceneObject.instanceSource.attrs.mesh_resolution",
                                        kodachi::FloatAttribute(clampValue));
                            }
                        }
                    } else {
                        autoInstancingEnabled = false;
                        interface.deleteAttr("rdl2.meta.autoInstancing");
                    }
                }
            }

            meshStatementsAttr = meshStatementsGb.build();
        }


        if (autoInstancingEnabled) {
            kodachi::GroupBuilder autoInstancingAttrsGb;
            autoInstancingAttrsGb
                .setGroupInherit(false)
                .update(interface.getAttr("rdl2.meta.autoInstancing.attrs"))
                .update(meshStatementsAttr)
                .del("adaptive error")
                .update(meshAttrs)
                ;

            interface.setAttr("rdl2.meta.autoInstancing.attrs",
                              autoInstancingAttrsGb.build(), false);
        }

        // SceneObject Attrs
        {
            kodachi::GroupBuilder sceneObjectAttrsGb;
            sceneObjectAttrsGb
                .setGroupInherit(false)
                .update(interface.getAttr("rdl2.sceneObject.attrs"))
                .update(meshStatementsAttr);
            if (useRdlMeshGeometry) {
                sceneObjectAttrsGb.update(meshAttrs);
            } else {
                sceneObjectAttrsGb.del("subd_scheme");

                interface.setAttr("rdl2.sceneObject.kodachiGeometry", meshAttrs, false);
                interface.setAttr("rdl2.sceneObject.kodachiGeometry.subd_scheme",
                        meshStatementsAttr.getChildByName("subd_scheme"), false);
            }

            interface.setAttr("rdl2.sceneObject.attrs", sceneObjectAttrsGb.build(), false);
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Sets attributes on locations that represent an RdlMeshGeometry procedural.");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPRdlMeshGeometry)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPRdlMeshGeometry, "KPOPRdlMeshGeometry", 0, 1);
}

