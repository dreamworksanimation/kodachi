// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseManipulator.h"

#include <FnAttribute/FnGroupBuilder.h>

namespace MoonrayKatana {

// A manipulator that manages 4 DecayLightFilterManpulatorHandles for use with
// DecayLightFilters. A corresponding DecayLightFilterDrawable is assumed
// to exist, otherwise the final location of the manipulator handles may be
// offset improperly.
class DecayLightFilterManipulator : public BaseManipulator
{
public:
    DecayLightFilterManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new DecayLightFilterManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Decay", "Ctrl+D", "Light Filter"); }

    void setup() override;

    float getLightTypeOffset();
};

class DecayLightFilterManipulatorHandle : public BaseManipulatorHandle
{
public:
    DecayLightFilterManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new DecayLightFilterManipulatorHandle(); }
    static void flush() {}

    void generateHandleMesh() override;
    bool shouldDraw() override;

    // Initialization function manually called after creation
    // Index refers to which handle this is (near_start == 0).
    void setup(int index) override;

    void updateLocalXform() override;

    void startDrag(const Vec3d& initialPointOnPlane, const Vec2i& initialMousePosition) override;
    void drag(const Vec3d& initialPointOnPlane, const Vec3d& previousPointOnPlane,
              const Vec3d& currentPointOnPlane, const Vec2i& initialMousePosition,
              const Vec2i& previousMousePosition, const Vec2i& currentMousePosition,
              bool isFinal) override;
    void dragValue(const std::string& name, float value, bool isFinal) override;

private:
    float values[4]; // current values of all 4 when dragging
    // Get the parameter corresponding to this handle
    double getValue();
    // Convenience function to auto-cast getManipulator
    DecayLightFilterManipulator* getDecayManipulator();
    // When dragging, the total distance dragged since the start
    double mTempOffset = 0.0;
};

}

