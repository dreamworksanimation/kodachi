// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "DecayLightFilterManipulator.h"

#include <FnGeolib/util/Path.h>
#include <GL/gl.h>

#include <kodachi_moonray/light_util/LightUtil.h>

#include "../Drawables/DecayLightFilterDrawable.h"
#include "../Drawables/VAOBuilder.h"

using namespace Foundry::Katana::ViewerUtils; // for toImathV3d

namespace MoonrayKatana {

bool
DecayLightFilterManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    if (type.isValid() && type == "light filter") {
        // Currently we only support non-network materials of type DecayLightFilter.
        // This is what the Gaffer node will generate.
        return kodachi_moonray::light_util::getShaderName(
            locationAttrs.getChildByName("material"), "moonrayLightfilter") == "DecayLightFilter" &&
            // no manipulators for light filter references
            !locationAttrs.getChildByName("referencePath").isValid();
    }
    return false;
}

void
DecayLightFilterManipulator::setup()
{
    mTerminalName = "moonrayLightfilter";
    // Initialize one handle for each stage of decay
    const std::vector<std::string> names = { "near_start", "near_end", "far_start", "far_end" };
    initHandles("MoonrayDecayLightFilterManipulatorHandle", names);
}

float
DecayLightFilterManipulator::getLightTypeOffset()
{
    GroupAttribute materialAttr(
        getValue(FnKat::Util::Path::GetLocationParent(getLastLocationPath()), "material"));
    const std::string lightType =
        kodachi_moonray::light_util::getShaderName(materialAttr);
    if (lightType == "SphereLight" || lightType == "CylinderLight") {
        const GroupAttribute params =
            kodachi_moonray::light_util::getShaderParams(materialAttr);
        return FloatAttribute(params.getChildByName("radius")).getValue(0.0f, false);
    } else {
        return 0;
    }
}

void
DecayLightFilterManipulatorHandle::setup(int index)
{
    BaseManipulatorHandle::setup(index);
    mColor[0] = MoonrayKatana::DecayLightFilterDrawable::sColors[mIndex][0];
    mColor[1] = MoonrayKatana::DecayLightFilterDrawable::sColors[mIndex][1];
    mColor[2] = MoonrayKatana::DecayLightFilterDrawable::sColors[mIndex][2];

    if (mIndex > 1)
        mMeshXform.setEulerAngles(Imath::Vec3<double>(0.0, M_PI, 0.0));
}

void
DecayLightFilterManipulatorHandle::generateHandleMesh()
{
    const size_t index = mHandleMeshes.size();
    mHandleMeshes.push_back(VAO());
    VAOBuilder::generateCylinder(Vec3f(0.0f, 0.0f, 0.0f), 0.118f, 0.001f, 0.4f, mHandleMeshes[index]);
}

double
DecayLightFilterManipulatorHandle::getValue()
{
    // When dragging, attribute isn't guaranteed to return the value
    // set by setValue(). So, we keep track of the real position manually through
    // some extra variables.
    if (isDragging()) return mInitialValue + mTempOffset;
    return FloatAttribute(getShaderAttribute()).getValue(false, 0.0f);
}

DecayLightFilterManipulator*
DecayLightFilterManipulatorHandle::getDecayManipulator()
{
    return getManipulator()->getPluginInstance<DecayLightFilterManipulator>();
}

void
DecayLightFilterManipulatorHandle::updateLocalXform()
{

    // Different types of lights may have different offset, ie sphere vs spot.
    const double lightOffset = getDecayManipulator()->getLightTypeOffset();

    mMeshXform[3][2] = -(getValue() + lightOffset);
    BaseManipulatorHandle::updateLocalXform();
}

bool
DecayLightFilterManipulatorHandle::shouldDraw()
{
    // don't draw the black ones when equal to the white ones
    if (mIndex == 0) {
        if (getValue() >= FloatAttribute(getShaderAttribute(name(1))).getValue(false, 0.0f))
            return false;
    } else if (mIndex == 3) {
        if (getValue() <= FloatAttribute(getShaderAttribute(name(2))).getValue(false, 0.0f))
            return false;
    }
    return true;
}

void
DecayLightFilterManipulatorHandle::startDrag(const Vec3d& initialPointOnPlane,
                                             const Vec2i& initialMousePosition)
{
    BaseManipulatorHandle::startDrag(initialPointOnPlane, initialMousePosition);
    mTempOffset = 0.0;
    mClampMin = 0.0f;
    mClampMax = std::numeric_limits<float>::max();

    auto manip = getBaseManipulator();
    for (int i = 0; i < 4; ++i) {
        values[i] = FloatAttribute(getShaderAttribute(name(i))).getValue(false, 0.0f);
    }
}

void
DecayLightFilterManipulatorHandle::dragValue(const std::string& name, float value, bool isFinal)
{
    for (int i = 0; i < 4; ++i) {
        float v;
        if (i < mIndex) v = std::min(value, values[i]);
        else if (i > mIndex) v = std::max(value, values[i]);
        else v = value;
        BaseManipulatorHandle::dragValue(this->name(i), v, isFinal);
    }
}

void
DecayLightFilterManipulatorHandle::drag(const Vec3d& initialPointOnPlane,
                                        const Vec3d& previousPointOnPlane,
                                        const Vec3d& currentPointOnPlane,
                                        const Vec2i& initialMousePosition,
                                        const Vec2i& previousMousePosition,
                                        const Vec2i& currentMousePosition,
                                        bool isFinal)
{
    BaseManipulatorHandle::drag(initialPointOnPlane, previousPointOnPlane,
                                            currentPointOnPlane, initialMousePosition,
                                            previousMousePosition, currentMousePosition,
                                            isFinal);
    // This is a single-axis manipulator, so project the delta onto the world-space axis.
    const Imath::V3d delta = toImathV3d(currentPointOnPlane) - toImathV3d(initialPointOnPlane);
    const double distance = delta.dot(mWSAxis);
    mTempOffset = distance;
}

}

