// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "BaseManipulator.h"

#include <FnViewer/plugin/FnPickingTypes.h>
#include <FnViewer/utils/FnImathHelpers.h>

#include <FTGL/ftgl.h>
#include <OpenEXR/ImathMatrixAlgo.h>

#include <sstream>

#include <kodachi_moonray/light_util/LightUtil.h>

namespace {
static const double kScreenSpacing = 10.0;
static const double kBoxMargin = 4.0;
}

using namespace Foundry::Katana::ViewerUtils; // for toImathMatrix44d

namespace MoonrayKatana {

FnAttribute::GroupAttribute
BaseManipulator::tags(const char* name, const char* shortcut, const char* group)
{
    FnAttribute::GroupBuilder gb;
    gb.set(kTagName, FnAttribute::StringAttribute(name));
    gb.set(kTagGroup, FnAttribute::StringAttribute(group ? group : "Light"));
    gb.set(kTagAlwaysAvailable, FnAttribute::IntAttribute(0));
    gb.set(kTagExclusiveInGroup, FnAttribute::IntAttribute(1));
    if (shortcut) gb.set(kTagShortcut, FnAttribute::StringAttribute(shortcut));
    gb.set(kTagTechnology, FnAttribute::StringAttribute(kTechnology));
    return gb.build();
}

float BaseManipulatorHandle::sSnapToIntDelta = 0.1f;

std::unique_ptr<FTPixmapFont> MoonrayLabelManipulatorHandle::sFont;
std::array<std::array<float, 4>, 4> BaseManipulatorHandle::sDefaultColor {{
        { 0.88f, 0.00f, 0.11f, 1.0f },
        { 0.00f, 0.84f, 0.39f, 1.0f },
        { 0.29f, 0.56f, 0.89f, 1.0f },
        { 0.88f, 0.83f, 0.15f, 1.0f }
}};

void
BaseManipulator::initHandles(const std::string& handleClassName,
                             const std::vector<std::string>& handleNames,
                             bool includeLabelHandles)
{
    if (!handleClassName.empty()) {
        for (size_t i = 0; i < handleNames.size(); ++i) {
            ManipulatorHandleWrapperPtr manipHandleWrapper =
                    Manipulator::addManipulatorHandle(handleClassName, handleNames[i]);
            BaseManipulatorHandle* handle =
                    manipHandleWrapper->getPluginInstance<BaseManipulatorHandle>();
            handle->setup(i);
        }
    }

    if (includeLabelHandles) {
        for (size_t i = 0; i < handleNames.size(); ++i) {
            const std::string labelName = handleClassName.empty() ? handleNames[i] : (handleNames[i] + "Label");
            ManipulatorHandleWrapperPtr manipHandleWrapper =
                    Manipulator::addManipulatorHandle("MoonrayLabelManipulatorHandle", labelName);
            MoonrayLabelManipulatorHandle* labelHandle =
                    manipHandleWrapper->getPluginInstance<MoonrayLabelManipulatorHandle>();
            labelHandle->setup(i);
        }
    }
}

std::string
BaseManipulator::getLastLocationPath()
{
    std::vector<std::string> locations;
    getLocationPaths(locations);
    if (locations.empty()) {
        return std::string();
    }
    return locations.back();
}

void
BaseManipulator::draw()
{
    // Draw the manipulator at the location of the last-selected filter
    setXform(getViewport()->getViewerDelegate()->getWorldXform(getLastLocationPath()));
    GLManipulator::draw();
}

void
BaseManipulator::pickerDraw(int64_t pickerId)
{
    // Draw the manipulator at the location of the last-selected filter
    setXform(getViewport()->getViewerDelegate()->getWorldXform(getLastLocationPath()));
    GLManipulator::pickerDraw(pickerId);
}

void
BaseManipulator::setOption(
    OptionIdGenerator::value_type optionId,
    Attribute attr)
{
    GLManipulator::setOption(optionId, attr);

    static const OptionIdGenerator::value_type sGlobalScaleId =
            OptionIdGenerator::GenerateId("Manipulator.Scale");

    if (optionId == sGlobalScaleId) {
        // For safety, all the multipliers will be ranged in [0.01, 10.0].
        double value = DoubleAttribute(attr).getValue(1, false);
        value = std::max(0.01, std::min(10.0, value));
        if (mGlobalScale != value) {
            mGlobalScale = value;
            getViewport()->setDirty(true);
        }
    }
}

Attribute
BaseManipulator::getOption(OptionIdGenerator::value_type optionId)
{
    return GLManipulator::getOption(optionId);
}

double
BaseManipulator::getFixedSizeScale(Imath::V3d point)
{
    ViewportWrapperPtr viewport = getViewport();

    const int width = viewport->getWidth();
    const int height = viewport->getHeight();

    Imath::M44d viewMatrix = toImathMatrix44d(viewport->getViewMatrix44d());
    Imath::M44d projMatrix = toImathMatrix44d(viewport->getProjectionMatrix());

    Imath::M44d toScreenXform = viewMatrix * projMatrix;
    Imath::M44d screenToManip = toScreenXform.inverse();

    Imath::V3d a = point * toScreenXform;
    Imath::V3d b(a.x, a.y, a.z);

    // This is a pre-defined magic number to provide the manipulators with
    // a decent size, given the default global scale (1.0) and their current
    // drawables' size.
    const double magicFactor = 120.0;

    if (width < height) {
        b.x += mGlobalScale * magicFactor / width;
    } else {
        b.y += mGlobalScale * magicFactor / height;
    }

    return (a * screenToManip - b * screenToManip).length();
}

Attribute
BaseManipulator::getShaderAttribute(const std::string& attribute)
{
    const GroupAttribute materialAttr = getValue(getLastLocationPath(), "material");
    const GroupAttribute params =
        kodachi_moonray::light_util::getShaderParams(materialAttr, mTerminalName);
    return params.getChildByName(attribute);
}

void
BaseManipulator::setShaderAttribute(
    const std::string& name, const Attribute& attribute, bool isFinal)
{
    const std::string attrName = "material." + mTerminalName + "Params." + name;
    std::vector<std::string> paths;
    getLocationPaths(paths);
    for (const auto& path : paths)
        setValue(path, attrName, attribute, isFinal);
}

bool
BaseManipulator::isMaterialType(const std::string& type)
{
    const GroupAttribute materialAttr = getValue(getLastLocationPath(), "material");
    return kodachi_moonray::light_util::getShaderName(materialAttr) == type;
}

void
BaseManipulatorHandle::setOption(
    OptionIdGenerator::value_type optionId,
    Attribute attr)
{
    GLManipulatorHandle::setOption(optionId, attr);
}

Attribute
BaseManipulatorHandle::getOption(OptionIdGenerator::value_type optionId)
{
    // Don't hide the mouse cursor:
    static const OptionIdGenerator::value_type sHideMousePointer =
        OptionIdGenerator::GenerateId("HideMousePointer");
    static const OptionIdGenerator::value_type sNewMousePosition =
        OptionIdGenerator::GenerateId("NewMousePosition");
    if (optionId == sHideMousePointer || optionId == sNewMousePosition)
        return FnAttribute::Attribute();
    return GLManipulatorHandle::getOption(optionId);
}

Vec4f
BaseManipulatorHandle::getColor(const Vec4f& defaultColor)
{
    if (!getGLManipulator()->isInteractive()) {
        return Vec4f(0.5f, 0.5f, 0.5f, 0.8f);
    } else if (isDragging()) {
        return Vec4f(0.74f, 0.6f, 0.2f, 1.0f);
    } else if (isActive()) {
        return Vec4f(0.92f, 0.72f, 0.16f, 1.0f);
    } else if (isHovered()) {
        return Vec4f(0.95f, 0.83f, 0.49f, 1.0f);
    } else {
        return defaultColor;
    }
}

BaseManipulator*
BaseManipulatorHandle::getBaseManipulator()
{
    return getManipulator()->getPluginInstance<BaseManipulator>();
}

std::string
BaseManipulatorHandle::name()
{
    return name(mIndex);
}

std::string
BaseManipulatorHandle::name(int i)
{
    return getManipulator()->getManipulatorHandleName(i);
}

void
BaseManipulatorHandle::setup(int index)
{
    mAxis = Imath::V3d(0.0, 0.0, -1.0);
    mIndex = index;
    mColor = sDefaultColor[mIndex % sDefaultColor.size()];
    generateHandleMesh();
}

Attribute
BaseManipulatorHandle::getShaderAttribute()
{
    return getShaderAttribute(name());
}

Attribute
BaseManipulatorHandle::getShaderAttribute(const std::string& attribute)
{
    return getBaseManipulator()->getShaderAttribute(attribute);
}

void
BaseManipulatorHandle::updateLocalXform()
{
    if (mUseFixedScale) {
        Imath::M44d scaleMatrix;
        // Set fixed global scale. Compute screen-space scale by
        // ignoring any existing scale on the parent manipulator or
        // from the previous fixed global scale result.
        auto manip = getBaseManipulator();
        Imath::V3d existingScale, existingShear;
        Imath::M44d parentXform = toImathMatrix44d(manip->getXform());
        Imath::extractAndRemoveScalingAndShear(parentXform, existingScale, existingShear);
        const double fixedScale = manip->getFixedSizeScale(
                (mMeshXform * parentXform).translation());
        scaleMatrix.setScale(fixedScale);

        // Undo any existing scale so that we're solely using the fixed scale
        Imath::M44d invertExistingScale;
        invertExistingScale.setScale(existingScale);
        invertExistingScale.invert(true);
        setLocalXform(toMatrix44d((scaleMatrix * mMeshXform * invertExistingScale)));
    } else {
        setLocalXform(toMatrix44d(mMeshXform));
    }
}

void
BaseManipulatorHandle::draw()
{
    if (shouldDraw()) {
        // Convert axis to world space. We don't care about local xform.
        toImathMatrix44d(getManipulator()->getXform()).multDirMatrix(mAxis, mWSAxis);
        mWSAxis.normalize();

        updateLocalXform();

        // Draw the handle
        const auto color = getColor(Vec4f(mColor[0], mColor[1], mColor[2], 1.0f));
        useDrawingShader(getXform(), color, mDrawAsLines);
        for (VAO& drawable : mHandleMeshes) {
            if (mDrawAsLines) {
                glLineWidth(3);
                drawable.drawLines();
            } else {
                drawable.draw();
            }
        }
    }
}

void
BaseManipulatorHandle::pickerDraw(int64_t pickerId)
{
    if (shouldDraw()) {
        updateLocalXform();

        // Draw the handle as a flat color. This is called after draw(),
        // so all the transforms are already set.
        usePickingShader(getXform(), pickerId, 0);
        for (VAO& drawable : mHandleMeshes) {
            if (mDrawAsLines) {
                glLineWidth(10);
                drawable.drawLines();
            } else {
                drawable.draw();
            }
        }
    }
}

bool
BaseManipulatorHandle::shouldDraw()
{
    return getShaderAttribute().isValid();
}

bool
BaseManipulatorHandle::getDraggingPlane(Vec3d& origin, Vec3d& normal)
{
    GLManipulator* manip = getGLManipulator();
    origin = toVec3d(toImathMatrix44d(manip->getXform()).translation());

    // The plane is the same direction as the world-space axis,
    // but always facing the camera
    ViewportCameraWrapperPtr camera = manip->getViewport()->getActiveCamera();
    Imath::V3d viewVec = toImathV3d(camera->getDirection());
    normal = toVec3d(mWSAxis.cross(viewVec).normalize().cross(mWSAxis).normalize());

    return true;
}

void
BaseManipulatorHandle::startDrag(const Vec3d& initialPointOnPlane,
                                             const Vec2i& initialMousePosition)
{
    mInitialValue = FloatAttribute(getShaderAttribute()).getValue(false, 0.0f);
}

void
BaseManipulatorHandle::drag(const Vec3d& initialPointOnPlane,
                            const Vec3d& previousPointOnPlane,
                            const Vec3d& currentPointOnPlane,
                            const Vec2i& initialMousePosition,
                            const Vec2i& previousMousePosition,
                            const Vec2i& currentMousePosition,
                            bool isFinal)
{
    const double distance = getDistanceDragged(initialPointOnPlane,
                                               previousPointOnPlane,
                                               currentPointOnPlane,
                                               initialMousePosition,
                                               previousMousePosition,
                                               currentMousePosition);

    // Submit the new values. Currently only works with non-network materials.
    float value = std::min(std::max((float)(mInitialValue + distance), mClampMin), mClampMax);
    const float rounded = std::round(value);
    if (rounded != 0.0f && std::abs(value - rounded) <= sSnapToIntDelta)
        value = rounded;
    dragValue(name(), value, isFinal);
}

void
BaseManipulatorHandle::dragValue(const std::string& name, float value, bool isFinal)
{
    getBaseManipulator()->setShaderAttribute(name, FloatAttribute(value), isFinal);
}

void
BaseManipulatorHandle::setShaderAttribute(const Attribute& attribute, bool isFinal)
{
    setShaderAttribute(name(), attribute, isFinal);
}

void
BaseManipulatorHandle::setShaderAttribute(
    const std::string& name, const Attribute& attribute, bool isFinal)
{
    getBaseManipulator()->setShaderAttribute(name, attribute, isFinal);
}

double
BaseManipulatorHandle::getDistanceDragged(const Vec3d& initialPointOnPlane,
                                          const Vec3d& previousPointOnPlane,
                                          const Vec3d& currentPointOnPlane,
                                          const Vec2i& initialMousePosition,
                                          const Vec2i& previousMousePosition,
                                          const Vec2i& currentMousePosition)
{
    // This is a single-axis manipulator, so project the delta onto the world-space axis.
    const Imath::V3d delta = toImathV3d(currentPointOnPlane) - toImathV3d(initialPointOnPlane);
    return delta.dot(mWSAxis);
}

void
BaseManipulatorHandle::cancelManipulation()
{
    if (isDragging())
        dragValue(name(), mInitialValue, false);
}

void
MoonrayLabelManipulatorHandle::setup(int index)
{
    BaseManipulatorHandle::setup(index);
    mColor = { 0.3f, 0.3f, 0.3f, 1.0 };
    generateHandleMesh();
    if (!sFont) {
        sFont.reset(new FTPixmapFont("/usr/share/fonts/liberation/LiberationSans-Regular.ttf"));
        sFont->FaceSize(14);
    }
}

void
MoonrayLabelManipulatorHandle::draw()
{
    if (shouldDraw()) {
        // Dragging to the right always increases
        mWSAxis = toImathV3d(-getViewport()->getActiveCamera()->getLeft());

        // Calculate bounds of text
        const std::string label = getLabel();
        const FTBBox bbox = calculateLabelSize(label);

        // Draw background label
        Vec4f newColor = getColor(Vec4f(mColor[0], mColor[1], mColor[2], mColor[3]));
        std::array<double, 4> color = { newColor.x, newColor.y, newColor.z, newColor.w };
        drawLabel(color);

        // Draw text
        const double fontY = mY + (mBoxHeight - (bbox.Upper().Y() - bbox.Lower().Y()) / 2.0);
        sFont->Render(label.c_str(), -1, FTPoint(mX + kBoxMargin, getViewport()->getHeight() - fontY));
    }
}

void
MoonrayLabelManipulatorHandle::pickerDraw(int64_t pickerId)
{
    if (shouldDraw()) {
        calculateLabelSize(getLabel());

        std::array<double, 4> color = {
            // This is a little different from pickIdToColor,
            // but matches Foundry's frag shader.
            pickerId / 255.0, 0.0, 0.0, 0.0
        };
        drawLabel(color);
    }
}

std::string
MoonrayLabelManipulatorHandle::getLabel()
{
    std::ostringstream stream;
    stream << name() << ": " << FloatAttribute(getShaderAttribute()).getValue(-1.0f, false);
    return stream.str();
}

FTBBox
MoonrayLabelManipulatorHandle::calculateLabelSize(const std::string& label)
{
    const FTBBox bbox = sFont->BBox(label.c_str());

    mBoxHeight = kBoxMargin * 2.0 + sFont->LineHeight();
    mX = kScreenSpacing;
    mY = kScreenSpacing + mIndex * (mBoxHeight + kScreenSpacing);
    mBoxWidth = bbox.Upper().X() - bbox.Lower().X() + 2 * kBoxMargin;

    return bbox;
}

void
MoonrayLabelManipulatorHandle::drawLabel(const std::array<double, 4>& color)
{
    glUseProgram(0);

    const double width = getViewport()->getWidth();
    const double height = getViewport()->getHeight();

    // Draw a box in 2D with coordinates calculated in draw()
    glMatrixMode(GL_PROJECTION);
    glPushMatrix();
    glLoadIdentity();
    glOrtho(0.0, width, height, 0.0, -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);
    glPushMatrix();
    glLoadIdentity();

    glBegin(GL_QUADS);
        glColor4d(color[0], color[1], color[2], color[3]);
        glVertex2d(mX, mY);
        glVertex2d(mX + mBoxWidth, mY);
        glVertex2d(mX + mBoxWidth, mY + mBoxHeight);
        glVertex2d(mX, mY + mBoxHeight);
    glEnd();

    glPopMatrix();
    glMatrixMode(GL_PROJECTION);
    glPopMatrix();
}

bool
MoonrayLabelManipulatorHandle::getDraggingPlane(Vec3d& origin, Vec3d& normal)
{
    ViewportCameraWrapperPtr camera = getViewport()->getActiveCamera();

    // Position the plane 1 unit in front of camera perpendicular to it,
    // to mimic a 2D plane.
    origin = camera->getOrigin() + camera->getDirection();
    normal = -camera->getDirection();

    return true;
}

}

