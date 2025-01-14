// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

// local
#include "MoonrayRenderSettings.h"

// kodachi
#include <kodachi/backend/BackendClient.h>

// katana
#include <FnDisplayDriver/FnKatanaDisplayDriver.h>

// tbb
#include <tbb/spin_mutex.h>

// local
#include <map>
#include <memory>

namespace Foundry {
namespace Katana {
class KatanaPipe;
class NewFrameMessage;
class NewChannelMessage_v2;
}
}

namespace mfk {

/**
 * Holder/helper for Katana Monitor frame sending.
 * Upon construction, starts a new frame, and sets up the
 * given channel buffers. Also covers taking a Kodachi render
 * buffer and sending it over.
 */
class MonitorFrame {
public:
    /**
     * Constructor. Starts a new frame and all given channels.
     * All channels have the same size and origin, and are all 4-floats per pixel.
     * Holds onto the pipe for convenience.
     */
    MonitorFrame(Foundry::Katana::KatanaPipe* pipe, float frameTime,
                 int64_t frameId, const std::string &frameName);
    virtual ~MonitorFrame();

    void flush();

    void sendRenderSnapshot(const MoonrayRenderSettings& renderSettings,
                            const kodachi::GroupAttribute& snapshotAttr);

protected:

    bool sendNewFrameMessage();
    void resendChannelMessages();

    Foundry::Katana::NewChannelMessage_v2*
    getChannelMessage(const MChannelInfo* chanInfo, const uint32_t pixelSize);

    void sendBuffers(const MoonrayRenderSettings& renderSettings,
                     const kodachi::IntAttribute& viewport,
                     const kodachi::GroupAttribute& buffers,
                     bool isFlipped);
    void sendData(Foundry::Katana::NewChannelMessage_v2* chanMsg,
                  const kodachi::DataAttribute& dataAttr, const size_t initialPixelSize,
                  const size_t adjustedPixelSize, int xMin, int yMin, int yMax,
                  int dataWidth, int dataHeight, int frameYMax, bool isFlipped);

    // Members for communicating back to the Monitor
    // We don't own the pipe, but we own the frame and channels messages
    Foundry::Katana::KatanaPipe* mPipe;
    const float mFrameTime;
    const int64_t mFrameId;
    const std::string mFrameName;

    std::unique_ptr<Foundry::Katana::NewFrameMessage> mFrameMsg;

    std::map<const MChannelInfo*, std::unique_ptr<Foundry::Katana::NewChannelMessage_v2>> mChannels;
    tbb::spin_mutex mChannelsMutex;

    kodachi::IntAttribute mDisplayWindowAttr;
    kodachi::IntAttribute mDataWindowAttr;
};

} /* namespace mfk */

