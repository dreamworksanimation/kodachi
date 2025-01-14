// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/attribute/Attribute.h>
#include <kodachi/KodachiRuntime.h>
#include <scene_rdl2/scene/rdl2/SceneObject.h>

#include <kodachi/plugin_system/PluginManager.h>
#include <tbb/task_arena.h>

namespace kodachi_moonray {

class KodachiRuntimeWrapper : public arras::rdl2::SceneObject
{
public:
    typedef arras::rdl2::SceneObject Parent;

    KodachiRuntimeWrapper(const arras::rdl2::SceneClass& sceneClass, const std::string& name)
    : Parent(sceneClass, name)
    {}

    static kodachi::KdPluginStatus setHost(kodachi::KdPluginHost* host);

    void setOpTree(const kodachi::GroupAttribute& opTreeAttr);

    class ClientWrapper
    {
    public:
        ClientWrapper(const kodachi::KodachiRuntime::Client::Ptr& client, bool flushPluginCaches)
        : mKodachiClient(client)
        , mFlushPluginCaches(flushPluginCaches)
        {}

        ~ClientWrapper();

        kodachi::GroupAttribute
        cookLocation(const std::string& location);

    private:
        const kodachi::KodachiRuntime::Client::Ptr mKodachiClient;
        const bool mFlushPluginCaches = true;
        tbb::task_arena mArena;
    };

    using ClientWrapperPtr = std::shared_ptr<ClientWrapper>;
    using ClientWrapperWeakPtr = std::weak_ptr<ClientWrapper>;

    /**
     * This should be called by all KodachiGeometry objects during their
     * update() call.
     */
    ClientWrapperPtr getClientWrapper() const;

private:
    mutable kodachi::KodachiRuntime::Ptr mKodachiRuntime;

    mutable std::mutex mClientCreationMutex;
    mutable ClientWrapperWeakPtr mClientWeakPtr;
};

} // namespace kodachi_moonray

