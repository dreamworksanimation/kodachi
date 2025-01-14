// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/ZeroCopyAttribute.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

#include <kodachi_moonray/moonray_util/MoonrayUtil.h>

// scene_rdl2
#include <scene_rdl2/scene/rdl2/rdl2.h>

// rndr
#include <rendering/rndr/RenderContext.h>
#include <rendering/rndr/RenderOptions.h>

namespace
{

KdLogSetup("RDLInOp");

std::vector<float>
getMotionBlurParams(const arras::rdl2::SceneContext& sceneContext)
{
    using namespace arras;

    const auto& sceneVars = sceneContext.getSceneVariables();

    const bool mbEnabled =
            sceneVars.get(rdl2::SceneVariables::sEnableMotionBlur);

    if (mbEnabled) {
        const auto sceneVarsMotionSteps =
                sceneVars.get(rdl2::SceneVariables::sMotionSteps);
        if (sceneVarsMotionSteps.size() >= 2) {
            // rdl2 only supports 2 motion steps currently
            return { sceneVarsMotionSteps[0], sceneVarsMotionSteps[1] };
        }
    }
    return { 0.0f };
}

// returns null attribute if value is the same as the default
// value
template <class RDL_T, class ATTR_T>
kodachi::DataAttribute
getValue(const arras::rdl2::SceneObject* obj,
         const arras::rdl2::Attribute* attrPtr,
         const kodachi::array_view<float> sampleTimes,
         const size_t tupleSize = 1)
{
    using namespace arras;
    using value_t = typename ATTR_T::value_type;

    const auto attrKey = rdl2::AttributeKey<RDL_T>(*attrPtr);

    // use motion blur?
    const bool mb =
            attrKey.isBlurrable() && (sampleTimes.size() > 1);

    // get rdl value and cast rdl types to raw values
    // eg. vec3f -> 3 floats
    RDL_T val = obj->get<RDL_T>(attrKey, rdl2::TIMESTEP_BEGIN);
    if (!mb &&
            val == attrPtr->getDefaultValue<RDL_T>()) {
        // return null for default values
        return {};
    }

    const value_t* rawVal = reinterpret_cast<const value_t*>(&val);

    // single value
    if (tupleSize == 1 && !mb) {
        return ATTR_T(*rawVal);
    }

    std::vector<value_t> data;
    if (mb) {
        // blurrable
        data.reserve(tupleSize * 2);
        data.insert(data.end(), rawVal, rawVal + tupleSize);

        RDL_T valMb = obj->get<RDL_T>(attrKey, rdl2::TIMESTEP_END);
        const value_t* rawValMb = reinterpret_cast<const value_t*>(&valMb);
        data.insert(data.end(), rawValMb, rawValMb + tupleSize);

        return kodachi::ZeroCopyAttribute<ATTR_T>::create(
                sampleTimes, std::move(data), tupleSize);
    }

    // not blurrable
    data.reserve(tupleSize);
    data.insert(data.end(), rawVal, rawVal + tupleSize);
    return kodachi::ZeroCopyAttribute<ATTR_T>::create(std::move(data), tupleSize);
}

template <class RDL_T, class ATTR_T>
kodachi::DataAttribute
getVector(const arras::rdl2::SceneObject* obj,
         const arras::rdl2::Attribute* attrPtr,
         const size_t tupleSize = 1)
{
    using namespace arras;
    using value_t = typename ATTR_T::value_type;

    const auto attrKey = rdl2::AttributeKey<RDL_T>(*attrPtr);

    std::vector<value_t> data;

    RDL_T vec = obj->get<RDL_T>(attrKey, rdl2::TIMESTEP_BEGIN);

    // return null for default values
    if (vec == attrPtr->getDefaultValue<RDL_T>()) {
        return {};
    }

    data.reserve(vec.size() * tupleSize);

    const value_t* rawVal = reinterpret_cast<const value_t*>(vec.data());
    data.insert(data.end(), rawVal, rawVal + (tupleSize * vec.size()));

    return kodachi::ZeroCopyAttribute<ATTR_T>::create(std::move(data), tupleSize);
}

// specialization for bool vector
kodachi::IntAttribute
getBoolVector(const arras::rdl2::SceneObject* obj,
              const arras::rdl2::Attribute* attrPtr,
              const size_t tupleSize = 1)
{
    using namespace arras;
    using RDL_T = typename rdl2::BoolVector; // deque of bool
    using value_t = typename kodachi::IntAttribute::value_type;

    const auto attrKey = rdl2::AttributeKey<RDL_T>(*attrPtr);

    RDL_T vec = obj->get<RDL_T>(attrKey, rdl2::TIMESTEP_BEGIN);

    // return null for default values
    if (vec == attrPtr->getDefaultValue<RDL_T>()) {
        return {};
    }

    std::vector<value_t> data;
    data.reserve(vec.size() * tupleSize);
    data.insert(data.end(), vec.begin(), vec.end());

    return kodachi::ZeroCopyIntAttribute::create(std::move(data), tupleSize);
}

// specialization for string
kodachi::StringAttribute
getString(const arras::rdl2::SceneObject* obj,
          const arras::rdl2::Attribute* attrPtr,
          const size_t tupleSize = 1)
{
    using namespace arras;
    using RDL_T = typename rdl2::String;

    const auto attrKey = rdl2::AttributeKey<RDL_T>(*attrPtr);

    // strings are not blurrable
    RDL_T val = obj->get<RDL_T>(attrKey, rdl2::TIMESTEP_BEGIN);
    // return null for default values
    if (val == attrPtr->getDefaultValue<RDL_T>()) {
        return {};
    }

    return kodachi::StringAttribute(val);
}

// specialization for string vector
kodachi::StringAttribute
getStringVector(const arras::rdl2::SceneObject* obj,
                const arras::rdl2::Attribute* attrPtr,
                const size_t tupleSize = 1)
{
    using namespace arras;
    using RDL_T = typename rdl2::StringVector;
    using value_t = typename kodachi::StringAttribute::value_type;

    const auto attrKey = rdl2::AttributeKey<RDL_T>(*attrPtr);

    RDL_T vec = obj->get<RDL_T>(attrKey, rdl2::TIMESTEP_BEGIN);
    // return null for default values
    if (vec == attrPtr->getDefaultValue<RDL_T>()) {
        return {};
    }

    return kodachi::StringAttribute(vec);
}

const arras::rdl2::SceneObject*
getBinding(const arras::rdl2::SceneObject* sourceObject,
           const arras::rdl2::Attribute* attr)
{
    using namespace arras;

    try {
        switch(attr->getType()) {
        case rdl2::AttributeType::TYPE_BOOL:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Bool>(*attr));
        case rdl2::AttributeType::TYPE_INT:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Int>(*attr));
        case rdl2::AttributeType::TYPE_LONG:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Long>(*attr));
        case rdl2::AttributeType::TYPE_FLOAT:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Float>(*attr));
        case rdl2::AttributeType::TYPE_DOUBLE:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Double>(*attr));
        case rdl2::AttributeType::TYPE_STRING:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::String>(*attr));
        case rdl2::AttributeType::TYPE_RGB:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Rgb>(*attr));
        case rdl2::AttributeType::TYPE_RGBA:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Rgba>(*attr));
        case rdl2::AttributeType::TYPE_VEC2F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2f>(*attr));
        case rdl2::AttributeType::TYPE_VEC2D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2d>(*attr));
        case rdl2::AttributeType::TYPE_VEC3F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3f>(*attr));
        case rdl2::AttributeType::TYPE_VEC3D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3d>(*attr));
        case rdl2::AttributeType::TYPE_VEC4F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4f>(*attr));
        case rdl2::AttributeType::TYPE_VEC4D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4d>(*attr));
        case rdl2::AttributeType::TYPE_MAT4F:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4f>(*attr));
        case rdl2::AttributeType::TYPE_MAT4D:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4d>(*attr));
        case rdl2::AttributeType::TYPE_BOOL_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::BoolVector>(*attr));
        case rdl2::AttributeType::TYPE_INT_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::IntVector>(*attr));
        case rdl2::AttributeType::TYPE_LONG_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::LongVector>(*attr));
        case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::FloatVector>(*attr));
        case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::DoubleVector>(*attr));
        case rdl2::AttributeType::TYPE_STRING_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::StringVector>(*attr));
        case rdl2::AttributeType::TYPE_RGB_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::RgbVector>(*attr));
        case rdl2::AttributeType::TYPE_RGBA_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::RgbaVector>(*attr));
        case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2fVector>(*attr));
        case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec2dVector>(*attr));
        case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3fVector>(*attr));
        case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec3dVector>(*attr));
        case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4fVector>(*attr));
        case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Vec4dVector>(*attr));
        case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4fVector>(*attr));
        case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::Mat4dVector>(*attr));
        case rdl2::AttributeType::TYPE_SCENE_OBJECT:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::SceneObject*>(*attr));
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:
            return sourceObject->getBinding(rdl2::AttributeKey<rdl2::SceneObjectVector>(*attr));
        default:
            break;
        }
    } catch (std::exception& e) {
        KdLogWarn(" - Error getting binding for attribute: " << e.what());
    }
    return nullptr;
}

kodachi::DataAttribute
getAttribute(const arras::rdl2::SceneObject* obj,
             const arras::rdl2::Attribute* attrPtr,
             const kodachi::array_view<float> sampleTimes)
{
    using namespace arras;
    const rdl2::AttributeType attrType = attrPtr->getType();

    switch (attrType)
    {
    case rdl2::AttributeType::TYPE_BOOL:
    {
        const auto attrKey = rdl2::AttributeKey<rdl2::Bool>(*attrPtr);
        rdl2::Bool valA = obj->get<rdl2::Bool>(attrKey, rdl2::TIMESTEP_BEGIN);

        // use motion blur?
        const bool mb =
                attrKey.isBlurrable() && (sampleTimes.size() > 1);

        if (!mb) {
            // return null for default values
            if (valA == attrPtr->getDefaultValue<rdl2::Bool>()) {
                return {};
            }

            return kodachi::IntAttribute(valA);
        }

        rdl2::Bool valB = obj->get<rdl2::Bool>(attrKey, rdl2::TIMESTEP_END);

        std::vector<int> data = { valA, valB };

        return kodachi::ZeroCopyIntAttribute::create(
                sampleTimes, std::move(data));
    }
    case rdl2::AttributeType::TYPE_BOOL_VECTOR:
    {
        return getBoolVector(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_INT:
    {
        return getValue<rdl2::Int,
                    kodachi::IntAttribute>(obj, attrPtr, sampleTimes);
    }
    case rdl2::AttributeType::TYPE_INT_VECTOR:
    {
        return getVector<rdl2::IntVector,
                    kodachi::IntAttribute>(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_LONG:
    {
        return getValue<rdl2::Long,
                    kodachi::IntAttribute>(obj, attrPtr, sampleTimes);
    }
    case rdl2::AttributeType::TYPE_LONG_VECTOR:
    {
        return getVector<rdl2::LongVector,
                    kodachi::IntAttribute>(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_FLOAT:
    {
        return getValue<rdl2::Float,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes);
    }
    case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
    {
        return getVector<rdl2::FloatVector,
                    kodachi::FloatAttribute>(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_DOUBLE:
    {
        return getValue<rdl2::Double,
                    kodachi::DoubleAttribute>(obj, attrPtr, sampleTimes);
    }
    case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
    {
        return getVector<rdl2::DoubleVector,
                    kodachi::DoubleAttribute>(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_STRING:
    {
        return getString(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_STRING_VECTOR:
    {
        return getStringVector(obj, attrPtr);
    }
    case rdl2::AttributeType::TYPE_RGB:
    {
        return getValue<rdl2::Rgb,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes, 3);
    }
    case rdl2::AttributeType::TYPE_RGB_VECTOR:
    {
        return getVector<rdl2::RgbVector,
                    kodachi::FloatAttribute>(obj, attrPtr, 3);
    }
    case rdl2::AttributeType::TYPE_RGBA:
    {
        return getValue<rdl2::Rgba,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes, 4);
    }
    case rdl2::AttributeType::TYPE_RGBA_VECTOR:
    {
        return getVector<rdl2::RgbaVector,
                    kodachi::FloatAttribute>(obj, attrPtr, 4);
    }
    case rdl2::AttributeType::TYPE_VEC2F:
    {
        return getValue<rdl2::Vec2f,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes, 2);
    }
    case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
    {
        return getVector<rdl2::Vec2fVector,
                    kodachi::FloatAttribute>(obj, attrPtr, 2);
    }
    case rdl2::AttributeType::TYPE_VEC3F:
    {
        return getValue<rdl2::Vec3f,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes, 3);
    }
    case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
    {
        return getVector<rdl2::Vec3fVector,
                    kodachi::FloatAttribute>(obj, attrPtr, 3);
    }
    case rdl2::AttributeType::TYPE_VEC4F:
    {
        return getValue<rdl2::Vec4f,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes, 4);
    }
    case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
    {
        return getVector<rdl2::Vec4fVector,
                    kodachi::FloatAttribute>(obj, attrPtr, 4);
    }
    case rdl2::AttributeType::TYPE_VEC2D:
    {
        return getValue<rdl2::Vec2d,
                    kodachi::DoubleAttribute>(obj, attrPtr, sampleTimes, 2);
    }
    case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
    {
        return getVector<rdl2::Vec2dVector,
                    kodachi::DoubleAttribute>(obj, attrPtr, 2);
    }
    case rdl2::AttributeType::TYPE_VEC3D:
    {
        return getValue<rdl2::Vec3d,
                    kodachi::DoubleAttribute>(obj, attrPtr, sampleTimes, 3);
    }
    case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
    {
        return getVector<rdl2::Vec3dVector,
                    kodachi::DoubleAttribute>(obj, attrPtr, 3);
    }
    case rdl2::AttributeType::TYPE_VEC4D:
    {
        return getValue<rdl2::Vec4d,
                    kodachi::DoubleAttribute>(obj, attrPtr, sampleTimes, 4);
    }
    case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
    {
        return getVector<rdl2::Vec4dVector,
                    kodachi::DoubleAttribute>(obj, attrPtr, 4);
    }
    case rdl2::AttributeType::TYPE_MAT4F:
    {
        return getValue<rdl2::Mat4f,
                    kodachi::FloatAttribute>(obj, attrPtr, sampleTimes, 16);
    }
    case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
    {
        return getVector<rdl2::Mat4fVector,
                    kodachi::FloatAttribute>(obj, attrPtr, 16);
    }
    case rdl2::AttributeType::TYPE_MAT4D:
    {
        return getValue<rdl2::Mat4d,
                    kodachi::DoubleAttribute>(obj, attrPtr, sampleTimes, 16);
    }
    case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
    {
        return getVector<rdl2::Mat4dVector,
                    kodachi::DoubleAttribute>(obj, attrPtr, 16);
    }
    case rdl2::AttributeType::TYPE_SCENE_OBJECT:
    {
        const auto attrKey = rdl2::AttributeKey<rdl2::SceneObject*>(*attrPtr);
        rdl2::SceneObject* val = obj->get<rdl2::SceneObject*>(attrKey, rdl2::TIMESTEP_BEGIN);
        // return null for default values
        if (val == attrPtr->getDefaultValue<rdl2::SceneObject*>()) {
            return {};
        }

        if (val) {
            return kodachi::StringAttribute(val->getName());
        }
        return {};
    }
    case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:
    {
        const auto attrKey = rdl2::AttributeKey<rdl2::SceneObjectVector>(*attrPtr);
        rdl2::SceneObjectVector vec = obj->get<rdl2::SceneObjectVector>(attrKey, rdl2::TIMESTEP_BEGIN);
        // return null for default values
        if (vec == attrPtr->getDefaultValue<rdl2::SceneObjectVector>()) {
            return {};
        }

        std::vector<const char*> data;
        data.reserve(vec.size());
        for (const auto& sceneObj : vec) {
            if (sceneObj) {
                data.emplace_back(sceneObj->getName().data());
            } else {
                data.emplace_back("");
            }
        }

        return kodachi::StringAttribute(data.data(), vec.size(), 1);
    }
    case rdl2::AttributeType::TYPE_SCENE_OBJECT_INDEXABLE:
    default:
    {
        return {};
    }
    }

    return {};
}

class MoonrayMaterialBuilder
{
public:
    MoonrayMaterialBuilder() {}

    // sets an arbitrary terminal
    void setTerminal(const kodachi::string_view terminal,
                     const kodachi::StringAttribute& name)
    {
        mTerminalsBuilder.set(terminal, name);
    }

    // set the 'moonrayMaterial' terminal
    void setMaterialTerminal(const kodachi::StringAttribute& name)
    {
        static const std::string sMaterialTerminal("moonrayMaterial");
        setTerminal(sMaterialTerminal, name);
    }

    // set the 'moonrayDisplacement' terminal
    void setDisplacementTerminal(const kodachi::StringAttribute& name)
    {
        static const std::string sDisplacementTerminal("moonrayDisplacement");
        setTerminal(sDisplacementTerminal, name);
    }

    // populate the nodes group with the shader information from root
    // will also populate all shaders from the network
    void createNodeNetwork(const arras::rdl2::Shader* shader,
                           const std::vector<float>& sampleTimes)
    {
        using namespace arras;
        if (!shader) {
            return;
        }

        const kodachi::string_view shaderName = shader->getName();
        const kodachi::StringAttribute baseName(
                shaderName.substr(shaderName.rfind("/") + 1).data());

        if (baseName.getValue("", false).empty()) {
            return;
        }

        if (mCreatedNodes.find(baseName) !=
                mCreatedNodes.end()) {
            // we've already created this node
            return;
        }

        const auto& sceneClass = shader->getSceneClass();

        static const std::string sType("type");
        static const std::string sName("name");
        static const std::string sSrcName("srcName");
        static const std::string sTarget("target");
        static const std::string sParameters("parameters");
        static const std::string sConnections("connections");
        static const std::string sConnectionOut("out@");

        static const kodachi::StringAttribute sMoonray("moonray");

        kodachi::GroupBuilder nodeGb;
        nodeGb.set(sType,    kodachi::StringAttribute(sceneClass.getName()));
        nodeGb.set(sName,    baseName);
        nodeGb.set(sSrcName, baseName);
        nodeGb.set(sTarget,  sMoonray);

        kodachi::GroupBuilder paramsGb;
        kodachi::GroupBuilder connectionGb;

        // set parameters or connections
        auto attrIter = sceneClass.beginAttributes();
        const auto attrEndIter = sceneClass.endAttributes();
        for (; attrIter != attrEndIter; ++attrIter) {
            rdl2::Attribute* attrPtr = *attrIter;

            // first check if there is a binding
            const rdl2::SceneObject* binding = nullptr;
            if (attrPtr->isBindable()) {
                binding = getBinding(shader, attrPtr);
            }

            if (binding) {
                const kodachi::string_view bindingName = binding->getName();
                const kodachi::StringAttribute connectionName(
                        kodachi::concat(sConnectionOut,
                                        bindingName.substr(bindingName.rfind("/") + 1)));

                // we have a binding, set the connection
                connectionGb.set(attrPtr->getName(),
                                 connectionName);

                // check if the target is another shader;
                // if so, create the node for it recursively
                const rdl2::Shader* bindingShader =
                        binding->asA<rdl2::Shader>();
                if (bindingShader) {
                    createNodeNetwork(bindingShader, sampleTimes);
                }
            } else {
                const auto attr = getAttribute(shader, attrPtr, sampleTimes);
                if (attr.isValid()) {
                    paramsGb.set(attrPtr->getName(), attr);
                }
            }
        } // attribute loop

        if (paramsGb.isValid()) {
            nodeGb.set(sParameters, paramsGb.build());
        }

        if (connectionGb.isValid()) {
            nodeGb.set(sConnections, connectionGb.build());
        }

        mNodesBuilder.set(baseName.getValueCStr(), nodeGb.build());
        mCreatedNodes.emplace(baseName);
    }

    kodachi::GroupAttribute build(
            kodachi::GroupBuilder::BuilderBuildMode builderMode =
                    kodachi::GroupBuilder::BuildAndFlush)
    {
        static const std::string sStyle("style");
        static const std::string sTerminals("terminals");
        static const std::string sNodes("nodes");

        static const kodachi::StringAttribute sNetwork("network");

        kodachi::GroupBuilder gb;

        gb.set(sStyle, sNetwork);
        gb.set(sTerminals, mTerminalsBuilder.build());
        gb.set(sNodes, mNodesBuilder.build());

        return gb.build(builderMode);
    }

private:
    // no copy/assign
    MoonrayMaterialBuilder(const MoonrayMaterialBuilder& rhs);
    MoonrayMaterialBuilder& operator=(const MoonrayMaterialBuilder& rhs);

    kodachi::GroupBuilder mTerminalsBuilder;
    kodachi::GroupBuilder mNodesBuilder;

    std::unordered_set<kodachi::StringAttribute,
                               kodachi::AttributeHash> mCreatedNodes;
};

std::string
getLocationPath(const kodachi::string_view base)
{
    static const std::string root = "/root/world/geo";
    return kodachi::concat(root, base);
}

// RDLInOp
// Imports geometry as renderer procedural locations
// Sets up material network
class RDLInOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::StringAttribute rdlFile =
                interface.getOpArg("scene_file_input");
        if (rdlFile.isValid()) {
            const std::string rdlFileStr = rdlFile.getValue();
            if (rdlFileStr.empty()) {
                // nothing to do
                return;
            }

            if (rdlFileStr.rfind(".rdla") == std::string::npos &&
                    rdlFileStr.rfind(".rdlb") == std::string::npos) {
                KdLogWarn("Invalid rdl file.");
                return;
            }

            using namespace arras;

            arras::rdl2::SceneContext sceneContext;

            try {
                rdl2::readSceneFromFile(rdlFileStr, sceneContext);
            } catch (const std::exception& e) {
                KdLogError("Error loading rdl scene file '"
                           << rdlFileStr << "'(" << e.what() << ")");
                return;
            }

            // motion blur sample times
            std::vector<float> sampleTimes =
                    getMotionBlurParams(sceneContext);

            // Scene Objects ================================================
            auto sceneObjectIter = sceneContext.beginSceneObject();
            const auto sceneObjectEnd = sceneContext.endSceneObject();

            kodachi::StaticSceneCreateOpArgsBuilder sscb(true);
            static const std::string kType("type");

            for (; sceneObjectIter != sceneObjectEnd; ++sceneObjectIter) {
                const rdl2::SceneObject* obj = sceneObjectIter->second;

                // geometry  - create the geometry location (default to renderer procedural)
                // materials - create the material location
                //             follow the network and set terminals and nodes
                // camera    - create camera location
                // scene variables - set render settings, etc?

                const auto& sceneClass = obj->getSceneClass();

                const rdl2::Geometry* geo = obj->asA<rdl2::Geometry>();
                if (geo) {
                    static const kodachi::StringAttribute kRendererProcedural("renderer procedural");
                    static const std::string kProcedural("rendererProcedural.procedural");

                    const std::string locationPath = getLocationPath(obj->getName());

                    sscb.setAttrAtLocation(locationPath, kType, kRendererProcedural);
                    sscb.setAttrAtLocation(locationPath, kProcedural,
                            kodachi::StringAttribute(sceneClass.getName()));

                    static const std::string kArgsPath = "rendererProcedural.args";
                    // override default attributes with attribute values
                    auto attrIter = sceneClass.beginAttributes();
                    const auto attrEndIter = sceneClass.endAttributes();

                    for (; attrIter != attrEndIter; ++attrIter) {
                        rdl2::Attribute* attrPtr = *attrIter;

                        const auto attr = getAttribute(geo, attrPtr, sampleTimes);
                        // don't need to set default attrs (returned as null attr)
                        if (attr.isValid()) {
                            sscb.setAttrAtLocation(locationPath,
                                    kodachi::concat(kArgsPath, ".", attrPtr->getName()),
                                    attr);
                        }
                    }

                    // TODO: set bounds?
                }
            }

            // Material networks ======================================================
            const rdl2::Layer* layer =
                    sceneContext.getSceneVariables().getLayer()->asA<rdl2::Layer>();
            if (layer) {
                // Geometry and RootShader assignments
                // in the Layer
                rdl2::Layer::GeometryToRootShadersMap g2s;
                const_cast<rdl2::Layer*>(layer)->getAllGeometryToRootShaders(g2s);

                for (const auto& pair : g2s) {
                    const rdl2::Layer::RootShaderSet shaderSet = pair.second;
                    if (shaderSet.empty()) {
                        continue;
                    }

                    MoonrayMaterialBuilder matBuilder;
                    std::string materialLocation = "";

                    // currently we expect at most one of each type
                    const rdl2::Material* material = nullptr;
                    const rdl2::Displacement* displacement = nullptr;

                    for (const auto& shader : shaderSet) {

                        const kodachi::string_view shaderName = shader->getName();
                        const kodachi::StringAttribute baseName(
                                shaderName.substr(shaderName.rfind("/") + 1).data());

                        material = shader->asA<rdl2::Material>();
                        if (material) {
                            // try to name the material location after the material root shader
                            materialLocation = shader->getName();
                            matBuilder.setMaterialTerminal(baseName);
                        }

                        displacement = shader->asA<rdl2::Displacement>();
                        if (displacement) {
                            if (!material) {
                                // name this location after the displacement only if we've never
                                // encountered a material root shader
                                materialLocation = shader->getName();
                            }
                            matBuilder.setDisplacementTerminal(baseName);
                        }

                        const rdl2::VolumeShader* volume = shader->asA<rdl2::VolumeShader>();
                        if (volume) {
                            if (!material && !displacement) {
                                // name this location after the volume only if we've never
                                // encountered a material root shader or a displacement root shader
                                materialLocation = shader->getName();
                            }
                        }

                        matBuilder.createNodeNetwork(shader, sampleTimes);
                    } // root shader loop

                    if (!materialLocation.empty()) {
                        materialLocation = getLocationPath(materialLocation);

                        static const kodachi::StringAttribute kMaterial("material");

                        sscb.setAttrAtLocation(materialLocation, kType, kMaterial);
                        sscb.setAttrAtLocation(materialLocation,
                                kMaterial.getValueCStr(), matBuilder.build());

                        const std::string geometryLocation =
                                    getLocationPath(pair.first->getName());

                        sscb.setAttrAtLocation(geometryLocation, "materialAssign",
                                kodachi::StringAttribute(materialLocation));
                    }
                } // geometry to root shader map

            } else {
                KdLogWarn("No active layer.");
            }

            interface.execOp("StaticSceneCreate", sscb.build());
        }
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        const std::string opHelp    = "";
        const std::string opSummary = "Loads rdl data given a rdla or rdlb file.";

        kodachi::OpArgDescription arg(kodachi::kTypeStringAttribute, "scene_file_input");
        arg.setOptional(false);
        arg.setDescription("Scene file to load from.");
        builder.describeOpArg(arg);

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {

    }
};

DEFINE_KODACHIOP_PLUGIN(RDLInOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(RDLInOp, "RDLInOp", 0, 1);
}


