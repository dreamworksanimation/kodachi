// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <FnViewer/utils/FnGLManipulator.h>
#include <OpenEXR/ImathVec.h>
#include "../Drawables/VAO.h"
#include <FnViewer/utils/FnImathHelpers.h>

using namespace Foundry::Katana::ViewerAPI;
using namespace FnAttribute;

class FTBBox;
class FTPixmapFont;

namespace MoonrayKatana {

// A default implementation for a manipulator meant to be subclassed.
class BaseManipulator : public Foundry::Katana::ViewerUtils::GLManipulator
{
public:
    BaseManipulator() : GLManipulator() {}

    void setOption(OptionIdGenerator::value_type optionId, Attribute attr) override;
    Attribute getOption(OptionIdGenerator::value_type optionId) override;
    void draw() override;
    void pickerDraw(int64_t pickerId) override;

    // Ripped from Foundry example manipulators, used to calculate
    // fixed scale as user zooms in and out
    double getFixedSizeScale(Imath::V3d point);

    // For all valid selected locations, retrieve the last one. This will be
    // used as an indicator of where to draw the manipulator handles and other data.
    std::string getLastLocationPath();

    bool isMaterialType(const std::string& type);

    // initHandles should generally be called during setup(), which will
    // initialize a BaseManipulatorHandle as well as a
    // MoonrayLabelManipulatorHandle for each attribute passed in.
    virtual void initHandles(const std::string& handleClassName,
                             const std::vector<std::string>& handleNames,
                             bool includeLabelHandles = true);

    Attribute getShaderAttribute(const std::string& attribute);
    void setShaderAttribute(const std::string& attribute, const Attribute&, bool isFinal);

    const std::string& getTerminalName() const { return mTerminalName; }

protected:
    // Used by getFixedSizeScale, controlled by setOption
    double mGlobalScale = 1.0;
    // The name of the shader type to manipulate
    std::string mTerminalName = "moonrayLight";
    // for implementing getTags() on the subclasses
    static FnAttribute::GroupAttribute
    tags(const char* name, const char* shortcut, const char* group = 0);
};

// A default implementation for a manipulator handle. A red cone appears at 0,0
// which can be dragged along the z axis to increase or decrease a value. Any
// individual function can be overridden to customize how the handle looks or
// behaves, while still reusing all the other default parts.
// This class assumes that the parameter being modified can be converted to a
// float for modification and display.
class BaseManipulatorHandle : public Foundry::Katana::ViewerUtils::GLManipulatorHandle
{
public:
    BaseManipulatorHandle() : GLManipulatorHandle() {}

    void setOption(OptionIdGenerator::value_type optionId, Attribute attr) override;
    Attribute getOption(OptionIdGenerator::value_type optionId) override;

    // Any initial handle setup immediately after the handle is created. The
    // index passed in is decided by the parent manipulator, but is intended
    // to be equal to the order the manipulator handle is added to the parent.
    virtual void setup(int index);
    // Initialize mHandleMesh
    virtual void generateHandleMesh() = 0;

    void draw() override;
    void pickerDraw(int64_t pickerId) override;

    bool getDraggingPlane(Vec3d& origin, Vec3d& normal) override;
    void startDrag(const Vec3d& initialPointOnPlane, const Vec2i& initialMousePosition) override;
    void drag(const Vec3d& initialPointOnPlane, const Vec3d& previousPointOnPlane,
              const Vec3d& currentPointOnPlane, const Vec2i& initialMousePosition,
              const Vec2i& previousMousePosition, const Vec2i& currentMousePosition,
              bool isFinal) override;
    void cancelManipulation() override;

    virtual void dragValue(const std::string&, float value, bool isFinal);

    std::string name();
    std::string name(int index); // get name of other handles on same manipulator

    Attribute getShaderAttribute();
    Attribute getShaderAttribute(const std::string& name);

    void setShaderAttribute(const Attribute&, bool isFinal);
    void setShaderAttribute(const std::string& name, const Attribute&, bool isFinal);

protected:
    BaseManipulator* getBaseManipulator();
    Vec4f getColor(const Vec4f& defaultColor);

    // Rounds a param to the nearest int if it's <= delta away from the int/
    // Setting to 0 effectively disables it.
    static float sSnapToIntDelta;

    // Returns a delta to be used in drag() when setting a new parameter value.
    // Make sure that a delta is returned, not the new parameter value.
    virtual double getDistanceDragged(const Vec3d& initialPointOnPlane,
                                      const Vec3d& previousPointOnPlane,
                                      const Vec3d& currentPointOnPlane,
                                      const Vec2i& initialMousePosition,
                                      const Vec2i& previousMousePosition,
                                      const Vec2i& currentMousePosition);
    virtual void updateLocalXform();

    // Whether this handle should be drawn or not. Updated every frame.
    virtual bool shouldDraw();

    // helper functions
    float clamp(float x) const {return std::max(mClampMin, std::min(mClampMax, x));}

    static std::array<std::array<float, 4>, 4> sDefaultColor;

    // Visual display of draggable handle
    std::vector<VAO> mHandleMeshes;
    // A transform to apply to mHandleMesh
    Imath::M44d mMeshXform;
    // Manipulator drag axis
    Imath::V3d mAxis;
    // World-space transformation of manipulator axis
    Imath::V3d mWSAxis;
    // Handle index defined by setup()
    int mIndex = 0;
    // Color of mHandleMesh
    std::array<float, 4> mColor;
    // When dragging, the value of the parameter at the start
    double mInitialValue = 0.0;
    // If true, mMeshXform will be set to always draw handles at constant size
    bool mUseFixedScale = true;
    // If true, mHandleMeshes will use drawLines() instead of draw()
    bool mDrawAsLines = false;
    // When setValue is called, clamps between min and max first.
    float mClampMin = -std::numeric_limits<float>::max();
    float mClampMax= std::numeric_limits<float>::max();
};

// A default implementation for displaying an attribute as a draggable label
class MoonrayLabelManipulatorHandle : public BaseManipulatorHandle
{
public:
    MoonrayLabelManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new MoonrayLabelManipulatorHandle(); }
    static void flush() {}

    void setup(int index) override;
    void generateHandleMesh() override {}

    void draw() override;
    void pickerDraw(int64_t pickerId) override;
    bool getDraggingPlane(Vec3d& origin, Vec3d& normal) override;

    // These aren't working right in 3.0v2
    bool shouldDraw() override { return false; }

protected:
    virtual std::string getLabel();
    // Set member variables depending on size of string
    virtual FTBBox calculateLabelSize(const std::string& label);
    // Draw the background of the label behind the text
    void drawLabel(const std::array<double, 4>& color);

    // FTGL font used to render text
    static std::unique_ptr<FTPixmapFont> sFont;

    // Bounds of the label used in drawLabel
    double mX = 0.0, mY = 0.0, mBoxWidth = 0.0, mBoxHeight = 0.0;
};

}

