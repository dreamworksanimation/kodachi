// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "LightDelegateComponent.h"

#include <FnAttribute/FnGroupBuilder.h>
#include <FnGeolib/util/Path.h>
#include <FnViewer/plugin/FnViewport.h>
#include <FnViewer/utils/FnImathHelpers.h>

#include "Drawables/CameraDrawable.h"
#include "Drawables/LightDrawable.h"
#include "Drawables/LightFilterDrawable.h"

using namespace Foundry::Katana::ViewerAPI;
using namespace FnAttribute;

const char LightDelegateComponent::NAME[] = "MoonrayLightDelegateComponent";

namespace {
const std::string kLight = "light";
const std::string kLightFilter = "light filter";
const std::string kLightFilterReference = "light filter reference";
const std::string kCamera = "camera";

// true if prefix names a parent, gp, etc of s
bool isAncestor(const std::string& prefix, const std::string& s) {
    return prefix.size() < s.size() && s.compare(0, prefix.size(), prefix) == 0 && s[prefix.size()] == '/';
}

}

void
LightDelegateComponent::dirtyAllViewports()
{
    for (unsigned int i = 0; i < getViewerDelegate()->getNumberOfViewports(); ++i) {
        getViewerDelegate()->getViewport(i)->setDirty(true);
    }
}

bool
LightDelegateComponent::locationEvent(const ViewerLocationEvent& event, bool locationHandled)
{
    if (event.stateChanges.locationRemoved) {
        bool dirty = false;
        for (auto&& i = mDrawables.lower_bound(event.locationPath);
             i != mDrawables.end() && isAncestor(event.locationPath, i->first);) {
            i = mDrawables.erase(i);
            dirty = true;
        }

        if (dirty) dirtyAllViewports();
        return false;
    }

    auto type = StringAttribute(event.attributes.getChildByName("type"));
    if (locationHandled) {
        return false;
    } else if (type != kLight && type != kLightFilter && type != kLightFilterReference && type != kCamera) {
        return false;
    }

    std::unique_ptr<MoonrayKatana::Drawable>& drawable(mDrawables[event.locationPath]);

    if (!drawable) {
        if (type == kLight) {
            drawable.reset(new MoonrayKatana::LightDrawable(event.locationPath));
        } else if (type == kLightFilter || type == kLightFilterReference) {
            MoonrayKatana::LightDrawable* parentDrawable = nullptr;
            auto iter = mDrawables.find(FnKat::Util::Path::GetLocationParent(event.locationPath));
            if (iter != mDrawables.end())
                parentDrawable = dynamic_cast<MoonrayKatana::LightDrawable*>(iter->second.get());
            drawable.reset(MoonrayKatana::LightFilterDrawable::create(
                    parentDrawable, event.locationPath, event.attributes));
        } else if (type == kCamera) {
            drawable.reset(new MoonrayKatana::CameraDrawable(event.locationPath));
        }

        if (!drawable) {
            mDrawables.erase(event.locationPath);
            return false;
        }
    }

    if (event.stateChanges.excludedChanged) {
        drawable->mHidden = event.excluded;
    }

    if (event.stateChanges.attributesUpdated) {
        drawable->setup(event.attributes);

        // Notify all children that this changed
        auto&& i = mDrawables.lower_bound(event.locationPath);
        // Increment once to skip itself
        ++i;
        for (; i != mDrawables.end() && isAncestor(event.locationPath, i->first); ++i) {
            i->second->ancestorChanged(drawable.get());
        }

        if (type == kLight || type == kCamera) {
            auto worldXform = getViewerDelegate()->getWorldXform(event.locationPath).data;
            // Correct centerOfInterest length to be in local space
            double a = worldXform[8];
            double b = worldXform[9];
            double c = worldXform[10];
            double s = sqrt(a*a+b*b+c*c);
            double coi = DoubleAttribute(event.attributes.getChildByName(
                    "geometry.centerOfInterest")).getValue(20.0, false) / s;
            if (type == kLight) {
                static_cast<MoonrayKatana::LightDrawable*>(drawable.get())->mCenterOfInterest = coi;
            } else {
                static_cast<MoonrayKatana::CameraDrawable*>(drawable.get())->mCenterOfInterest = coi;
            }
        }
    }

    dirtyAllViewports();

    return true;
}

void
LightDelegateComponent::locationsSelected(const std::vector<std::string>& locations)
{
    if (mDrawables.empty()) return;
    for (auto&& i : mDrawables)
        if (i.second)
            i.second->mSelected = i.second->mAncestorSelected = i.second->mChildSelected = false;
    for (const std::string& path : locations) {
        if (path.empty()) continue; // bug in Katana?
        for (auto&& i : mDrawables) {
            if (i.second) {
                if (i.first == path) {
                    i.second->mSelected = true;
                } else if (isAncestor(path, i.first)) {
                    i.second->mAncestorSelected = true;
                } else if (isAncestor(i.first, path)) {
                    i.second->mChildSelected = true;
                }
            }
        }
    }
    dirtyAllViewports();
}

DoubleAttribute
LightDelegateComponent::getBounds(const std::string& location)
{
    auto iter = mDrawables.find(location);
    if (iter != mDrawables.end())
        return iter->second->getBounds();
    else
        return {};
}

DoubleAttribute
LightDelegateComponent::computeExtent(const std::string& location)
{
    auto iter = mDrawables.find(location);
    if (iter != mDrawables.end())
        return iter->second.get()->getExtent();
    else
        return {};
}

