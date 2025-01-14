// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/backend/plugin/BackendBase.h>

#include <kodachi/KodachiRuntime.h>
#include <kodachi/Traversal.h>

#include "MoonrayRenderState.h"

#include <arras/rendering/rndr/RenderContext.h>
#include <grid/engine_tool/McrtFbSender.h>

#include <chrono>
#include <memory>
#include <unordered_map>

namespace kodachi_moonray {

class MoonrayRenderBackend : public kodachi::BackendBase
{
public:
    static void flush() {};

    MoonrayRenderBackend();
    MoonrayRenderBackend(const MoonrayRenderBackend&) = delete;
    MoonrayRenderBackend& operator=(const MoonrayRenderBackend&) = delete;

    bool initialize(const kodachi::GroupAttribute& opTreeAttr) override;

    void start() override;
    void stop() override;

    void setData(const kodachi::GroupAttribute& data) override;

    DataMessage getData(const kodachi::GroupAttribute& query) override;

    static MoonrayRenderBackend* create();
    static kodachi::GroupAttribute getStaticData(const kodachi::GroupAttribute& configAttr);

    static kodachi::KdPluginStatus setHost(FnPluginHost* host);

protected:

    static kodachi::GroupAttribute getTerminalOps(const kodachi::GroupAttribute& configAttr);

    void initializeFromRoot(const kodachi::GroupAttribute& rootAttrs,
                            const kodachi::GroupAttribute& opTreeAttr);

    void preTraversal();
    void postTraversal();

    void applyOpTreeDeltas(const kodachi::GroupAttribute& deltasAttr);

    void requestStop();

    void writeRenderOutput();

    bool isStateInitialized() const { return mMoonrayRenderState != nullptr; }

    MoonrayRenderState& getMoonrayRenderState()
    {
        if (!isStateInitialized()) {
            throw std::runtime_error("MoonrayRenderState has not been initialized");
        }

        return *mMoonrayRenderState;
    }

    const MoonrayRenderState& getMoonrayRenderState() const
    {
        if (!isStateInitialized()) {
            throw std::runtime_error("MoonrayRenderState has not been initialized");
        }

        return *mMoonrayRenderState;
    }

    float getRenderProgress() const;

    bool isFrameReadyForDisplay() const;

    bool isFrameRendering() const;

    bool isFrameComplete() const;

    std::string pickPixel(const int x, const int y,
                          const int mode);

    void startFrame();

    void stopFrame();

    DataMessage snapshotBuffers();

    void resetIdPassSnapshotTimer();
    // Send ID Buffer as its own DataMessage
    DataMessage getIdPass();

    bool isMultiMachine() const { return mNumMachines > 1; }

    void syncFbSender();

    /****************************** McrtFbSender ******************************/
    bool mLastSnapshotWasCoarse = true;
    bool mIsProgressiveFrameMode = false;
    arras::engine_tool::McrtFbSender mFbSender;
    uint32_t mSnapshotId = 0;

    // McrtFbSender ignores AOVs of type BEAUTY, and sends the beauty buffer
    // instead. Use the beauty buffer as the BEAUTY AOV by renaming it
    std::string mBeautyRenderOutputName;
    std::string mCryptomatteManifest;

    // For RendererFrame only. The McrtFbSender ignores HEATMAP and ALPHA
    // AOVs, and instead sends the heatmap buffer separately, and the alpha
    // as part of the beauty buffer. Unlikely that users would want multiple
    // of these types of AOVs but its little overhead to support it.
    std::vector<std::string> mTimePerPixelRenderOutputs;
    std::vector<std::string> mAlphaRenderOutputs;
    /**************************************************************************/

    std::unique_ptr<arras::rndr::RenderContext> mRenderContext;
    std::unique_ptr<arras::rndr::RenderOptions> mRenderOptions;

    std::unique_ptr<MoonrayRenderState> mMoonrayRenderState;
    kodachi::GroupAttribute mMoonrayGlobalSettings;

    int mNumThreads = 0;
    int mMachineId = -1;
    int mNumMachines = -1;
    int mDeltaFileCount = 0;

    bool mFirstFrame = true;
    bool mWriteToDisk = false;
    bool mMultiThreadedSceneBuild = true;

    bool mPerformPartialLiveRender = false;
    kodachi::Traversal::PartialLiveRenderMethod mPartialLiveRenderMethod = kodachi::Traversal::PartialLiveRenderMethod::None;

    // Checkpoint rendering
    bool mIsCheckpointActive = false;

    kodachi::StringAttribute mKPOPStateKey;

    kodachi::KodachiRuntime::Ptr mKodachiRuntime;
    kodachi::KodachiRuntime::Op::Ptr mMonitorOp;
    kodachi::KodachiRuntime::Op::Ptr mCookOp;
    std::unique_ptr<kodachi::MonitoringTraversal> mMonitoringTraversal;

    std::chrono::system_clock::time_point mNextIdPassSnapshotTime =
            std::chrono::system_clock::time_point::max();
    std::chrono::milliseconds mIdPassSnapshotDelay;
};

} // namespace kodachi_moonray

