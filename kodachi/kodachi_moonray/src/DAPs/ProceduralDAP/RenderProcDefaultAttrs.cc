// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnGroupBuilder.h>
#include <FnDefaultAttributeProducer/plugin/FnDefaultAttributeProducerPlugin.h>
#include <FnDefaultAttributeProducer/plugin/FnDefaultAttributeProducerUtil.h>
#include <FnPluginSystem/FnPlugin.h>
#include <FnPluginManager/FnPluginManager.h>
#include <FnAsset/FnDefaultAssetPlugin.h>

#include <scene_rdl2/scene/rdl2/SceneContext.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>

namespace // anonymous
{

//----------------------------------------------------------

inline
static void
printDAPError(const std::string& msg)
{
    std::cerr << "RenderProcDefaultAttrs:ERROR " << msg << '\n';
}

//----------------------------------------------------------

namespace WidgetType
{

    static const std::string sBoolean            = "boolean";
    static const std::string sColor              = "color";
    static const std::string sCheckBox           = "checkBox";
    static const std::string sScenegraphLoc      = "scenegraphLocation";
    static const std::string sScenegraphLocArray = "scenegraphLocationArray";
    static const std::string sArray              = "array";
    static const std::string sDynamicArray       = "dynamicArray";
    static const std::string sSortableArray      = "sortableArray";

} // namespace WidgetType

std::pair<FnAttribute::Attribute, FnAttribute::GroupAttribute>
rdl2AttrToKatanaAttr(const arras::rdl2::Attribute* attrPtr)
{
    //--------------------------

    using namespace arras;

    using KatInt_t    = FnAttribute::IntAttribute::value_type;
    using KatFloat_t  = FnAttribute::FloatAttribute::value_type;
    using KatDouble_t = FnAttribute::DoubleAttribute::value_type;
    using KatString_t = FnAttribute::StringAttribute::value_type;

    static const FnAttribute::GroupAttribute kResizableArrayHintsAttr(
            "widget", FnAttribute::StringAttribute("array"),
            "resize", FnAttribute::IntAttribute(true), false);

    static const FnAttribute::GroupAttribute kResizableNumberArrayHintsAttr(
            "widget", FnAttribute::StringAttribute("numberArray"),
            "resize", FnAttribute::IntAttribute(true), false);

    //--------------------------

    const arras::rdl2::AttributeType attrType = attrPtr->getType();

    switch (attrType)
    {
        case rdl2::AttributeType::TYPE_BOOL:
            {
                const rdl2::AttributeKey<rdl2::Bool> attrKey(*attrPtr);
                if (attrKey == rdl2::Geometry::sStaticKey) {
                    return {
                        FnAttribute::NullAttribute{},
                        FnAttribute::GroupAttribute{}
                    };
                }

                return { FnAttribute::IntAttribute(attrPtr->getDefaultValue<rdl2::Bool>()),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sCheckBox)).build()
                       };
            }
        case rdl2::AttributeType::TYPE_BOOL_VECTOR:
            {
                // Not an actual std::vector, but a std::deque<bool>
                const rdl2::BoolVector& vect = attrPtr->getDefaultValue<rdl2::BoolVector>();
                const std::vector<KatInt_t> intVec(vect.begin(), vect.end());

                return { FnAttribute::IntAttribute(intVec.data(), intVec.size(), 1),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_INT:
            {
                return { FnAttribute::IntAttribute(attrPtr->getDefaultValue<rdl2::Int>()),
                         FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_INT_VECTOR:
            {
                const rdl2::IntVector& vect = attrPtr->getDefaultValue<rdl2::IntVector>();

                return { FnAttribute::IntAttribute(vect.data(), vect.size(), 1),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_LONG:
            {
                const KatInt_t val =
                        static_cast<KatInt_t>(
                                attrPtr->getDefaultValue<rdl2::Long>());
                return { FnAttribute::IntAttribute(val), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_LONG_VECTOR:
            {
                const rdl2::LongVector& vect = attrPtr->getDefaultValue<rdl2::LongVector>();

                std::vector<KatInt_t> intVec(vect.size());
                for (rdl2::Long e : vect) {
                    intVec.push_back(static_cast<KatInt_t>(e));
                }

                return { FnAttribute::IntAttribute(intVec.data(), intVec.size(), 1),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_FLOAT:
            {
                return { FnAttribute::FloatAttribute(attrPtr->getDefaultValue<rdl2::Float>()),
                         FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_FLOAT_VECTOR:
            {
                const rdl2::FloatVector& vect = attrPtr->getDefaultValue<rdl2::FloatVector>();

                return { FnAttribute::FloatAttribute(vect.data(), vect.size(), 1),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_DOUBLE:
            {
                return { FnAttribute::DoubleAttribute(attrPtr->getDefaultValue<rdl2::Double>()),
                         FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_DOUBLE_VECTOR:
            {
                const rdl2::DoubleVector& vect = attrPtr->getDefaultValue<rdl2::DoubleVector>();

                return { FnAttribute::DoubleAttribute(vect.data(), vect.size(), 1),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_STRING:
            {
                return { FnAttribute::StringAttribute(attrPtr->getDefaultValue<rdl2::String>()),
                         FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_STRING_VECTOR:
            {
                const rdl2::StringVector& vect = attrPtr->getDefaultValue<rdl2::StringVector>();

                return { FnAttribute::StringAttribute(vect.data(), vect.size(), 1),
                         kResizableArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_RGB:
            {
                const rdl2::Rgb& rgb =
                        attrPtr->getDefaultValue<rdl2::Rgb>();

                return { FnAttribute::FloatAttribute(&rgb.r, 3, 3),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sColor)).build() };
            }
        case rdl2::AttributeType::TYPE_RGB_VECTOR:
            {
                const rdl2::RgbVector& colorVect = attrPtr->getDefaultValue<rdl2::RgbVector>();

                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(colorVect.data()),
                                colorVect.size() * 3,
                                3),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sDynamicArray))
                                .set("panelWidget", FnAttribute::StringAttribute(WidgetType::sColor)).build() };
            }
        case rdl2::AttributeType::TYPE_RGBA:
            {
                const rdl2::Rgba& rgba = attrPtr->getDefaultValue<rdl2::Rgba>();
                return { FnAttribute::FloatAttribute(&rgba.r, 4, 4),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sColor)).build()
                       };
            }
        case rdl2::AttributeType::TYPE_RGBA_VECTOR:
            {
                const rdl2::RgbaVector& colorVect = attrPtr->getDefaultValue<rdl2::RgbaVector>();

                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(colorVect.data()),
                                colorVect.size() * 4,
                                4),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sDynamicArray))
                                .set("panelWidget", FnAttribute::StringAttribute(WidgetType::sColor)).build() };
            }
        case rdl2::AttributeType::TYPE_VEC2F:
            {
                const rdl2::Vec2f& vec2f =
                        attrPtr->getDefaultValue<rdl2::Vec2f>();
                return { FnAttribute::FloatAttribute(&vec2f.x, 2, 2), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_VEC2F_VECTOR:
            {
                const rdl2::Vec2fVector& vec2fvec = attrPtr->getDefaultValue<rdl2::Vec2fVector>();

                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(vec2fvec.data()),
                                vec2fvec.size() * 2,
                                2),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_VEC3F:
            {
                const rdl2::Vec3f& vec3f =
                        attrPtr->getDefaultValue<rdl2::Vec3f>();
                return { FnAttribute::FloatAttribute(&vec3f.x, 3, 3), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_VEC3F_VECTOR:
            {
                const rdl2::Vec3fVector& vec3fvec = attrPtr->getDefaultValue<rdl2::Vec3fVector>();

                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(vec3fvec.data()),
                                vec3fvec.size() * 3,
                                3),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_VEC4F:
            {
                const rdl2::Vec4f& vec4f =
                        attrPtr->getDefaultValue<rdl2::Vec4f>();
                return { FnAttribute::FloatAttribute(&vec4f.x, 4, 4), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_VEC4F_VECTOR:
            {
                const rdl2::Vec4fVector& vec4fvec = attrPtr->getDefaultValue<rdl2::Vec4fVector>();

                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(vec4fvec.data()),
                                vec4fvec.size() * 4,
                                4),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_VEC2D:
            {
                const rdl2::Vec2d& vec2d =
                        attrPtr->getDefaultValue<rdl2::Vec2d>();
                return { FnAttribute::DoubleAttribute(&vec2d.x, 2, 2), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_VEC2D_VECTOR:
            {
                const rdl2::Vec2dVector& vec2dvec = attrPtr->getDefaultValue<rdl2::Vec2dVector>();

                return { FnAttribute::DoubleAttribute(
                                reinterpret_cast<const KatDouble_t*>(vec2dvec.data()),
                                vec2dvec.size() * 2,
                                2),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_VEC3D:
            {
                const rdl2::Vec3d& vec3d = attrPtr->getDefaultValue<rdl2::Vec3d>();
                return { FnAttribute::DoubleAttribute(&vec3d.x, 3, 3), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_VEC3D_VECTOR:
            {
                const rdl2::Vec3dVector& vec3dvec = attrPtr->getDefaultValue<rdl2::Vec3dVector>();

                return { FnAttribute::DoubleAttribute(
                                reinterpret_cast<const KatDouble_t*>(vec3dvec.data()),
                                vec3dvec.size() * 3,
                                3),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_VEC4D:
            {
                const rdl2::Vec4d& vec4d = attrPtr->getDefaultValue<rdl2::Vec4d>();
                return { FnAttribute::DoubleAttribute(&vec4d.x, 4, 4), FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_VEC4D_VECTOR:
            {
                const rdl2::Vec4dVector& vec4dvec = attrPtr->getDefaultValue<rdl2::Vec4dVector>();

                return { FnAttribute::DoubleAttribute(
                                reinterpret_cast<const KatDouble_t*>(vec4dvec.data()),
                                vec4dvec.size() * 4,
                                4),
                         kResizableNumberArrayHintsAttr };
            }
        case rdl2::AttributeType::TYPE_MAT4F:
            {
                const rdl2::Mat4f& mat44f = attrPtr->getDefaultValue<rdl2::Mat4f>();
                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(&mat44f), 16, 16),
                         FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_MAT4F_VECTOR:
            {
                const rdl2::Mat4fVector& mat44fVec = attrPtr->getDefaultValue<rdl2::Mat4fVector>();

                return { FnAttribute::FloatAttribute(
                                reinterpret_cast<const KatFloat_t*>(mat44fVec.data()),
                                mat44fVec.size() * 16,
                                16),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sDynamicArray))
                                .set("tupleGroupSize", FnAttribute::IntAttribute(4))
                                .set("tupleSize", FnAttribute::IntAttribute(4))
                                .set("panelWidget", FnAttribute::StringAttribute(WidgetType::sArray)).build() };
            }
        case rdl2::AttributeType::TYPE_MAT4D:
            {
                const rdl2::Mat4d& mat44d = attrPtr->getDefaultValue<rdl2::Mat4d>();
                return { FnAttribute::DoubleAttribute(
                                reinterpret_cast<const KatDouble_t*>(&mat44d), 16, 16),
                         FnAttribute::GroupAttribute() };
            }
        case rdl2::AttributeType::TYPE_MAT4D_VECTOR:
            {
                const rdl2::Mat4dVector& mat44dVec = attrPtr->getDefaultValue<rdl2::Mat4dVector>();

                return { FnAttribute::DoubleAttribute(
                                reinterpret_cast<const KatDouble_t*>(mat44dVec.data()),
                                mat44dVec.size() * 16,
                                16),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sDynamicArray))
                                .set("tupleGroupSize", FnAttribute::IntAttribute(4))
                                .set("tupleSize", FnAttribute::IntAttribute(4))
                                .set("panelWidget", FnAttribute::StringAttribute(WidgetType::sArray)).build() };
            }
        case rdl2::AttributeType::TYPE_SCENE_OBJECT:
            {
                return { FnAttribute::StringAttribute(""),
                         FnAttribute::GroupBuilder()
                               .set("widget", FnAttribute::StringAttribute(WidgetType::sScenegraphLoc)).build()
                       };
            }
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_VECTOR:
            {
                return { FnAttribute::StringAttribute(static_cast<std::string*>(nullptr), 0, 1),
                         FnAttribute::GroupBuilder()
                               .set("widget", FnAttribute::StringAttribute(WidgetType::sSortableArray))
                               .build()
                       };
            }
        case rdl2::AttributeType::TYPE_SCENE_OBJECT_INDEXABLE:
            {

                return { FnAttribute::StringAttribute(""),
                         FnAttribute::GroupBuilder()
                                .set("widget", FnAttribute::StringAttribute(WidgetType::sScenegraphLoc)).build()
                       };
            }
        default:
            {
                break;
            }
    }

    return { FnAttribute::Attribute(), FnAttribute::GroupAttribute() };
}

//----------------------------------------------------------

class RenderProcDefaultAttrs : public Foundry::Katana::DefaultAttributeProducer
{
public:
    static FnPlugStatus setHost(FnPluginHost* host)
    {
        Foundry::Katana::DefaultAttributeProducer::setHost(host);
        return Foundry::Katana::DefaultAssetPlugin::setHost(host);
    }

    static FnAttribute::GroupAttribute cook(const Foundry::Katana::GeolibCookInterface& interface,
                                            const std::string& attrRoot,
                                            const std::string& inputLocationPath,
                                            std::int32_t inputIndex)
    {
        static const FnAttribute::StringAttribute kRendererProcedural("renderer procedural");
        static const FnAttribute::StringAttribute kRendererProceduralArguments("renderer procedural arguments");

        FnAttribute::GroupBuilder groupBuilder;

        FnAttribute::StringAttribute locationType = interface.getAttr("type");

        if (locationType == kRendererProcedural ||
                locationType == kRendererProceduralArguments) {
            const FnAttribute::StringAttribute proceduralTypeAttr = interface.getAttr("rendererProcedural.procedural");
            if (proceduralTypeAttr.isValid()) {

                std::string dsoNameOrPath = proceduralTypeAttr.getValue();
                if (dsoNameOrPath.empty()) {
                    printDAPError("Procedural name missing (\"rendererProcedural.procedural\" is empty)");
                    return groupBuilder.build();
                }

                // Since the "renderer procedural attribute" is defined as an assetIdInput we need to resolve
                // against the current asset plugin
                dsoNameOrPath = FnKat::DefaultAssetPlugin::resolvePath(dsoNameOrPath, 0);

                // If file extension ".so" exists, remove it
                const std::size_t fileExtensionIdx = dsoNameOrPath.find(".so");
                if (fileExtensionIdx != std::string::npos) {
                    dsoNameOrPath.erase(fileExtensionIdx, 3);
                }

                arras::rdl2::SceneContext sceneContext;

                //--------------------------
                // Check if "rendererProcedural.procedural" attribute contains a path to a *.so

                std::string className;
                if (dsoNameOrPath[0] == '/') {
                    std::string newDsoPath;

                    // 1) extract and remove the file name from end of the string (e.g. "AbcGeometry.so"),
                    //    the rest of the string should be a valid path to the directory containing the *.so file
                    const auto r_iter = std::find(dsoNameOrPath.rbegin(), dsoNameOrPath.rend(), '/');
                    newDsoPath = std::string(dsoNameOrPath.begin(), r_iter.base() - 1);
                    className  = std::string(r_iter.base(), dsoNameOrPath.end());

                    // 2) get the list of known DSO paths using rdl2::SceneContext::getDsoPath()
                    std::string currentDsoPath = sceneContext.getDsoPath();

                    // 3) prepend path from (1) to list of paths from (2), if path not already
                    //    included:
                    {
                        // Check if the full path (exact string) exists in the current DSO path list;
                        // meaning, if it can be found in the list, and it's either at the end of the
                        // list, OR, it is immediately followed by a ':' character.
                        // Example:
                        //      newDsoPath == "/usr/local/dso_folder"
                        //      currentDsoPath == "/usr/local/dso_folder_a:/usr/local/dso_folder_b:/usr/local/dso_folder_c"
                        //      Therefore relying on currentDsoPath.find(newDsoPath) is misleading
                        //
                        const std::size_t startStrIdx = currentDsoPath.find(newDsoPath);
                        if (startStrIdx != std::string::npos) {
                            const std::size_t endStrIdx = startStrIdx + newDsoPath.size();

                            // Prepend if exact path not found
                            if (endStrIdx != currentDsoPath.size() && currentDsoPath[endStrIdx] != ':') {
                                currentDsoPath.insert(0, newDsoPath + ":");
                            }
                        }
                    }

                    // 4) pass (3) to rdl2::SceneContext::setDsoPath()
                    sceneContext.setDsoPath(currentDsoPath);
                }
                else {
                    className = dsoNameOrPath;
                }

                //--------------------------

                arras::rdl2::SceneClass* sceneClassPtr = sceneContext.createSceneClass(className);

                if (sceneClassPtr != nullptr) {
                    //--------------------------

                    using namespace arras;

                    using KatInt_t    = FnAttribute::IntAttribute::value_type;
                    using KatFloat_t  = FnAttribute::FloatAttribute::value_type;
                    using KatDouble_t = FnAttribute::DoubleAttribute::value_type;
                    using KatString_t = FnAttribute::StringAttribute::value_type;

                    //--------------------------

                    auto attrIter = sceneClassPtr->beginAttributes();
                    const auto endIter = sceneClassPtr->endAttributes();

                    for (; attrIter != endIter; ++attrIter) {

                        rdl2::Attribute* attrPtr = *attrIter;

                        const auto katAttrWidgetPair = rdl2AttrToKatanaAttr(attrPtr);

                        const FnAttribute::Attribute&      katanaAttr = katAttrWidgetPair.first;
                        const FnAttribute::GroupAttribute& hintsGroup = katAttrWidgetPair.second;

                        // Skip any NullAttributes
                        if (katanaAttr.getType() == kFnKatAttributeTypeNull) {
                            continue;
                        }

                        const std::string& attrName = attrPtr->getName();

                        if (!katanaAttr.isValid()) {
                            printDAPError("Renderer Procedural Default Attribute Producer: invalid attribute \""
                                       + attrName
                                       + "\" encountered, skip to next attribute.");
                            continue;
                        }

                        const std::string attrPath = "rendererProcedural.args." + attrName;

                        groupBuilder.set(attrPath, katanaAttr);
                        if (hintsGroup.isValid()) {
                            FnKat::DapUtil::SetAttrHints(groupBuilder, attrPath, hintsGroup);
                        }
                    }
                }
            }
        }

        return groupBuilder.build();
    }
};

DEFINE_DEFAULTATTRIBUTEPRODUCER_PLUGIN(RenderProcDefaultAttrs)

} // namespace anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(RenderProcDefaultAttrs, "RenderProcDefaultAttrs", 0, 1);
}

