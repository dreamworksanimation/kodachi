// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>

#include <unordered_map>

namespace {

KdLogSetup("KPOPGeometry");

class KPOPGeometry: public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                R"(/root/world//*{@type=="rdl2" and hasattr("rdl2.meta.isGeometry")})");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        // moonrayStatements
        const kodachi::GroupAttribute moonrayStatementsAttr = interface.getAttr("moonrayStatements");
        if (moonrayStatementsAttr.isValid()) {
            // Get the moonrayStatements attrs that apply directly to
            // Geometry scene objects and add them to the attrs list
            kodachi::GroupBuilder geometryAttrsGb;
            geometryAttrsGb
                .setGroupInherit(false)
                .update(moonrayStatementsAttr)
                .del("cutout")
                .del("sceneBuild")
                .del("arbitraryAttrs")
                // TODO: These are added by USD conditioning, should that be the case?
                .del("model")
                .del("subasset_name")
                .del("subd_type");

            // Retain the builder contents in case we need it for auto-instancing
            const kodachi::GroupAttribute geometryAttrs =
                    geometryAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain);

            // auto instancing attrs
            {
                const kodachi::IntAttribute autoInstancingEnabledAttr =
                        interface.getAttr("rdl2.meta.autoInstancing.enabled");

                if (autoInstancingEnabledAttr.isValid()) {
                    // We want to exclude the visibility moonrayStatements
                    // attrs from determining the instance ID, but store them
                    // on the location since each instance can specify its
                    // own visibility
                    kodachi::GroupBuilder autoInstancingAttrsGb;
                    autoInstancingAttrsGb
                        .setGroupInherit(false)
                        .update(interface.getAttr("rdl2.meta.autoInstancing.attrs"))
                        .update(geometryAttrs);

                    const kodachi::GroupAttribute visibilityAttr =
                        geometryAttrsGb
                            .del("label")
                            .del("static")
                            .del("side type")
                            .del("reverse normals")
                            .del("motion_blur_type")
                            .del("use_rotation_motion_blur")
                            .del("curved_motion_blur_sample_count")
                            .del("velocity_scale")
                            .build();

                    if (visibilityAttr.isValid()) {
                        kodachi::GroupBuilder instanceAttrsGb;
                        instanceAttrsGb
                            .setGroupInherit(false)
                            .update(interface.getAttr("rdl2.sceneObject.instance.attrs"))
                            .update(visibilityAttr);

                        // We have to set the visibility flags on the GroupGeometry
                        // referencing this geometry
                        interface.setAttr("rdl2.sceneObject.instance.attrs",
                                          instanceAttrsGb.build(), false);
                    }

                    interface.setAttr("rdl2.meta.autoInstancing.attrs",
                                      autoInstancingAttrsGb.build(), false);
                }
            }

            // SceneObject Attrs
            {
                kodachi::GroupBuilder sceneObjectAttrsGb;
                sceneObjectAttrsGb
                    .setGroupInherit(false)
                    .update(interface.getAttr("rdl2.sceneObject.attrs"))
                    .update(geometryAttrs);

                interface.setAttr("rdl2.sceneObject.attrs",
                                  sceneObjectAttrsGb.build(), false);
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        kodachi::OpDescriptionBuilder builder;

        builder.setSummary("Sets attributes specific to the locations that will become rdl2::Geometry");

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPGeometry)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPGeometry, "KPOPGeometry", 0, 1);
}

