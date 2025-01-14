// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/attribute/Attribute.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <kodachi_moonray/kodachi_runtime_wrapper/KodachiRuntimeWrapper.h>

namespace kodachi_moonray {

class KodachiGeometry : public arras::rdl2::Geometry
{
public:
    typedef arras::rdl2::Geometry Parent;

    KodachiGeometry(const arras::rdl2::SceneClass& sceneClass, const std::string& name)
    : Parent(sceneClass, name)
    {}

    void update() override
    {
        if (mKodachiAttr.isValid()) {
            return;
        }

        const arras::rdl2::SceneObject* runtimeObject =
                get<arras::rdl2::SceneObject*>("kodachi_runtime");

        if (!runtimeObject) {
            error("No Attribute or kodachi_runtime specified");
            return;
        }

        mClientWrapper = static_cast<const kodachi_moonray::KodachiRuntimeWrapper*>(runtimeObject)->getClientWrapper();
    }

    mutable kodachi::GroupAttribute mKodachiAttr;
    mutable KodachiRuntimeWrapper::ClientWrapperPtr mClientWrapper;
    bool mReleaseAttr = true;
    bool mDeformed = false;
};

} // namespace kodachi_moonray

