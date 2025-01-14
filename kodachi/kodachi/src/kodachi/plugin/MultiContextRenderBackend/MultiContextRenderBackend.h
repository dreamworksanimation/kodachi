// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// kodachi
#include <kodachi/backend/plugin/BackendBase.h>
#include <kodachi/backend/BackendClient.h>

#include <map>

namespace kodachi {

class MultiContextRenderBackend : public BackendBase
{
public:
    MultiContextRenderBackend();

    bool initialize(const kodachi::GroupAttribute& opTree) override;

    void start() override;
    void stop() override;

    void setData(const kodachi::GroupAttribute& data) override;
    DataMessage getData(const kodachi::GroupAttribute& query) override;

    static MultiContextRenderBackend* create();
    static kodachi::KdPluginStatus setHost(FnPluginHost* host);

protected:
    MultiContextRenderBackend(const MultiContextRenderBackend&) = delete;
    MultiContextRenderBackend& operator=(const MultiContextRenderBackend&) = delete;

    // Returns true if any context is ready for display or rendering
    bool isFrameReadyForDisplay() const;
    bool isFrameRendering() const;

    // Returns true if all contexts are complete
    bool isFrameComplete() const;

    // Tries to snapshot each context and builds a tiled snapshot from the
    // valid individual snapshots.
    DataMessage snapshotBuffers();

    // Merges the registrations from all contexts to handle the case that
    // they have different geometry
    DataMessage getIdRegistrations();

    // Having to build the ID Pass is temporary. Eventually it will be an AOV
    // and be returned with the snapshot
    DataMessage getIdPass();

    struct Context {
        std::string mContextName;
        BackendClient mRenderBackend;
        int32_t mXOffset = 0, mYOffset = 0;
        float mLastProgress = 0.f;
        bool mFrameComplete = false;
    };

    // Map of context name to BackendClient
    std::map<kodachi::string_view, Context> mContexts;

    // The data/region window that contains all of the contexts
    kodachi::IntAttribute mRegionViewport;
};

} // namespace kodachi
