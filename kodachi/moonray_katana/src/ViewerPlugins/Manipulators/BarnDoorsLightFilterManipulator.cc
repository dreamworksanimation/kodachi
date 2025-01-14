// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "BarnDoorsLightFilterManipulator.h"

#include <FnGeolib/util/Path.h>
#include <GL/gl.h>

#include <kodachi_moonray/light_util/LightUtil.h>

using namespace Foundry::Katana::ViewerUtils; // for toImathMatrix44d

namespace MoonrayKatana {

namespace {// helper aliasings, constants and functions
// helper aliasings
enum class Pos {
    TOP_RIGHT,
    TOP_LEFT,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
    LEFT,
    BOTTOM,
    RIGHT,
    TOP,
};

using V3dPair = std::array<Imath::V3d, 2>;
using LineType = std::array<std::array<float, 2>, 2>;

// costants
const std::vector<std::string> handleNames {
    "top_right", "top_left", "bottom_left", "bottom_right",
    "left", "bottom", "right", "top"
};
constexpr float kCornerLength = 0.2f; // Percentage of handles

// functions
// aliasing conversion
inline bool
operator==(int index, Pos pos) {
    return index == static_cast<int>(pos);
}

// get attribute name for an edge handle
inline void
getAttrName(int index, std::array<std::string, 2>& attrName) {
    if (index == Pos::LEFT) {
        attrName[0] = "top_left";
        attrName[1] = "bottom_left";
    } else if (index == Pos::BOTTOM) {
        attrName[0] = "bottom_left";
        attrName[1] = "bottom_right";
    } else if (index == Pos::RIGHT) {
        attrName[0] = "top_right";
        attrName[1] = "bottom_right";
    } else if (index == Pos::TOP) {
        attrName[0] = "top_left";
        attrName[1] = "top_right";
    }
}

// helper function to convert world space point to object space
// for corner handle, it takes one point reference as output
// for edge handle, it takes two points references as output
inline void
getPointInObjectSpace(
        BarnDoorsLightFilterManipulator* manip,
        const Imath::V3d& pointInWorldSpace,
        Imath::V3d& pointInObjectSpace,
        Pos corner)
{
    toImathMatrix44d(manip->getXform()).invert().multVecMatrix(pointInWorldSpace, pointInObjectSpace);

    if (corner == Pos::TOP_LEFT || corner == Pos::BOTTOM_LEFT) {
        pointInObjectSpace.x = -pointInObjectSpace.x;
    }
    if (corner == Pos::BOTTOM_LEFT || corner == Pos::BOTTOM_RIGHT) {
        pointInObjectSpace.y = -pointInObjectSpace.y;
    }
}

inline void
getPointInObjectSpace(
        BarnDoorsLightFilterManipulator* manip,
        const Imath::V3d& pointInWorldSpace,
        Imath::V3d& pointInObjectSpace1,
        Imath::V3d& pointInObjectSpace2,
        Pos edge)
{
    if (edge == Pos::LEFT) {
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace1, Pos::TOP_LEFT);
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace2, Pos::BOTTOM_LEFT);
    } else if (edge == Pos::BOTTOM) {
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace1, Pos::BOTTOM_LEFT);
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace2, Pos::BOTTOM_RIGHT);
    } else if (edge == Pos::RIGHT) {
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace1, Pos::TOP_RIGHT);
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace2, Pos::BOTTOM_RIGHT);
    } else if (edge == Pos::TOP) {
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace1, Pos::TOP_LEFT);
        getPointInObjectSpace(manip, pointInWorldSpace, pointInObjectSpace2, Pos::TOP_RIGHT);
    }
}

} // end of namespace

/******************************************* Manipulator ***********************************************/
bool
BarnDoorsLightFilterManipulator::matches(const FnAttribute::GroupAttribute& locationAttrs)
{
    const auto type = StringAttribute(locationAttrs.getChildByName("type"));
    if (type == "light filter") {
        GroupAttribute material(locationAttrs.getChildByName("material"));
        return kodachi_moonray::light_util::getShaderName(material, "moonrayLightfilter") ==
            "BarnDoorsLightFilter" &&
            // no manipulators for light filter references
            not locationAttrs.getChildByName("referencePath").isValid();
    }

    return false;
}

void
BarnDoorsLightFilterManipulator::setup()
{
    mTerminalName = "moonrayLightfilter";
    initHandles("MoonrayBarnDoorsLightFilterManipulatorHandle", handleNames, false);
}

GroupAttribute
BarnDoorsLightFilterManipulator::getShaderParams()
{
    const GroupAttribute material = getValue(getLastLocationPath(), "material");
    return kodachi_moonray::light_util::getShaderParams(material, mTerminalName);
}

GroupAttribute
BarnDoorsLightFilterManipulator::getLightParams()
{
    std::string lightLocation(FnKat::Util::Path::GetLocationParent(getLastLocationPath()));
    const GroupAttribute material = getValue(lightLocation, "material");
    return kodachi_moonray::light_util::getShaderParams(material);
}

BarnDoorsLightFilterManipulator*
BarnDoorsLightFilterManipulatorHandle::getBarnDoorsManipulator()
{
    return getManipulator()->getPluginInstance<BarnDoorsLightFilterManipulator>();
}
/******************************************* Handle ***********************************************/
void
BarnDoorsLightFilterManipulatorHandle::setup(int index)
{
    BaseManipulatorHandle::setup(index);
    mAxis = Imath::V3d(0.0, 0.0, 1.0);
    mClampMin = -1.0f;
    mClampMax = 1.0f;
    mDrawAsLines = true;
    mUseFixedScale = false;
    mColor = sDefaultColor[0];
}

bool
BarnDoorsLightFilterManipulatorHandle::inLookThrough()
{
    auto manip = getBarnDoorsManipulator();
    return getViewport()->getActiveCamera()->getLocationPath() ==
        FnKat::Util::Path::GetLocationParent(manip->getLastLocationPath());
}

void
BarnDoorsLightFilterManipulatorHandle::generateHandleMesh()
{
    // this is just a mesh for one or two joined lines
    static const float vertices[] = {0,0,0, 0,0,1, 0,0,2};
    static const unsigned indices[] = {0, 1, 1, 2};

    mHandleMeshes.push_back(VAO());
    if (mIndex < 4) // corner
        mHandleMeshes[0].setup(vertices, 3, indices, 4);
    else // edges
        mHandleMeshes[0].setup(vertices, 2, indices, 2);
}

void
BarnDoorsLightFilterManipulatorHandle::updateLocalXform()
{
    std::array<float, 24> P;
    std::array<int, 16> vertexList;
    auto manip = getBarnDoorsManipulator();
    kodachi_moonray::light_util::BarnDoor doors(
        manip->getLightParams(), manip->getShaderParams());
    doors.populateBuffers(P.data(), vertexList.data());
    mDistance = doors.distance;

    // get four corners
    Vec3f tr, tl, bl, br;
    tr = Vec3f(P[12], P[13], P[14]);
    tl = Vec3f(P[15], P[16], P[17]);
    bl = Vec3f(P[18], P[19], P[20]);
    br = Vec3f(P[21], P[22], P[23]);

    if (mIndex < 4) { // corner
        // Generate 2 lines to form an angle
        // p1 == start point, p2 and p3 == end points
        Vec3f p1, p2, p3;
        if (mIndex == Pos::TOP_RIGHT) {
            p1 = tr;
            p2 = tl;
            p3 = br;
        } else if (mIndex == Pos::TOP_LEFT) {
            p1 = tl;
            p2 = tr;
            p3 = bl;
        } else if (mIndex == Pos::BOTTOM_LEFT) {
            p1 = bl;
            p2 = tl;
            p3 = br;
        } else { // Pos::BOTTOM_RIGHT
            p1 = br;
            p2 = bl;
            p3 = tr;
        }

        std::vector<Vec3f> vertices;
        vertices.push_back((p2-p1) * kCornerLength + p1);
        vertices.push_back(p1);
        vertices.push_back((p3-p1) * kCornerLength + p1);
        mHandleMeshes[0].updateVertices(vertices);

    } else { // edges

        Vec3f p1, p2;
        if (mIndex == Pos::LEFT) {
            p1 = bl;
            p2 = tl;
        } else if (mIndex == Pos::BOTTOM) {
            p1 = bl;
            p2 = br;
        } else if (mIndex == Pos::RIGHT) {
            p1 = br;
            p2 = tr;
        } else { // TOP
            p1 = tr;
            p2 = tl;
        }

        std::vector<Vec3f> vertices;
        vertices.push_back(p1 * kCornerLength + p2 * (1 - kCornerLength));
        vertices.push_back(p1 * (1 - kCornerLength) + p2 * kCornerLength);
        mHandleMeshes[0].updateVertices(vertices);
    }

    BaseManipulatorHandle::updateLocalXform();
}

bool
BarnDoorsLightFilterManipulatorHandle::getDraggingPlane(Vec3d& origin, Vec3d& normal)
{
    // Get default origin
    GLManipulator* manip = getGLManipulator();
    auto mat = toImathMatrix44d(manip->getXform());
    mat.translate(Imath::V3d(0.0, 0.0, -mDistance));
    origin = toVec3d(mat.translation());
    normal = toVec3d(mWSAxis);
    return true;
}

void
BarnDoorsLightFilterManipulatorHandle::startDrag(
        const Vec3d& initialPointOnPlane,
        const Vec2i& initialMousePosition)
{
    if (mIndex < 4) { // corner
        mInitialValueAttr.resize(1);
        mInitialValueAttr[0] = FloatAttribute(getShaderAttribute());
    } else { // edge
        std::array<std::string, 2> attrName;
        getAttrName(mIndex, attrName);

        mInitialValueAttr.resize(2);
        mInitialValueAttr[0] = FloatAttribute(getShaderAttribute(attrName[0]));
        mInitialValueAttr[1] = FloatAttribute(getShaderAttribute(attrName[1]));
    }
}

bool
BarnDoorsLightFilterManipulatorHandle::shouldDraw()
{
    if (mIndex < 4) { // corner
        return getShaderAttribute().isValid();
    } else { // edge
        std::array<std::string, 2> attrName;
        getAttrName(mIndex, attrName);
        return getShaderAttribute(attrName[0]).isValid() &&
               getShaderAttribute(attrName[1]).isValid();
    }
}

void
BarnDoorsLightFilterManipulatorHandle::drag(
        const Vec3d& initialPointOnPlane,
        const Vec3d& previousPointOnPlane,
        const Vec3d& currentPointOnPlane,
        const Vec2i& initialMousePosition,
        const Vec2i& previousMousePosition,
        const Vec2i& currentMousePosition,
        bool isFinal)
{
    auto manip = getBarnDoorsManipulator();
    kodachi_moonray::light_util::BarnDoor doors(
        manip->getLightParams(), manip->getShaderParams());
    const float& outerRadiusX(doors.outerRadiusX);
    const float& outerRadiusY(doors.outerRadiusY);

    if (mIndex < 4) { //corners
        // get click points, current and previous
        Imath::V3d currentActualPoint, previousActualPoint;
        getPointInObjectSpace(manip, toImathV3d(currentPointOnPlane), currentActualPoint, static_cast<Pos>(mIndex));
        getPointInObjectSpace(manip, toImathV3d(previousPointOnPlane), previousActualPoint, static_cast<Pos>(mIndex));

        // get original control point
        auto originalValue = FloatAttribute(getShaderAttribute()).getNearestSample(0);
        auto originalActualPoint = Imath::V3d((1 - originalValue[0]) * outerRadiusX,
                                              (1 - originalValue[1]) * outerRadiusY, 0);

        // calculate final point
        auto finalActualPoint = originalActualPoint + currentActualPoint - previousActualPoint;

        float finalValue[2] = {
            1 - (float)finalActualPoint.x / outerRadiusX,
            1 - (float)finalActualPoint.y / outerRadiusY
        };
        for (int j = 0; j < 2; ++j) {
            float& v(finalValue[j]);
            if (v == 0) // work around Katana bug where it replaces 0 with garbage
                v = FLT_MIN;
        }

        // set the value back
        setShaderAttribute(FnAttribute::FloatAttribute(finalValue, 2 , 1), isFinal);

    } else { //edges
        // get current and previous click points in object space
        V3dPair currentActualPoints;
        V3dPair previousActualPoints;

        std::array<std::string, 2> attrName;
        getAttrName(mIndex, attrName);

        LineType finalValues;

        getPointInObjectSpace(manip, toImathV3d(currentPointOnPlane), currentActualPoints[0], currentActualPoints[1], static_cast<Pos>(mIndex));
        getPointInObjectSpace(manip, toImathV3d(previousPointOnPlane), previousActualPoints[0], previousActualPoints[1], static_cast<Pos>(mIndex));

        // get values from shader
        std::array<Foundry::Katana::FloatAttribute::array_type, 2> originalValues {
            FloatAttribute(getShaderAttribute(attrName[0])).getNearestSample(0),
            FloatAttribute(getShaderAttribute(attrName[1])).getNearestSample(0)
        };

        // calculate the control points from shader values
        V3dPair originalActualPoints {
            Imath::V3d((1 - originalValues[0][0]) * outerRadiusX,
                       (1 - originalValues[0][1]) * outerRadiusY, 0),
            Imath::V3d((1 - originalValues[1][0]) * outerRadiusX,
                       (1 - originalValues[1][1]) * outerRadiusY, 0)
        };

        // final = original + mouse dragged vector
        V3dPair finalActualPoints {
            originalActualPoints[0] + currentActualPoints[0] - previousActualPoints[0],
            originalActualPoints[1] + currentActualPoints[1] - previousActualPoints[1]
        };

        // calculate final shader value
        if (mIndex == Pos::BOTTOM || mIndex == Pos::TOP) {
            finalValues[0][0] = originalValues[0][0];
            finalValues[1][0] = originalValues[1][0];
            finalValues[0][1] = 1.0f - finalActualPoints[0].y / outerRadiusY;
            finalValues[1][1] = 1.0f - finalActualPoints[1].y / outerRadiusY;
        } else {
            finalValues[0][1] = originalValues[0][1];
            finalValues[1][1] = originalValues[1][1];
            finalValues[0][0] = 1.0f - finalActualPoints[0].x / outerRadiusX;
            finalValues[1][0] = 1.0f - finalActualPoints[1].x / outerRadiusX;
        }
        for (int i = 0; i < 2; ++i) {
            for (int j = 0; j < 2; ++j) {
                float& v(finalValues[i][j]);
                if (v == 0) // work around Katana bug where it replaces 0 with garbage
                    v = FLT_MIN;
            }
        }
        // set the final values back
        setShaderAttribute(attrName[0], FnAttribute::FloatAttribute(finalValues[0].data(), 2, 1), isFinal);
        setShaderAttribute(attrName[1], FnAttribute::FloatAttribute(finalValues[1].data(), 2, 1), isFinal);
    }
}

void
BarnDoorsLightFilterManipulatorHandle::cancelManipulation()
{
    if (isDragging()) {
        if (mIndex < 4) {
            setShaderAttribute(mInitialValueAttr[0], false);
        } else {
            std::array<std::string, 2> attrName;
            getAttrName(mIndex, attrName);
            setShaderAttribute(attrName[0], mInitialValueAttr[0], false);
            setShaderAttribute(attrName[1], mInitialValueAttr[1], false);
        }
    }
}

} // end of namespace MoonrayKatana

