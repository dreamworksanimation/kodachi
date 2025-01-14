// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

#include <unordered_map>

namespace {

KdLogSetup("KPOPMeta");

// The kodachi scenegraph location types that can be used to create 'rdl2' locations
// Not all of them necessarily require meta tags
enum class LocationType : uint32_t
{
    CAMERA              = 1 << 0,
    CURVES              = 1 << 1,
    FACESET             = 1 << 2,
    INSTANCE_ARRAY      = 1 << 3,
    INSTANCE_SOURCE     = 1 << 4,
    INSTANCE            = 1 << 5,
    JOINT               = 1 << 6,
    LIGHT_FILTER        = 1 << 7,
    LIGHT               = 1 << 8,
    NURBSPATCH          = 1 << 9,
    POINTCLOUD          = 1 << 10,
    POLYMESH            = 1 << 11,
    RDL_ARCHIVE         = 1 << 12,
    RENDERER_PROCEDURAL = 1 << 13,
    SUBDMESH            = 1 << 14,
    VOLUME              = 1 << 15,
    INVALID
};

inline constexpr LocationType operator|(LocationType a, LocationType b)
{
    return static_cast<LocationType>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline constexpr bool operator&(LocationType a, LocationType b)
{
    return static_cast<bool>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline constexpr LocationType operator^(LocationType a, LocationType b)
{
    return static_cast<LocationType>(static_cast<uint32_t>(a) ^ static_cast<uint32_t>(b));
}


constexpr LocationType kMeshTypes = LocationType::POLYMESH | LocationType::SUBDMESH;

// Types that can be added to a GeometrySet
constexpr LocationType kGeometryTypes = kMeshTypes |
                                        LocationType::CURVES |
                                        LocationType::INSTANCE_ARRAY |
                                        LocationType::INSTANCE_SOURCE |
                                        LocationType::INSTANCE |
                                        LocationType::POINTCLOUD |
                                        LocationType::VOLUME;

constexpr LocationType kJointTypes = LocationType::JOINT |
                                     LocationType::NURBSPATCH;

// Types that have an xform
constexpr LocationType kNodeTypes = LocationType::CAMERA |
                                    LocationType::LIGHT |
                                    kGeometryTypes |
                                    kJointTypes;

// Types that can have materials and lightsets assigned to them
constexpr LocationType kLayerAssignableTypes = kGeometryTypes |
                                               LocationType::FACESET;

constexpr LocationType kMaterialAssignableTypes = (((kLayerAssignableTypes ^ LocationType::INSTANCE)
                                                  ^ LocationType::INSTANCE_ARRAY) ^ LocationType::INSTANCE_SOURCE);

constexpr LocationType kAutoInstanceableTypes = kMeshTypes | LocationType::CURVES | LocationType::RENDERER_PROCEDURAL;

LocationType
getLocationType(const kodachi::StringAttribute& typeAttr)
{
    using LocationTypeMap = std::unordered_map<kodachi::StringAttribute, LocationType, kodachi::AttributeHash>;

    static const LocationTypeMap kLocationTypeMap
    {
        { "camera"              , LocationType::CAMERA              },
        { "curves"              , LocationType::CURVES              },
        { "faceset"             , LocationType::FACESET             },
        { "instance array"      , LocationType::INSTANCE_ARRAY      },
        { "instance source"     , LocationType::INSTANCE_SOURCE     },
        { "instance"            , LocationType::INSTANCE            },
        { "joint"               , LocationType::JOINT               },
        { "light filter"        , LocationType::LIGHT_FILTER        },
        { "light"               , LocationType::LIGHT               },
        { "nurbspatch"          , LocationType::NURBSPATCH          },
        { "pointcloud"          , LocationType::POINTCLOUD          },
        { "polymesh"            , LocationType::POLYMESH            },
        { "rdl archive"         , LocationType::RDL_ARCHIVE         },
        { "renderer procedural" , LocationType::RENDERER_PROCEDURAL },
        { "subdmesh"            , LocationType::SUBDMESH            },
        { "volume"              , LocationType::VOLUME              }
    };

    const auto iter = kLocationTypeMap.find(typeAttr);
    if (iter != kLocationTypeMap.end()) {
        return iter->second;
    }

    return LocationType::INVALID;
}

class KPOPMeta : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            const int numSamples = kodachi::GetNumSamples(interface);
            const float shutterOpen = kodachi::GetShutterOpen(interface);
            const float shutterClose = kodachi::GetShutterClose(interface);
            const bool mbEnabled = (numSamples >= 2
                    && (std::fabs(shutterOpen - shutterClose) >
                        std::numeric_limits<float>::epsilon()));
            const float fps = kodachi::FloatAttribute(
                    interface.getAttr("moonrayGlobalStatements.fps")).getValue(24.f, false);

            kodachi::GroupBuilder opArgsGb;
            opArgsGb.update(interface.getOpArg(""));
            opArgsGb.set("shutterOpen", kodachi::FloatAttribute(shutterOpen));
            opArgsGb.set("shutterClose", kodachi::FloatAttribute(shutterClose));
            opArgsGb.set("mbEnabled", kodachi::IntAttribute(mbEnabled));
            opArgsGb.set("fps", kodachi::FloatAttribute(fps));

            const kodachi::IntAttribute autoInstancingAttr =
                    interface.getAttr("moonrayGlobalStatements.autoInstancing");

            if (autoInstancingAttr.isValid()) {
                opArgsGb.set("isAutoInstancingEnabled", autoInstancingAttr);
            }

            interface.replaceChildTraversalOp("", opArgsGb.build());

            return;
        }

        // Make a new location for any TraceSet collections.
        // This just creates a blank TraceSet rdl2 object, post-processing
        // of the "baked" attribute by MoonrayRenderState is used to fill it in.
        const kodachi::GroupAttribute collections(interface.getAttr("collections"));
        int64_t n = collections.getNumberOfChildren();
        for (int64_t i = 0; i < n; ++i) {
            const char* name(collections.getChildNameCStr(i));
            if (!name || strncmp(name, "traceSet__", 10)) continue;
            const kodachi::GroupAttribute collection(collections.getChildByIndex(i));
            const kodachi::StringAttribute baked(collection.getChildByName("baked"));

            kodachi::GroupBuilder so;
            static kodachi::StringAttribute TraceSet("TraceSet");
            so.set("sceneClass", TraceSet);
            so.set("name", kodachi::StringAttribute(name+10));
            so.set("disableAliasing", kodachi::IntAttribute(1));
            // See MoonrayRenderState that decodes this
            if (baked.isValid()) {
                so.set("baked", baked);
            } else {
                KdLogWarn(name << " must be baked for rendering.");
            }

            kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
            static kodachi::StringAttribute celAll("//*");
            asb.setCEL(celAll);
            static kodachi::StringAttribute rdl2("rdl2");
            asb.setAttr("type", rdl2);
            asb.setAttr("rdl2.sceneObject", so.build());

            interface.createChild(name+10, "AttributeSet", asb.build());
        }

        const kodachi::StringAttribute typeAttr = interface.getAttr("type");
        const LocationType locationType = getLocationType(typeAttr);
        if (locationType == LocationType::INVALID) {
            return;
        }

        kodachi::GroupBuilder metaGb;
        metaGb.setGroupInherit(false);

        const kodachi::IntAttribute trueAttr(true);

        if (locationType & kMeshTypes) {
            metaGb.set("isMesh", trueAttr);
        }

        if (locationType & kLayerAssignableTypes) {
            if (interface.getAttr("disableLayerAssign",
                   (locationType == LocationType::FACESET) ? ".." : "").isValid()) {
                ; // for meshLight copiedGeometry
            } else {
                metaGb.set("isLayerAssignable", trueAttr);

                if (locationType & kMaterialAssignableTypes) {
                    metaGb.set("isMaterialAssignable", trueAttr);
                }
            }
        }

        if (locationType & kGeometryTypes) {
            metaGb.set("isGeometry", trueAttr);
        }

        if (locationType & kNodeTypes) {
            metaGb.set("isNode", trueAttr);
        }

        if (locationType & kJointTypes) {
            metaGb.set("isJoint", trueAttr);
        }

        if (locationType == LocationType::FACESET) {
            metaGb.set("isPart", trueAttr);
        }

        metaGb.set("shutterOpen", interface.getOpArg("shutterOpen"));
        metaGb.set("shutterClose", interface.getOpArg("shutterClose"));
        metaGb.set("mbEnabled", interface.getOpArg("mbEnabled"));

        // If the motion_blur_type has been explicitly set on this object,
        // we want to make sure the motion blur is disabled if the value is static.
        // motion_blur_type normally works in rdl....except when the blur is done
        // through the node_xforms.  This handles that.
        const kodachi::StringAttribute mbType =
                interface.getAttr("moonrayStatements.motion_blur_type");
        static const kodachi::StringAttribute kStaticAttr("static");
        if (mbType == kStaticAttr) {
            // If the geometry is static, then we also want the open/close
            // to be the same for other motion related evaluations
            metaGb.set("shutterOpen", interface.getOpArg("shutterClose"));
            metaGb.set("mbEnabled", kodachi::IntAttribute(0));
        }

        metaGb.set("fps", interface.getOpArg("fps"));

        if (locationType & kAutoInstanceableTypes) {
            const kodachi::IntAttribute autoInstancingAttr =
                    interface.getOpArg("isAutoInstancingEnabled");

            if (autoInstancingAttr.getValue(true, false)) {
                const kodachi::IntAttribute geometryAutoInstancingAttr =
                        interface.getAttr("moonrayStatements.sceneBuild.autoInstancing");
                if (geometryAutoInstancingAttr.getValue(true, false)) {
                    metaGb.set("autoInstancing.enabled", trueAttr);
                }
            }
        }

        metaGb.set("kodachiType", typeAttr);

        // the only attributes that go into 'rdl2.sceneObject' are those that are
        // needed to create the scene object. Use meta to describe the object
        // for other ops
        interface.setAttr("rdl2.meta", metaGb.build(), false);

        // Other KPOPs and kodachi backends will key off this type
        static const kodachi::StringAttribute kRdl2Attr("rdl2");
        interface.setAttr("type", kRdl2Attr, false);
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace kodachi;

        OpDescriptionBuilder builder;

        builder.setSummary("Populates the 'rdl2.meta' attribute with properties inferred by the location type.");
        builder.describeInputAttr(InputAttrDescription(kTypeStringAttribute, "type"));
        builder.describeOutputAttr(OutputAttrDescription(kTypeStringAttribute, "type"));
        builder.describeOutputAttr(OutputAttrDescription(kTypeGroupAttribute, "kodachi.meta"));

        return builder.build();
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_KODACHIOP_PLUGIN(KPOPMeta)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(KPOPMeta, "KPOPMeta", 0, 1);
}

