// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <arras/rendering/geom/ProceduralContext.h>
#include <kodachi/attribute/Attribute.h>

#include <OpenEXR/ImathMatrix.h>

namespace kodachi_moonray {

struct MotionBlurData
{
    std::vector<float> mMotionSteps;
    bool mUseVelocity = false;
    bool mUseAcceleration = false;
};

MotionBlurData
computeMotionBlurData(const arras::geom::GenerateContext& generateContext,
                      arras::rdl2::MotionBlurType motionBlurType,
                      bool pos1Valid, bool vel0Valid, bool vel1Valid, bool acc0Valid)
{
    // Fall back on static case if we don't have sufficient data for requested mb type
    bool useVelocity = false;
    bool useAcceleration = false;

    std::vector<float> motionSteps = generateContext.getMotionSteps();

    bool err = false;
    if (!generateContext.isMotionBlurOn() || motionSteps.size() != 2) {
        motionBlurType = arras::rdl2::MotionBlurType::STATIC;
    }

    switch (motionBlurType) {

    case arras::rdl2::MotionBlurType::STATIC:
    {
        motionSteps = {0.f};
        break;
    }

    case arras::rdl2::MotionBlurType::VELOCITY:
    {
        if (vel0Valid) {
            useVelocity = true;
            motionSteps = {0.f};
        } else {
            err = true;
        }
        break;
    }

    case arras::rdl2::MotionBlurType::FRAME_DELTA:
    {
        if (!pos1Valid) {
            err = true;
        }
        break;
    }

    case arras::rdl2::MotionBlurType::ACCELERATION:
    {
        if (vel0Valid && acc0Valid) {
            useVelocity = true;
            useAcceleration = true;
            motionSteps = {0.f};
        } else {
            err = true;
        }
        break;
    }

    case arras::rdl2::MotionBlurType::HERMITE:
    {
        if (pos1Valid && vel0Valid && vel1Valid) {
            useVelocity = true;
        } else {
            err = true;
        }
        break;
    }

    case arras::rdl2::MotionBlurType::BEST:
    {
        if (pos1Valid && vel0Valid && vel1Valid) {
            // use Hermite mb type
            useVelocity = true;
        } else if (vel0Valid && acc0Valid) {
            // use acceleration mb type
            useVelocity = true;
            useAcceleration = true;
            motionSteps = {0.f};
        } else if (pos1Valid) {
            // use frame delta mb type
        } else if (!pos1Valid && vel0Valid) {
            // use velocity mb type
            useVelocity = true;
            motionSteps = {0.f};
        } else {
            // just keep static mb type
            motionSteps = {0.f};
        }
        break;
    }

    default:
    {
        err = true;
        break;
    }

    } // end of switch statement

    if (err) {
        generateContext.getRdlGeometry()->warn("Insufficient data for requested motion blur type. "
                                               "Falling back to static case.");
        motionSteps = {0.f};
    }

    MotionBlurData mbd;
    mbd.mMotionSteps = std::move(motionSteps);
    mbd.mUseVelocity = useVelocity;
    mbd.mUseAcceleration = useAcceleration;

    return mbd;
}

arras::rdl2::Vec2fVector
toVec2fVector(const kodachi::FloatAttribute::array_type& floatArray) {
    static_assert(std::is_same<arras::rdl2::Vec2fVector::value_type::Scalar, kodachi::FloatAttribute::value_type>::value,
            "Vec2fVector and FloatAttribute are both float containers");

    arras::rdl2::Vec2fVector vec(floatArray.size() / 2);

    std::memcpy(vec.data(), floatArray.data(), floatArray.size() * sizeof(float));

    return vec;
}

arras::rdl2::Vec3fVector
toVec3fVector(const kodachi::FloatAttribute::array_type& floatArray) {
    static_assert(std::is_same<arras::rdl2::Vec3fVector::value_type::Scalar, kodachi::FloatAttribute::value_type>::value,
            "Vec3fVector and FloatAttribute are both float containers");

    arras::rdl2::Vec3fVector vec(floatArray.size() / 3);

    std::memcpy(vec.data(), floatArray.data(), floatArray.size() * sizeof(float));

    return vec;
}

inline void
setXformMatrix(Imath::M44d& mat, const double* arr)
{
    if (arr) {
        mat[0][0] = arr[0];
        mat[0][1] = arr[1];
        mat[0][2] = arr[2];
        mat[0][3] = arr[3];
        mat[1][0] = arr[4];
        mat[1][1] = arr[5];
        mat[1][2] = arr[6];
        mat[1][3] = arr[7];
        mat[2][0] = arr[8];
        mat[2][1] = arr[9];
        mat[2][2] = arr[10];
        mat[2][3] = arr[11];
        mat[3][0] = arr[12];
        mat[3][1] = arr[13];
        mat[3][2] = arr[14];
        mat[3][3] = arr[15];
    }
}

} // namespace kodachi_moonray

