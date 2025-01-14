// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "BaseManipulator.h"

#include <FnAttribute/FnGroupBuilder.h>

namespace MoonrayKatana {

// --------------------------------------------------------

class RadiusManipulator : public BaseManipulator
{
public:
    RadiusManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new RadiusManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Radius", "Shift+R"); }

    virtual void setup() override;
};

class RadiusManipulatorHandle : public BaseManipulatorHandle
{
public:
    RadiusManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new RadiusManipulatorHandle(); }
    static void flush() {}

    virtual void setup(int index) override;
    virtual void generateHandleMesh() override;
    virtual void updateLocalXform() override;
    virtual bool getDraggingPlane(Vec3d& origin, Vec3d& normal) override;
    virtual double getDistanceDragged(const Vec3d& initialPointOnPlane,
                                      const Vec3d& previousPointOnPlane,
                                      const Vec3d& currentPointOnPlane,
                                      const Vec2i& initialMousePosition,
                                      const Vec2i& previousMousePosition,
                                      const Vec2i& currentMousePosition) override;
};

// --------------------------------------------------------

class ConeAngleManipulator : public BaseManipulator
{
public:
    ConeAngleManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new ConeAngleManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Cone Angle", "Shift+C"); }

    virtual void setup() override;
};

class ConeAngleManipulatorHandle : public RadiusManipulatorHandle
{
public:
    ConeAngleManipulatorHandle() : RadiusManipulatorHandle() {}
    static ManipulatorHandle* create() { return new ConeAngleManipulatorHandle(); }
    static void flush() {}

    virtual void updateLocalXform() override;
    virtual bool getDraggingPlane(Vec3d& origin, Vec3d& normal) override;
    virtual void startDrag(const Vec3d& initialPointOnPlane,
                           const Vec2i& initialMousePosition) override;
    virtual double getDistanceDragged(const Vec3d& initialPointOnPlane,
                                      const Vec3d& previousPointOnPlane,
                                      const Vec3d& currentPointOnPlane,
                                      const Vec2i& initialMousePosition,
                                      const Vec2i& previousMousePosition,
                                      const Vec2i& currentMousePosition) override;

protected:
    // cached attributes to avoid recomputing in getDistanceDragged
    double mCoi = 0.0;
    double mLensRadius = 0.0;
};

// --------------------------------------------------------

class AspectRatioManipulator : public BaseManipulator
{
public:
    AspectRatioManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new AspectRatioManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Aspect Ratio", "Shift+A"); }

    virtual void setup() override;
};

class AspectRatioManipulatorHandle : public BaseManipulatorHandle
{
public:
    AspectRatioManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new AspectRatioManipulatorHandle(); }
    static void flush() {}

    virtual void setup(int index) override;
    virtual void generateHandleMesh() override;
    virtual void startDrag(const Vec3d& initialPointOnPlane,
                           const Vec2i& initialMousePosition) override;
    virtual double getDistanceDragged(const Vec3d& initialPointOnPlane,
                                      const Vec3d& previousPointOnPlane,
                                      const Vec3d& currentPointOnPlane,
                                      const Vec2i& initialMousePosition,
                                      const Vec2i& previousMousePosition,
                                      const Vec2i& currentMousePosition) override;

protected:
    double mTempOffset = 0.0;
};

// --------------------------------------------------------

class ExposureManipulator : public BaseManipulator
{
public:
    ExposureManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new ExposureManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Exposure", "Shift+E"); }

    virtual void setup() override;
};

// --------------------------------------------------------

class SizeManipulator : public BaseManipulator
{
public:
    SizeManipulator() : BaseManipulator() {}
    static Manipulator* create() { return new SizeManipulator(); }
    static void flush() {}

    static bool matches(const FnAttribute::GroupAttribute& locationAttrs);

    static GroupAttribute getTags() { return tags("Size", "Shift+B"); }

    virtual void setup() override;
};

class SizeManipulatorHandle : public BaseManipulatorHandle
{
public:
    SizeManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new SizeManipulatorHandle(); }
    static void flush() {}

    virtual void setup(int index) override;
    virtual void generateHandleMesh() override;
};

class SizeEdgeManipulatorHandle : public BaseManipulatorHandle
{
public:
    SizeEdgeManipulatorHandle() : BaseManipulatorHandle() {}
    static ManipulatorHandle* create() { return new SizeEdgeManipulatorHandle(); }
    static void flush() {}

    virtual void setup(int index) override;
    void setIsLeft() { mIsLeft = true; }
    virtual void generateHandleMesh() override;
    virtual void updateLocalXform() override;
    virtual void startDrag(const Vec3d& initialPointOnPlane,
                           const Vec2i& initialMousePosition) override;
    void drag(const Vec3d& initialPointOnPlane, const Vec3d& previousPointOnPlane,
              const Vec3d& currentPointOnPlane, const Vec2i& initialMousePosition,
              const Vec2i& previousMousePosition, const Vec2i& currentMousePosition,
              bool isFinal);
    virtual void cancelManipulation() override;

protected:
    // Whether this is the left (or bottom) edge manipulator, which causes
    // the drag distance to be negated
    bool mIsLeft = false;
    float prevValue;
};

// --------------------------------------------------------

}

