// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseManipulator.h"

namespace MoonrayKatana {

class BarnDoorsLightFilterManipulator : public BaseManipulator
{
public:
    BarnDoorsLightFilterManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new BarnDoorsLightFilterManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Barn Doors", "Ctrl+B", "Light Filter"); }

    virtual void setup() override;
    GroupAttribute getShaderParams();
    GroupAttribute getLightParams();
};

// Handle for a control point of the barn doors
class BarnDoorsLightFilterManipulatorHandle : public BaseManipulatorHandle
{
public:
    BarnDoorsLightFilterManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new BarnDoorsLightFilterManipulatorHandle(); }
    static void flush() {}

    virtual void setup(int index) override;
    virtual void updateLocalXform() override;
    virtual bool shouldDraw() override;

    virtual void generateHandleMesh() override;

    virtual bool getDraggingPlane(Vec3d& origin, Vec3d& normal) override;
    virtual void startDrag(const Vec3d& initialPointOnPlane,
            const Vec2i& initialMousePosition) override;
    void drag(const Vec3d& initialPointOnPlane, const Vec3d& previousPointOnPlane,
            const Vec3d& currentPointOnPlane, const Vec2i& initialMousePosition,
            const Vec2i& previousMousePosition, const Vec2i& currentMousePosition,
            bool isFinal);

    virtual void cancelManipulation() override;

private:
    // Whether the last selected location is in look-through mode
    bool inLookThrough();
    // Returns distance from light
    float mDistance;
    // Convenience function to auto-cast getManipulator
    BarnDoorsLightFilterManipulator* getBarnDoorsManipulator();
    // Since BaseManipulatorHandle only stores a single float for mInitialAttr,
    // we have to store the full FloatAttribute here
    std::vector<FloatAttribute> mInitialValueAttr;
};

}

