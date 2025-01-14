// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

// local
#include "MoonrayRender.h"
#include "MoonrayRenderSettings.h"
#include "MonitorFrame.h"

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/backend/BackendClient.h>
#include <kodachi/KodachiRuntime.h>

// katana
#include <FnRender/plugin/IdSenderInterface.h>

// system
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace mfk {

enum class KatanaRenderMethod {
    PREVIEW,
    LIVE,
    DISK
};

KatanaRenderMethod parseMethod(const std::string& methodName);
const char* toString(KatanaRenderMethod krm);

class KodachiRenderMethod
{
public:
    KodachiRenderMethod(MoonrayRender* source,
                        KatanaRenderMethod renderMethod,
                        const kodachi::GroupAttribute& opTreeMessage,
                        const kodachi::StringAttribute& debugFile);
    virtual ~KodachiRenderMethod();

    int start();
    int pause()  { return 0; }
    int resume() { return 0; }
    int stop()   { return 0; }

    void queueDataUpdates(const Foundry::Katana::GroupAttribute& updateAttribute);
    void applyPendingDataUpdates();

    const MoonrayRenderSettings& getRenderSettings() const { return mRenderSettings; }

protected:
    bool isPreviewRender() const;
    bool isLiveRender() const;
    bool isDiskRender() const;

    int calculateNumTbbThreads();

    void initializeRenderBackend(const kodachi::GroupAttribute& opTreeMessage);
    kodachi::GroupAttribute buildRenderBackendOpTree(const kodachi::GroupAttribute& rootAttrs,
                                                     const kodachi::GroupAttribute& opTreeAttr);

    int onRenderStarted();

    // snapshot loops for the different types of Katana render methods
    int onDiskRenderStarted();
    int onPreviewRenderStarted();
    int onLiveRenderStarted();

    /**
     * Called during the snapshot loop when the frame is ready for snapshotting
     * Snapshots the buffers from the RenderBackend and sends over the KatanaPipe
     * Returns true if frame is complete
     */
    bool onFrameReadyForSnapshot();

    void sendIdRegistrations();

    kodachi::StringAttribute resolveFileSequence(const kodachi::StringAttribute& fileSequence);

    std::string mActiveContextId;

    MoonrayRender* mSourceBase;
    // We don't need to condition but we still need the
    // render settings for the channel information
    MoonrayRenderSettings mRenderSettings;
    kodachi::GroupAttribute mGlobalSettings;
    const KatanaRenderMethod mKatanaRenderMethod;

    kodachi::BackendClient mRenderBackend;
    bool mIsMultiContext = false;

    const int mNumTbbThreads;

    // For LiveRender updates
    mutable std::mutex mDataUpdateMutex;
    std::condition_variable mDataUpdateCondition;
    std::vector<kodachi::GroupAttribute> mDataUpdates;

    // time to wait between frame snapshots
    std::chrono::milliseconds mSnapshotInterval;

    bool mSkipRender = false;
    std::unique_ptr<FnKat::Render::IdSenderInterface> mIdSender;
    kodachi::StringAttribute mDebugOutputFile;
    kodachi::StringAttribute mRezContextFile;

    std::unique_ptr<MonitorFrame> mCurrentFrame;
};

} /* namespace mfk */

