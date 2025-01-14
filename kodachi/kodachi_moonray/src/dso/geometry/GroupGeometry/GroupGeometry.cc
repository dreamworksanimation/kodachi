// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "attributes.cc"
#include <arras/rendering/geom/Api.h>
#include <arras/rendering/geom/ProceduralLeaf.h>
#include <arras/rendering/geom/Types.h>
#include <arras/rendering/shading/Shading.h>
#include <scene_rdl2/scene/rdl2/rdl2.h>

namespace {

inline arras::geom::Mat43
toMat43(const rdl2::Mat4d& m)
{
    return { static_cast<float>(m.vx.x), static_cast<float>(m.vx.y), static_cast<float>(m.vx.z),
             static_cast<float>(m.vy.x), static_cast<float>(m.vy.y), static_cast<float>(m.vy.z),
             static_cast<float>(m.vz.x), static_cast<float>(m.vz.y), static_cast<float>(m.vz.z),
             static_cast<float>(m.vw.x), static_cast<float>(m.vw.y), static_cast<float>(m.vw.z) };
}

using namespace arras;
using namespace arras::geom;
using namespace arras::shading;

class GroupProcedural : public ProceduralLeaf
{
public:
    // constructor can be freely extended but should always pass in State to
    // construct base Procedural class
    explicit GroupProcedural(const arras::geom::State& state) :
        ProceduralLeaf(state) {}

    void generate(const GenerateContext& generateContext,
            const shading::XformSamples& parent2render)
    {
        const rdl2::Geometry* rdlGeometry = generateContext.getRdlGeometry();
        const auto& referenceGeometries =
                rdlGeometry->get(rdl2::Geometry::sReferenceGeometries);
        if (referenceGeometries.empty()) {
            rdlGeometry->error("Did not find any reference geometry. "
                "Please make sure the \"references\" field contains "
                "at least one source reference geometry");
            return;
        }

        const size_t numInstances = referenceGeometries.size();

        // primitive attributes
        // Instances can have their own CONSTANT rate primitive attributes.
        // UserData doesn't currently support AttributeRate,
        // So assume single values should be applied to each instance,
        // and multiple values should map to individual instances.
        std::vector<PrimitiveAttributeTable> primAttrTables(numInstances);

        const auto& primitiveAttributes = rdlGeometry->get(attrPrimitiveAttributes);
        for (const auto& sceneObject : primitiveAttributes) {
            if (const auto userData = sceneObject->asA<rdl2::UserData>()) {
                if (userData->hasBoolData()) {
                    TypedAttributeKey<rdl2::Bool> key(userData->getBoolKey());
                    const auto& boolValues = userData->getBoolValues();

                    if (boolValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Bool>{boolValues[0]});
                        }
                    } else if (boolValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Bool>{boolValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of boolValues");
                    }
                }

                if (userData->hasIntData()) {
                    TypedAttributeKey<rdl2::Int> key(userData->getIntKey());
                    const auto& intValues = userData->getIntValues();

                    if (intValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Int>{intValues[0]});
                        }
                    } else if (intValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Int>{intValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of intValues");
                    }
                }

                if (userData->hasFloatData()) {
                    TypedAttributeKey<rdl2::Float> key(userData->getFloatKey());
                    const auto& floatValues = userData->getFloatValues();

                    if (floatValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Float>{floatValues[0]});
                        }
                    } else if (floatValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Float>{floatValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of floatValues");
                    }
                }

                if (userData->hasStringData()) {
                    TypedAttributeKey<rdl2::String> key(userData->getStringKey());
                    const auto& stringValues = userData->getStringValues();

                    if (stringValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::String>{stringValues[0]});
                        }
                    } else if (stringValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::String>{stringValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of stringValues");
                    }
                }

                if (userData->hasColorData()) {
                    TypedAttributeKey<rdl2::Rgb> key(userData->getColorKey());
                    const auto& colorValues = userData->getColorValues();

                    if (colorValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Rgb>{colorValues[0]});
                        }
                    } else if (colorValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Rgb>{colorValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of colorValues");
                    }
                }

                if (userData->hasVec2fData()) {
                    TypedAttributeKey<rdl2::Vec2f> key(userData->getVec2fKey());
                    const auto& vec2fValues = userData->getVec2fValues();

                    if (vec2fValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Vec2f>{vec2fValues[0]});
                        }
                    } else if (vec2fValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Vec2f>{vec2fValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of vec2fValues");
                    }
                }

                if (userData->hasVec3fData()) {
                    TypedAttributeKey<rdl2::Vec3f> key(userData->getVec3fKey());
                    const auto& vec3fValues = userData->getVec3fValues();

                    if (vec3fValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Vec3f>{vec3fValues[0]});
                        }
                    } else if (vec3fValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Vec3f>{vec3fValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of vec3fValues");
                    }
                }

                if (userData->hasMat4fData()) {
                    TypedAttributeKey<rdl2::Mat4f> key(userData->getMat4fKey());
                    const auto& mat4fValues = userData->getMat4fValues();

                    if (mat4fValues.size() == 1) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Mat4f>{mat4fValues[0]});
                        }
                    } else if (mat4fValues.size() == numInstances) {
                        for (size_t i = 0; i < numInstances; ++i) {
                            primAttrTables[i].addAttribute(key,
                                    AttributeRate::RATE_CONSTANT,
                                    std::vector<rdl2::Mat4f>{mat4fValues[i]});
                        }
                    } else {
                        rdlGeometry->warn("primitive attribute UserData'",
                                userData->getName() ,
                                "'contains invalid number of vec3fValues");
                    }
                }
            }
        }

        reservePrimitive(numInstances);

        for (size_t i = 0; i < numInstances; ++i) {
            if (rdl2::Geometry* geom = referenceGeometries[i]->asA<rdl2::Geometry>()) {
                const std::shared_ptr<SharedPrimitive>& ref =
                        geom->getProcedural()->getReference();

                std::unique_ptr<Instance> instance;
                {
                    // If xform is blurred then add it to the instance
                    const rdl2::Mat4d xformTSBegin =
                            geom->get(rdl2::Geometry::sNodeXformKey,
                                      rdl2::AttributeTimestep::TIMESTEP_BEGIN);

                    const rdl2::Mat4d xformTSEnd =
                            geom->get(rdl2::Geometry::sNodeXformKey,
                                      rdl2::AttributeTimestep::TIMESTEP_END);

                    if (xformTSBegin == xformTSEnd) {
                        instance = createInstance(toMat43(xformTSBegin), ref,
                                std::move(primAttrTables[i]));
                    } else {
                        instance = createInstance(
                                {toMat43(xformTSBegin), toMat43(xformTSEnd)},
                                ref, std::move(primAttrTables[i]));
                    }
                }

                addPrimitive(std::move(instance),
                        generateContext.getMotionBlurParams(), parent2render);
            }
        }
    }

    void update(const UpdateContext& updateContext,
            const shading::XformSamples& parent2render)
    {
        // For realtime rendering usages. Feature film shader development
        // is not required to implement this method.

        // Implement this method to update primitives created from
        // generate call
    }
};

} // anonymous namespace

RDL2_DSO_CLASS_BEGIN(GroupGeometry, rdl2::Geometry)

public:
    RDL2_DSO_DEFAULT_CTOR(GroupGeometry)
    geom::Procedural* createProcedural() const;
    void destroyProcedural() const;
    bool deformed() const;
    void resetDeformed();

RDL2_DSO_CLASS_END(GroupGeometry)

geom::Procedural* GroupGeometry::createProcedural() const
{
    geom::State state;
    return new GroupProcedural(state);
}

void GroupGeometry::destroyProcedural() const
{
    delete mProcedural;
}

bool GroupGeometry::deformed() const
{
    return mProcedural->deformed();
}

void GroupGeometry::resetDeformed()
{
    mProcedural->resetDeformed();
}

