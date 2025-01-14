// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/attribute/Attribute.h>

namespace kodachi_moonray {
namespace motion_blur_util {

/**
 * Takes the position, and optionally velocity and acceleration data from
 * a kodachi geometry location and creates the correctly named and interpolated
 *  attributes for Rdl*Geometry procedurals.
 *
 * Possible Attributes in return value:
 * - errorMessage (optional)
 * - warningMessage (optional)
 * - motionBlurType
 * - attrs.vertex_list_0
 * - attrs.vertex_list_1
 * - attrs.velocity_list_0
 * - attrs.velocity_list_1
 * - attrs.acceleration_list
 */
kodachi::GroupAttribute
createMotionBlurAttributes(const kodachi::Attribute& motionBlurTypeAttr,
                           const kodachi::FloatAttribute& positionAttr,
                           const kodachi::FloatAttribute& velocityAttr,
                           const kodachi::Attribute& accelerationAttr,
                           float shutterOpen, float shutterClose, float fps);

/**
 * Returns data in the same format as the above function but when it is
 * known that static motion blur is being used (such as when motion blur is
 * disabled globally)
 */
kodachi::GroupAttribute
createStaticMotionBlurAttributes(const kodachi::FloatAttribute& positionAttr);

} // namespace motion_blur_util
} // namespace kodachi_moonray

