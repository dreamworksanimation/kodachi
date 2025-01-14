// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightManipulators.h"

#include "../Drawables/VAOBuilder.h"

#include <kodachi_moonray/light_util/LightUtil.h>

using namespace Foundry::Katana::ViewerUtils; // for toImathMatrix44d

namespace MoonrayKatana {

namespace {
bool hasShaderAttribute(const FnAttribute::GroupAttribute& locationAttrs,
                        const char* a0, const char* a1 = nullptr)
{
    const GroupAttribute material(locationAttrs.getChildByName("material"));
    if (not material.isValid()) return false;
    const GroupAttribute params = kodachi_moonray::light_util::getShaderParams(material);
    if (not params.isValid()) return false;
    return params.getChildByName(a0).isValid() ||
        a1 && params.getChildByName(a1).isValid();
}

}

// --------------------------------------------------------

bool
ConeAngleManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    return type == "light" &&
        hasShaderAttribute(locationAttrs, "outer_cone_angle", "inner_cone_angle");
}

void
ConeAngleManipulator::setup()
{
    const std::vector<std::string> names = { "outer_cone_angle", "inner_cone_angle" };
    initHandles("MoonrayConeAngleManipulatorHandle", names);
}

void
ConeAngleManipulatorHandle::updateLocalXform()
{
    auto manip = getBaseManipulator();

    float outerSlope1 = 60.0f, outerSlope2 = 60.0f, innerSlope = 0.0f;
    const auto params = kodachi_moonray::light_util::getShaderParams(
            manip->getValue(manip->getLastLocationPath(), "material"));
    kodachi_moonray::light_util::getSpotLightSlopes(params, outerSlope1, outerSlope2, innerSlope);

    mLensRadius = FloatAttribute(getShaderAttribute("lens_radius")).getValue(1.0f, false);

    auto worldXform = manip->getXform().data;
    double a = worldXform[8];
    double b = worldXform[9];
    double c = worldXform[10];
    double s = sqrt(a*a+b*b+c*c);
    mCoi = DoubleAttribute(manip->getValue(manip->getLastLocationPath(),
            "geometry.centerOfInterest")).getValue(20.0, false) / s;

    const float scale = mCoi * (mIndex == 0 ? outerSlope1 : innerSlope) + 1.0f;
    mMeshXform.setScale(scale * mLensRadius);
    mMeshXform[3][2] = -mCoi;

    setLocalXform(toMatrix44d(mMeshXform));
}

bool
ConeAngleManipulatorHandle::getDraggingPlane(Vec3d& origin, Vec3d& normal)
{
    RadiusManipulatorHandle::getDraggingPlane(origin, normal);
    origin = toVec3d(toImathMatrix44d(getXform()).translation());
    return true;
}

void
ConeAngleManipulatorHandle::startDrag(const Vec3d& initialPointOnPlane,
                                      const Vec2i& initialMousePosition)
{
    BaseManipulatorHandle::startDrag(initialPointOnPlane, initialMousePosition);

    // Don't allow inner cone angle to be dragged higher than outer
    auto manip = getBaseManipulator();
    if (mIndex == 1) {
        const auto nextAttr = getShaderAttribute(name(0));
        mClampMax = FloatAttribute(nextAttr).getValue(0.0, false);
    } else {
        mClampMax = 180.0f;
    }
}

double
ConeAngleManipulatorHandle::getDistanceDragged(const Vec3d& initialPointOnPlane,
                                               const Vec3d& previousPointOnPlane,
                                               const Vec3d& currentPointOnPlane,
                                               const Vec2i& initialMousePosition,
                                               const Vec2i& previousMousePosition,
                                               const Vec2i& currentMousePosition)
{
    // Distance from center of circle
    const Imath::V3d currentPoint(currentPointOnPlane.x, currentPointOnPlane.y, currentPointOnPlane.z);
    const Imath::V3d origin = toImathMatrix44d(getXform()).translation();
    const double angle = atan2((currentPoint - origin).length() - mLensRadius, mCoi) * 360.0 / M_PI;

    return angle - mInitialValue;
}

// --------------------------------------------------------

bool
RadiusManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    return type == "light" && hasShaderAttribute(locationAttrs, "radius", "lens_radius");
}

void
RadiusManipulator::setup()
{
    std::vector<std::string> names;
    if (getShaderAttribute("radius").isValid()) {
        names.push_back("radius");
    } else {
        names.push_back("lens_radius");
    }
    initHandles("MoonrayRadiusManipulatorHandle", names);
}

void
RadiusManipulatorHandle::setup(int index)
{
    BaseManipulatorHandle::setup(index);
    mAxis = Imath::Vec3<double>(1.0, 0.0, 0.0);

    mUseFixedScale = false;
    mDrawAsLines = true;
    mClampMin = 0.0f;
}

void
RadiusManipulatorHandle::generateHandleMesh()
{
    const size_t index = mHandleMeshes.size();
    mHandleMeshes.push_back(VAO());
    VAOBuilder::generateCircle(Vec3f(0.0f, 0.0f, 0.0f), 1.0f, 40, mHandleMeshes[index]);
}

void
RadiusManipulatorHandle::updateLocalXform()
{
    mMeshXform.makeIdentity();

    auto manip = getBaseManipulator();
    if (manip->isMaterialType("CylinderLight")) {
        mMeshXform.setEulerAngles(Imath::V3d(M_PI / 2, 0.0, 0.0));
    }

    const float scale = FloatAttribute(getShaderAttribute()).getValue(1.0f, false);
    mMeshXform.scale(Imath::V3f(scale, scale, scale));

    BaseManipulatorHandle::updateLocalXform();
}

bool
RadiusManipulatorHandle::getDraggingPlane(Vec3d& origin, Vec3d& normal)
{
    // Get default origin
    BaseManipulatorHandle::getDraggingPlane(origin, normal);
    // -z axis in world space for normal. multiply by local matrix stack in
    // order to account for things like CylinderLight rotation, although a
    // better way to do this would be to generate the normal based on mWSAxis
    Imath::V3d result;
    toImathMatrix44d(getXform()).multDirMatrix(Imath::V3d(0.0, 0.0, -1.0), result);
    normal = toVec3d(result);
    return true;
}

double
RadiusManipulatorHandle::getDistanceDragged(const Vec3d& initialPointOnPlane,
                                            const Vec3d& previousPointOnPlane,
                                            const Vec3d& currentPointOnPlane,
                                            const Vec2i& initialMousePosition,
                                            const Vec2i& previousMousePosition,
                                            const Vec2i& currentMousePosition)
{
    // Distance from center of circle
    const Imath::V3d pointOnPlane(currentPointOnPlane.x, currentPointOnPlane.y, currentPointOnPlane.z);
    const Imath::V3d origin = toImathMatrix44d(getManipulator()->getXform()).translation();

    return (pointOnPlane - origin).length() - mInitialValue;
}

// --------------------------------------------------------

bool
AspectRatioManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    return type == "light" && hasShaderAttribute(locationAttrs, "aspect_ratio");
}

void
AspectRatioManipulator::setup()
{
    const std::vector<std::string> names = { "aspect_ratio" };
    initHandles("MoonrayAspectRatioManipulatorHandle", names);

    ManipulatorHandleWrapperPtr manipHandleWrapper =
            Manipulator::addManipulatorHandle("MoonrayAspectRatioManipulatorHandle", "aspect_ratio2");
    BaseManipulatorHandle* handle =
            manipHandleWrapper->getPluginInstance<BaseManipulatorHandle>();
    handle->setup(getNumberOfManipulatorHandles() - 1);
}

void
AspectRatioManipulatorHandle::setup(int index)
{
    mIndex = index;
    mColor = sDefaultColor[index];
    generateHandleMesh();
    // Both handles do the same thing, but they're inverses of each other
    if (index == 0) {
        mAxis = Imath::Vec3<double>(1.0, 0.0, 0.0);
        mMeshXform.setEulerAngles(Imath::V3d(0.0, M_PI / 2.0, 0.0));
    } else {
        mAxis = Imath::Vec3<double>(0.0, 1.0, 0.0);
        mMeshXform.setEulerAngles(Imath::V3d(-M_PI / 2.0, 0.0, 0.0));
    }
}

void
AspectRatioManipulatorHandle::generateHandleMesh()
{
    const size_t index = mHandleMeshes.size();
    mHandleMeshes.push_back(VAO());
    mHandleMeshes.push_back(VAO());

    VAOBuilder::generateCylinder(
        Vec3f(0.f, 0.f, 0.f), 0.016f, 0.016f, 1.55f, mHandleMeshes[index]);

    VAOBuilder::generateCube(
        Vec3f(0.f, 0.f, 1.668f), 0.236f, mHandleMeshes[index+1]);
}

void
AspectRatioManipulatorHandle::startDrag(const Vec3d& initialPointOnPlane,
                                        const Vec2i& initialMousePosition)
{
    BaseManipulatorHandle::startDrag(initialPointOnPlane, initialMousePosition);
    mTempOffset = 0.0;
}

double
AspectRatioManipulatorHandle::getDistanceDragged(const Vec3d& initialPointOnPlane,
                                                 const Vec3d& previousPointOnPlane,
                                                 const Vec3d& currentPointOnPlane,
                                                 const Vec2i& initialMousePosition,
                                                 const Vec2i& previousMousePosition,
                                                 const Vec2i& currentMousePosition)
{
    const double distance = BaseManipulatorHandle::getDistanceDragged(
            initialPointOnPlane, previousPointOnPlane, currentPointOnPlane,
            initialMousePosition, previousMousePosition, currentMousePosition);

    const Imath::V3d previousDelta = toImathV3d(previousPointOnPlane) - toImathV3d(initialPointOnPlane);
    const double previousDistance = previousDelta.dot(mWSAxis);
    double delta = distance - previousDistance;

    if (mIndex != 0) {
        delta = -delta;
    }

    // Percent-based approach rather than fixed amounts.
    mTempOffset += (mInitialValue + mTempOffset) * delta / 2.0;

    return mTempOffset;
}

// --------------------------------------------------------

bool
ExposureManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    return type == "light" && hasShaderAttribute(locationAttrs, "exposure");
}

void
ExposureManipulator::setup()
{
    const std::vector<std::string> names = { "exposure" };
    // No 3d handle for exposure
    initHandles("", names);
}

// --------------------------------------------------------

bool
SizeManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    return type == "light" && hasShaderAttribute(locationAttrs, "width", "height");
}

void
SizeManipulator::setup()
{
    const std::vector<std::string> names = { "width", "height" };
    initHandles("MoonraySizeManipulatorHandle", names);

    {
        ManipulatorHandleWrapperPtr manipHandleWrapper =
                Manipulator::addManipulatorHandle("MoonraySizeEdgeManipulatorHandle", "widthEdgeRight");
        SizeEdgeManipulatorHandle* handle =
                manipHandleWrapper->getPluginInstance<SizeEdgeManipulatorHandle>();
        handle->setup(0);
    }

    {
        ManipulatorHandleWrapperPtr manipHandleWrapper =
                Manipulator::addManipulatorHandle("MoonraySizeEdgeManipulatorHandle", "widthEdgeLeft");
        SizeEdgeManipulatorHandle* handle =
                manipHandleWrapper->getPluginInstance<SizeEdgeManipulatorHandle>();
        handle->setup(0);
        handle->setIsLeft();
    }

    {
        ManipulatorHandleWrapperPtr manipHandleWrapper =
                Manipulator::addManipulatorHandle("MoonraySizeEdgeManipulatorHandle", "heightEdgeRight");
        SizeEdgeManipulatorHandle* handle =
                manipHandleWrapper->getPluginInstance<SizeEdgeManipulatorHandle>();
        handle->setup(1);
    }

    {
        ManipulatorHandleWrapperPtr manipHandleWrapper =
                Manipulator::addManipulatorHandle("MoonraySizeEdgeManipulatorHandle", "heightEdgeLeft");
        SizeEdgeManipulatorHandle* handle =
                manipHandleWrapper->getPluginInstance<SizeEdgeManipulatorHandle>();
        handle->setup(1);
        handle->setIsLeft();
    }
}

void
SizeManipulatorHandle::setup(int index)
{
    BaseManipulatorHandle::setup(index);
    if (index == 0) {
        // Width
        mAxis = Imath::Vec3<double>(1.0, 0.0, 0.0);
        mMeshXform.setEulerAngles(Imath::V3d(0.0, M_PI / 2.0, 0.0));
    } else {
        // Height
        mAxis = Imath::Vec3<double>(0.0, 1.0, 0.0);
        mMeshXform.setEulerAngles(Imath::V3d(-M_PI / 2.0, 0.0, 0.0));
    }
    mClampMin = 0.0f;
}

void
SizeManipulatorHandle::generateHandleMesh()
{
    const size_t index = mHandleMeshes.size();
    mHandleMeshes.push_back(VAO());
    mHandleMeshes.push_back(VAO());

    VAOBuilder::generateCylinder(
        Vec3f(0.f, 0.f, 0.f), 0.016f, 0.016f, 1.55f, mHandleMeshes[index]);

    VAOBuilder::generateCube(
        Vec3f(0.f, 0.f, 1.668f), 0.236f, mHandleMeshes[index+1]);
}

void
SizeEdgeManipulatorHandle::setup(int index)
{
    BaseManipulatorHandle::setup(index);
    mColor = sDefaultColor[2];
    if (index == 0) {
        // Width
        mAxis = Imath::Vec3<double>(1.0, 0.0, 0.0);
    } else {
        // Height
        mAxis = Imath::Vec3<double>(0.0, 1.0, 0.0);
    }

    mUseFixedScale = false;
    mDrawAsLines = true;
    mClampMin = 0.0f;
}

void
SizeEdgeManipulatorHandle::generateHandleMesh()
{
    const size_t index = mHandleMeshes.size();
    mHandleMeshes.push_back(VAO());
    auto manip = getBaseManipulator();
    if (manip->isMaterialType("CylinderLight")) {
        VAOBuilder::generateCircle(Vec3f(0.0f, 0.0f, 0.0f), 1.0f, 40, mHandleMeshes[index]);
    } else {
        static const float vertices[] = {0, -.5, 0,  0, .5, 0};
        static const unsigned indices[] = {0, 1};
        mHandleMeshes[index].setup(vertices, 2, indices, 2);
    }
}

void
SizeEdgeManipulatorHandle::updateLocalXform()
{
    auto manip = getBaseManipulator();

    // Determine how big to draw the Drawable based on another attribute
    std::string scaleAttrName;
    const bool isCylinder = manip->isMaterialType("CylinderLight");
    if (isCylinder) {
        scaleAttrName = "radius";
    } else if (mIndex == 0) {
        scaleAttrName = "height";
    } else {
        scaleAttrName = "width";
    }
    const float scale = FloatAttribute(getShaderAttribute(scaleAttrName)).getValue(1.0f, false);

    // Position handle on edge of object
    float offset = FloatAttribute(getShaderAttribute()).getValue(1.0f, false);
    if (mIsLeft) {
        offset = -offset;
    }

    mMeshXform.makeIdentity();

    if (mIndex == 0) {
        mMeshXform.translate(Imath::V3f(offset / 2.0f, 0.0f, 0.0f));
    } else {
        mMeshXform.translate(Imath::V3f(0.0f, offset / 2.0f, 0.0f));
        mMeshXform.rotate(Imath::V3d(0.0, 0.0, M_PI / 2.0));
    }
    mMeshXform.scale(Imath::V3f(scale, scale, scale));


    if (isCylinder) {
        mMeshXform.rotate(Imath::V3d(0.0, M_PI / 2.0, 0.0));
    }

    BaseManipulatorHandle::updateLocalXform();
}

void
SizeEdgeManipulatorHandle::startDrag(const Vec3d& initialPointOnPlane,
                                      const Vec2i& initialMousePosition)
{
    BaseManipulatorHandle::startDrag(initialPointOnPlane, initialMousePosition);
    prevValue = mInitialValue;
}

void
SizeEdgeManipulatorHandle::drag(const Vec3d& initialPointOnPlane,
                                 const Vec3d& previousPointOnPlane,
                                 const Vec3d& currentPointOnPlane,
                                 const Vec2i& initialMousePosition,
                                 const Vec2i& previousMousePosition,
                                 const Vec2i& currentMousePosition,
                                 bool isFinal)
{
    double distance = getDistanceDragged(initialPointOnPlane,
                                         previousPointOnPlane,
                                         currentPointOnPlane,
                                         initialMousePosition,
                                         previousMousePosition,
                                         currentMousePosition);
    if (mIsLeft)
        distance = -distance;

    auto manip = getBaseManipulator();

    // Convert to local scale
    DoubleAttribute scaleAttr(
        manip->getValue(manip->getLastLocationPath(), "xform.interactive.scale"));
    auto&& scale(scaleAttr.getNearestSample(0));
    const double axisScale = (mIndex == 0 ? scale[0] : scale[1]);
    distance /= axisScale;

    // compute the new width/height
    float value = std::max((float)(mInitialValue + distance), 0.0f);
    const float rounded = std::round(value);
    if (std::abs(value - rounded) <= sSnapToIntDelta) value = rounded;

    // Set new width/height
    setShaderAttribute(FnAttribute::FloatAttribute(value), isFinal);

    // Figure out how much to move the translation
    distance = value - prevValue; prevValue = value;
    Imath::V3d offset = mWSAxis * (distance / 2.0f * axisScale);
    if (mIsLeft) offset = -offset;

    // Adjust translation of all the lights
    std::vector<std::string> paths;
    manip->getLocationPaths(paths);
    for (const auto& path : paths) {
        DoubleAttribute translateAttr(
            manip->getValue(path, "xform.interactive.translate"));
        auto&& translate = translateAttr.getNearestSample(0);
        const double newTranslate[3] = {
            translate[0] + offset.x,
            translate[1] + offset.y,
            translate[2] + offset.z
        };
        manip->setValue(path, "xform.interactive.translate", DoubleAttribute(newTranslate, 3, 1), isFinal);
    }
}

void
SizeEdgeManipulatorHandle::cancelManipulation()
{
    if (isDragging()) {
        setShaderAttribute(FnAttribute::FloatAttribute(mInitialValue), false);
        // Restore of translate nyi
    }
}

}

