// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi_moonray/kodachi_geometry/GenerateUtil.h>
#include <kodachi_moonray/kodachi_geometry/KodachiGeometry.h>
#include <kodachi_moonray/kodachi_geometry/PrimitiveAttributeUtil.h>
#include <kodachi_moonray/kodachi_runtime_wrapper/KodachiRuntimeWrapper.h>

#include <arras/rendering/geom/Api.h>
#include <arras/rendering/geom/Curves.h>
#include <arras/rendering/geom/ProceduralLeaf.h>

#include "attributes.cc"

namespace {

/*

See: moonray:           rendering/geom/Curves.cc:checkPrimitiveData

[Attribute Rate Conversions]
USD           | KATANA                      | MOONRAY
------------------------------------------------------------
              | CURVES                      |
------------------------------------------------------------
face varying  | vertex                      | face varying
varying       | point                       | vertex
vertex        | vertex (interpType = subdiv)| vertex
uniform       | face                        | uniform
constant      | primitive                   | constant

[Curve Attribute Rates in Moonray]
MOONRAY
--------------------------------------------------
RATE           |  LINEAR CURVES | CUBIC CURVES
--------------------------------------------------
face varying   | cv count       | segments count
varying        | cv count       | segments count
vertex         | cv count       | cv count
uniform        | curves count   | curves count
constant       | 1              | 1

*/
arras::shading::AttributeRate
rateFunc(const kodachi::StringAttribute& scopeAttr,
         const kodachi::StringAttribute& interpAttr)
{
    static const kodachi::StringAttribute kScopePrimitiveAttr("primitive");
    static const kodachi::StringAttribute kScopeFaceAttr("face");
    static const kodachi::StringAttribute kScopePointAttr("point");
    static const kodachi::StringAttribute kScopeVertexAttr("vertex");

    static const kodachi::StringAttribute kInterpSubdivAttr("subdiv");

    arras::shading::AttributeRate rate = arras::shading::AttributeRate::RATE_UNKNOWN;
    if (scopeAttr == kScopePrimitiveAttr) {
        rate = arras::shading::AttributeRate::RATE_CONSTANT;
    } else if (scopeAttr == kScopeFaceAttr) {
        rate = arras::shading::AttributeRate::RATE_UNIFORM;
    } else if (scopeAttr == kScopePointAttr) {
        rate = arras::shading::AttributeRate::RATE_VERTEX;
    } else if (scopeAttr == kScopeVertexAttr) {
        // for curves, both facevarying and vertex rates are set to 'vertex' in katana,
        // while vertex sets an additional 'interpolationType' attribute as 'subdiv'
        if (interpAttr == kInterpSubdivAttr) {
            rate = arras::shading::AttributeRate::RATE_VERTEX;
        } else {
            rate = arras::shading::AttributeRate::RATE_FACE_VARYING;
        }
    }

    return rate;
}

using namespace arras;
using namespace arras::geom;

class KodachiCurveProcedural : public ProceduralLeaf
{
public:
    explicit KodachiCurveProcedural(const State &state)
        : ProceduralLeaf(state)
    {}

    void generate(const GenerateContext &generateContext,
            const shading::XformSamples &parent2render) override;

    void update(const UpdateContext &updateContext,
            const shading::XformSamples &parent2render) override;

private:

};

void
KodachiCurveProcedural::generate(
        const GenerateContext &generateContext,
        const shading::XformSamples &parent2render)
{
    clear();

    const kodachi_moonray::KodachiGeometry* kodachiGeometry =
            static_cast<const kodachi_moonray::KodachiGeometry*>(generateContext.getRdlGeometry());
    const rdl2::Layer *rdlLayer = generateContext.getRdlLayer();

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

    // curve type
    const kodachi::IntAttribute curveTypeAttr =
                        kodachiGeometryAttr.getChildByName("curve_type");
    if (!curveTypeAttr.isValid()) {
        kodachiGeometry->warn("Missing curve type, defaulting to Bezier.");
    }
    const int curveType = curveTypeAttr.getValue(1, false);

    Curves::Type type = Curves::Type::UNKNOWN;
    switch (curveType) {
    case 0:
        type = Curves::Type::LINEAR;
        break;
    case 1:
        type = Curves::Type::BEZIER;
        break;
    case 2:
        type = Curves::Type::BSPLINE;
        break;
    default:
        kodachiGeometry->warn("Unknown curve type, defaulting to Bezier.");
        type = Curves::Type::BEZIER;
    }

    shading::PrimitiveAttributeTable primitiveAttributeTable;

    const kodachi::FloatAttribute vertexAttr =
            kodachiGeometryAttr.getChildByName("point.P");

    const kodachi::FloatAttribute velocityAttr =
            kodachiGeometryAttr.getChildByName("point.v");

    const kodachi::GroupAttribute accelerationAttr =
            kodachiGeometryAttr.getChildByName("acceleration");

    const int64_t vertNumValues = vertexAttr.getNumberOfValues();
    const size_t vertCount = vertNumValues / 3;

    if (vertNumValues == 0) {
        kodachiGeometry->error("vertex attr is empty");
        return;
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
            if (accIndexAttr.getNumberOfValues() == vertCount) {
                acc0Valid = true;
            }
        }
    }

    const auto motionBlurData = kodachi_moonray::computeMotionBlurData(
            generateContext, static_cast<rdl2::MotionBlurType>(kodachiGeometry->get(attrMotionBlurType)),
            pos1Valid, vel0Valid, vel1Valid, acc0Valid);

    const auto& motionSteps = motionBlurData.mMotionSteps;

    // Copy vertices
    Curves::VertexBuffer vertices(vertCount, motionSteps.size());
    {
        const kodachi::GroupAttribute widthAttr =
                kodachiGeometryAttr.getChildByName("width");

        if (!widthAttr.isValid()) {
            kodachiGeometry->error("width attribute not provided");
            return;
        }

        const kodachi::FloatAttribute scaleFactorAttr =
                widthAttr.getChildByName("scaleFactor");

        float scaleFactor = scaleFactorAttr.getValue(0.5f, false);

        float constantRadius = 1.f;

        const kodachi::FloatAttribute constantWidthAttr =
                widthAttr.getChildByName("constantWidth");

        if (constantWidthAttr.isValid()) {
            constantRadius = constantWidthAttr.getValue() * scaleFactor;
        }

        const kodachi::FloatAttribute vertexWidthAttr =
                widthAttr.getChildByName("vertexWidth");

        kodachi::FloatAttribute::array_type vertexWidth;
        if (vertexWidthAttr.isValid()) {
            vertexWidth = vertexWidthAttr.getNearestSample(0.f);
        }

        for (size_t m = 0; m < motionSteps.size(); ++m) {
            const auto vertexSample = vertexAttr.getNearestSample(motionSteps[m]);
            for (size_t i = 0; i < vertCount; ++i) {
                const size_t idx = i * 3;
                float radius = vertexWidth.empty() ? constantRadius : (vertexWidth[i] * scaleFactor);
                vertices(i, m) = Vec3fa(vertexSample[idx], vertexSample[idx + 1], vertexSample[idx + 2], radius);
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

        primitiveAttributeTable.addAttribute(arras::shading::StandardAttributes::sVelocity,
                                             arras::shading::RATE_VERTEX, std::move(velocities));
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

        primitiveAttributeTable.addAttribute(arras::shading::StandardAttributes::sAcceleration,
                                             arras::shading::RATE_VERTEX, std::move(acceleration));
    }

    // number of curves
    const kodachi::IntAttribute curvesVertexCountAttr =
            kodachiGeometryAttr.getChildByName("curves_vertex_count");

    if (!curvesVertexCountAttr.isValid()) {
        kodachiGeometry->error("'curves_vertex_count' attribute not valid");
        return;
    }

    const auto curvesVertexCount = curvesVertexCountAttr.getNearestSample(0.f);
    Curves::CurvesVertexCount vertexCounts(curvesVertexCount.begin(), curvesVertexCount.end());

    // layer assignment ids
    const int id = rdlLayer->getAssignmentId(kodachiGeometry, {});
    if (id < 0) {
        // skip if there's no assignment
        return;
    }
    LayerAssignmentId layerAssignmentId(id);

    // primitive attributes
    const kodachi::GroupAttribute arbAttrs = kodachiGeometryAttr.getChildByName("arbitrary");
    // we'll process the primitive attributes
    // and try to set their types based on the requested attributes
    const arras::shading::AttributeKeySet& requestedAttributes =
            generateContext.getRequestedAttributes();

    if (arbAttrs.isValid()) {
        processArbitraryData(arbAttrs, primitiveAttributeTable, requestedAttributes,
                             motionSteps, kodachiGeometry, rateFunc);
    }

    // try to add surface_st if we hadn't already
    if (!primitiveAttributeTable.hasAttribute(shading::StandardAttributes::sSurfaceST)) {
        // Add UV coordinates if available
        const kodachi::FloatAttribute stAttr =
                kodachiGeometryAttr.getChildByName("uv_list");

        if (stAttr.isValid()) {
            if (stAttr.getNumberOfValues() / 2 == vertexCounts.size()) {
                primitiveAttributeTable.addAttribute(arras::shading::StandardAttributes::sSurfaceST,
                                                     arras::shading::RATE_UNIFORM,
                                                     kodachi_moonray::toVec2fVector(stAttr.getNearestSample(0.f)));
            } else {
                kodachiGeometry->warn("uv list is incorrect size for "
                    "uniform rate, skipping");
            }
        }
    }

    // Check the validness of the curves data and
    // print out any error messages
    std::string errorMessage;
    Primitive::DataValidness dataValid =
    Curves::checkPrimitiveData(type, vertexCounts, vertices,
            primitiveAttributeTable, &errorMessage);
    if (dataValid != Primitive::DataValidness::VALID) {
        kodachiGeometry->error(errorMessage);
        return;
    }

    std::unique_ptr<Curves> primitive = createCurves(
                type,
                std::move(vertexCounts),
                std::move(vertices),
                LayerAssignmentId(std::move(layerAssignmentId)),
                std::move(primitiveAttributeTable));

    if (primitive) {
        primitive->setCurvedMotionBlurSampleCount(kodachiGeometry->get(attrCurvedMotionBlurSampleCount));
        primitive->setMinCurveWidth(kodachiGeometry->get(attrMinCurveWidth));

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
KodachiCurveProcedural::update(
        const UpdateContext &updateContext,
        const shading::XformSamples &parent2render)
{
    // TODO: ?
}

} // anonymous namespace

RDL2_DSO_CLASS_BEGIN(KodachiCurveGeometry, kodachi_moonray::KodachiGeometry)

public:
    RDL2_DSO_DEFAULT_CTOR(KodachiCurveGeometry)

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

        return new KodachiCurveProcedural(geom::State{});
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

RDL2_DSO_CLASS_END(KodachiCurveGeometry)

