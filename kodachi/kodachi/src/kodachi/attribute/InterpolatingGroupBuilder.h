// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>

namespace kodachi {

/**
 * Wrapper around GroupBuilder that interpolates attributes with multiple
 * time samples.
 */
class InterpolatingGroupBuilder
{
public:

    InterpolatingGroupBuilder(float shutterOpen, float shutterClose);
    InterpolatingGroupBuilder(float shutterOpen, float shutterClose,
                              GroupBuilder::BuilderMode builderMode);

    void reset();

    bool isValid() const;

    // Interpolates multi-samples attributes to shutterOpen. Otherwise sets
    // the unmodified single-sampled attribute.
    InterpolatingGroupBuilder& set(const kodachi::string_view& path,
                                   const kodachi::Attribute& attr,
                                   bool groupInherit = true);

    // Interpolates multi-samples attributes to shutterOpen and ShutterClose
    // Otherwise sets the unmodified single-sampled attribute.
    InterpolatingGroupBuilder& setBlurrable(const kodachi::string_view& path,
                                            const kodachi::Attribute& attr,
                                            bool groupInherit = true);

    // Sets the passed in attribute without any interpolation or modification
    InterpolatingGroupBuilder& setWithoutInterpolation(const kodachi::string_view& path,
                                                       const kodachi::Attribute& attr,
                                                       bool groupInherit = true);

    InterpolatingGroupBuilder& del(const kodachi::string_view& path);

    InterpolatingGroupBuilder& update(const kodachi::GroupAttribute& attr);

    InterpolatingGroupBuilder& deepUpdate(const kodachi::GroupAttribute& attr);

    InterpolatingGroupBuilder& reserve(int64_t n);

    InterpolatingGroupBuilder& setGroupInherit(bool groupInherit);

    InterpolatingGroupBuilder& sort();

    GroupAttribute build(GroupBuilder::BuilderBuildMode builderMode = GroupBuilder::BuildAndFlush);
private:
    kodachi::GroupBuilder mGb;
    const float mShutterOpen;
    const float mShutterClose;
    const bool mbEnabled;
};

} // namespace kodachi

