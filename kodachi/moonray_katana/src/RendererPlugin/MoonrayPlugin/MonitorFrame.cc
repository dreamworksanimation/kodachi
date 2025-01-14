// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include "MonitorFrame.h"

// local
#include "MoonrayRenderSettings.h"

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/logging/KodachiLogging.h>

#include <tbb/spin_mutex.h>
#include <unordered_map>

namespace {

KdLogSetup("MfK");

size_t
getPixelSize(const kodachi::StringAttribute& encoding)
{
    static const std::unordered_map<kodachi::StringAttribute, size_t, kodachi::AttributeHash> kEncodingMap
    {
        {"RGB888", 3},
        {"RGBA8" , 4},
        {"FLOAT" , 4},
        {"FLOAT2", 8},
        {"FLOAT3", 12},
        {"INT3"  , 12},
        {"FLOAT4", 16},
    };

    const auto iter = kEncodingMap.find(encoding);

    if (iter != kEncodingMap.end()) {
        return iter->second;
    }

    throw std::runtime_error("Unsupported ImageEncoding type: " + encoding.getValue());
}

size_t
getAdjustedPixelSize(const kodachi::StringAttribute& encoding)
{
    static const kodachi::StringAttribute kFloat2("FLOAT2");
    static const kodachi::StringAttribute kFloat3("FLOAT3");

    if (encoding == kFloat2)
        return getPixelSize(kFloat3);

    return getPixelSize(encoding);
}

// Katana doesn't currently support AOVs with 2 channels - it actually
// completely discards the entire AOV instead of just not displaying it
// which means even our pixel probe enhancements can't view it.
// To get around this, we have to recreate the buffer as a float3 and
// set the B value to 0, which gets us the same functionality as Torch.
// Technically, we could do this conversion in fillMessage(), but
// there are a lot of various checks for stride/pixelSize/offset and
// and it's easier to maintain to just update it here in one place and
// not add random 3-line catches in 5 different functions.
// Foundry is tracking this request under TP 55919
inline std::unique_ptr<uint8_t[]>
padPixels(const uint8_t* row, const size_t initialPixelSize,
          const size_t targetPixelSize, const size_t numPixels)
{
    const size_t rowBytes = targetPixelSize * numPixels;

    auto paddedRow = std::unique_ptr<uint8_t[]>(new uint8_t[rowBytes]);
    const uint8_t* pixIn = row;
    uint8_t* pixOut = paddedRow.get();
    const size_t sizeDiff = targetPixelSize - initialPixelSize;

    for (size_t i = 0; i < numPixels; ++i) {
        std::memcpy(pixOut, pixIn, initialPixelSize);
        std::memset(pixOut + initialPixelSize, 0, sizeDiff);
        pixIn += initialPixelSize;
        pixOut += targetPixelSize;

    }

    return paddedRow;
}

// FnPixelDataDeleter for padded pixels. This data can be deleted normally.
void paddedPixelDeleter(void* pixelData) {
    if (pixelData) {
        std::default_delete<uint8_t[]>()(static_cast<uint8_t*>(pixelData));
    }
}

/**
 * Maps pixel data to the DataAttribute that it belongs to. Take advantage
 * of Attribute reference counting to delete the data when all pixels have
 * been sent to the monitor.
 */
tbb::spin_mutex sPixelDataMutex;
std::unordered_map<const void*, kodachi::Attribute> sPixelDataMap;

void registerPixelData(const void* pixelData, const kodachi::Attribute& dataAttr)
{
    tbb::spin_mutex::scoped_lock lock(sPixelDataMutex);
    sPixelDataMap[pixelData] = dataAttr;
}

void attributeDataDeleter(void* pixelData)
{
    if (pixelData) {
        tbb::spin_mutex::scoped_lock lock(sPixelDataMutex);
        sPixelDataMap.erase(pixelData);
    }
}

} // end anonymous namespace

namespace mfk {

MonitorFrame::MonitorFrame(Foundry::Katana::KatanaPipe* pipe,
                           float frameTime,
                           int64_t frameId,
                           const std::string& frameName)
    : mPipe(pipe)
    , mFrameTime(frameTime)
    , mFrameId(frameId)
    , mFrameName(frameName)
{
}

bool
MonitorFrame::sendNewFrameMessage()
{
    // Start with new frame /////////////////////////////
    mFrameMsg.reset(new FnKat::NewFrameMessage());

    // Frame Time...
    mFrameMsg->setFrameTime(mFrameTime);

    // Frame dimensions
    // This should use the format aperture.
    // For now assume it is at 0,0 with equal-sized borders on all sides in displayWindow
    const auto displayWindow = mDisplayWindowAttr.getNearestSample(0.f);
    mFrameMsg->setFrameOrigin(0, 0);
    mFrameMsg->setFrameDimensions(displayWindow[2] + displayWindow[0],
                                  displayWindow[3] + displayWindow[1]);

    std::string encodedFrameName;
    FnKat::encodeLegacyName(mFrameName, mFrameId, encodedFrameName);
    mFrameMsg->setFrameName(encodedFrameName);

    KdLogDebug("sendNewFrameMessage - " << mFrameMsg->frameName()
               << ", XOrigin: " << mFrameMsg->frameXOrigin()
               << ", YOrigin: " << mFrameMsg->frameYOrigin()
               << ", Width: "   << mFrameMsg->frameWidth()
               << ", Height: "  << mFrameMsg->frameHeight());

    // Send it
    auto sendResult = mPipe->send(*mFrameMsg);
    if (sendResult != 0) {
        // log error
        KdLogError("Couldn't send mFrameMsg " << mFrameMsg->frameName());
        return false;
    }

    return true;
}

MonitorFrame::~MonitorFrame()
{
    flush();
    for (auto& chanMsg : mChannels) {
        mPipe->closeChannel(*chanMsg.second);
    }
}

void
MonitorFrame::resendChannelMessages()
{
    const auto dataWindow = mDataWindowAttr.getNearestSample(0.f);
    // Re-send the NewChannelMessage with the updated dimensions. I think this
    // is fine? Not sure if there are any memory considerations here.
    for (auto& channel : mChannels) {
        channel.second->setChannelOrigin(dataWindow[0], dataWindow[1]);
        channel.second->setChannelDimensions(dataWindow[2], dataWindow[3]);
        mPipe->send(*channel.second);
    }
}

Foundry::Katana::NewChannelMessage_v2*
MonitorFrame::getChannelMessage(const MChannelInfo* chanInfo,
                         const uint32_t pixelSize)
{
    tbb::spin_mutex::scoped_lock lock(mChannelsMutex);
    auto iter = mChannels.find(chanInfo);
    if (iter != mChannels.end()) {
        return iter->second.get();
    }

    lock.release();

    KdLogDebug("Creating channel for " << chanInfo->getReturnName());

    // new channels ///////////////////////////////////////////
    auto newChannelMsg = std::unique_ptr<FnKat::NewChannelMessage_v2>(
                                   new FnKat::NewChannelMessage_v2(*mFrameMsg));
    if (chanInfo->isBeauty()) {
        newChannelMsg->setPixelLayout(FnKat::NewChannelMessage_v2::PixelLayout::RGBA);
    }

    // Set channelID & dimensions
    const uint16_t chanID = chanInfo->getBufferId();

    const auto dataWindow = mDataWindowAttr.getNearestSample(0.f);

    newChannelMsg->setChannelID(chanID);
    newChannelMsg->setChannelOrigin(dataWindow[0], dataWindow[1]);
    // setChannelDimensions claims to take in a width and height, but my
    // experience has been that you're supposed to pass it the right and top
    // borders. Actual width would be mDataWindow[2] - mDataWindow[0].
    newChannelMsg->setChannelDimensions(dataWindow[2], dataWindow[3]);

    // Set sample rate and size
    constexpr float sampleRate[2] {1.0, 1.0};
    newChannelMsg->setSampleRate(sampleRate);
    // Upsize long to float
    newChannelMsg->setDataSize(pixelSize);

    // Encode Channel Name
    // Even though the documentation says that ChannelName is the "human readable"
    // name, Katana will crash if you don't encode it. Use the channelID set in
    // the NewChannelMessage as the ID in the encoding, except in the case of the
    // ID pass, where you have to use ID of the frame (or maybe it's the ID of the
    // primary channel, which is always the same as the frameID anyway)
    std::string channelName;
    FnKat::encodeLegacyName(chanInfo->getReturnName(),
                            chanInfo->getChannelType() == MChannelInfo::ChannelType::ID ? mFrameId : chanID,
                            channelName);
    newChannelMsg->setChannelName(channelName);

    lock.acquire(mChannelsMutex);

    // Attempt to add this new channel message
    // Its possible another thread already did
    const auto emplaceResult = mChannels.emplace(chanInfo, std::move(newChannelMsg));

    // Either way there will be a message, but we should only send it if
    // insertion was successful
    Foundry::Katana::NewChannelMessage_v2* msg = emplaceResult.first->second.get();

    if (emplaceResult.second) {
        // Send it
        const int sendResult = mPipe->send(*msg);
        if (sendResult != 0) {
            // log error
            KdLogError("Couldn't send channelObject "
                             << msg->channelName());
            return nullptr;
        }

        KdLogDebug("Added channel '" << chanInfo->getReturnName()
                   << "' (" << channelName << ", "
                   << chanInfo->getMoonrayChannelName() << ")"
                   << ", XOrigin: " << msg->channelXOrigin()
                   << ", YOrigin: " << msg->channelYOrigin()
                   << ", Width: "   << msg->channelWidth()
                   << ", Height: "  << msg->channelHeight()
                   << ", dataSize" << msg->channelDataSize());
    }

    return msg;
}

void
MonitorFrame::flush()
{
    tbb::spin_mutex::scoped_lock lock(mChannelsMutex);
    for (auto& chanMsg : mChannels) {
        mPipe->flushPipe(*chanMsg.second);
    }
}

void
MonitorFrame::sendRenderSnapshot(const MoonrayRenderSettings& renderSettings,
                                 const kodachi::GroupAttribute& snapshotAttr)
{
    const kodachi::IntAttribute apertureWindowAttr = snapshotAttr.getChildByName("avp");
    const kodachi::IntAttribute regionWindowAttr = snapshotAttr.getChildByName("rvp");

    if (mDisplayWindowAttr != apertureWindowAttr
            || mDataWindowAttr != regionWindowAttr) {
        mDisplayWindowAttr = apertureWindowAttr;
        mDataWindowAttr = regionWindowAttr;

        if (!mFrameMsg) {
            sendNewFrameMessage();
        } else {
            resendChannelMessages();
        }
    }

    const kodachi::GroupAttribute tilesAttr = snapshotAttr.getChildByName("tiles");
    if (tilesAttr.isValid()) {
        for (const auto tile : tilesAttr) {
            const kodachi::GroupAttribute tileAttr = tile.attribute;

            const kodachi::GroupAttribute buffersAttr =
                    tileAttr.getChildByName("bufs");

            const kodachi::IntAttribute viewportAttr =
                    tileAttr.getChildByName("vp");

            const kodachi::IntAttribute flippedVAttr =
                    tileAttr.getChildByName("flippedV");

            const bool isFlipped = flippedVAttr.getValue(false, false);

            sendBuffers(renderSettings, viewportAttr, buffersAttr, isFlipped);
        }
    }

    const kodachi::GroupAttribute buffersAttr = snapshotAttr.getChildByName("bufs");
    if (buffersAttr.isValid()) {
        const kodachi::IntAttribute subviewportAttr =
                snapshotAttr.getChildByName("svp");

        const kodachi::IntAttribute flippedVAttr =
                snapshotAttr.getChildByName("flippedV");

        const bool isFlipped = flippedVAttr.getValue(false, false);

        sendBuffers(renderSettings, subviewportAttr, buffersAttr, isFlipped);
    }
}

void
MonitorFrame::sendBuffers(const MoonrayRenderSettings& renderSettings,
                          const kodachi::IntAttribute& viewportAttr,
                          const kodachi::GroupAttribute& buffersAttr,
                          bool isFlipped)
{
    // subViewport is in coordinates relative to regionWindow.
    const auto dataWindow = mDataWindowAttr.getNearestSample(0.f);
    const auto viewport = viewportAttr.getNearestSample(0.f);

    const int frameHeight = dataWindow[3] - dataWindow[1];
    const int frameYMax = frameHeight - 1;

    const int xMin = viewport[0];
    const int yMin = viewport[1];
    const int yMax = viewport[3] - 1;
    const int dataWidth  = viewport[2] - viewport[0];
    const int dataHeight = viewport[3] - viewport[1];

    for (const auto buffer : buffersAttr) {
        const std::string decodedBufferName = kodachi::DelimiterDecode(std::string(buffer.name));

        auto channelInfo = renderSettings.getChannelByLocationPath(decodedBufferName);
        if (!channelInfo) {
            KdLogWarn("No channel found for buffer: " << decodedBufferName);
        } else {
            const kodachi::GroupAttribute bufferAttr = buffer.attribute;

            const kodachi::DataAttribute dataAttr =
                    bufferAttr.getChildByName("data");
            if (!dataAttr.isValid()) {
                KdLogError("Buffer missing 'data' attribute. "
                           << "Payload-based buffers are not currently handled. Skipping: "
                           << buffer.name);
                continue;
            }

            const kodachi::StringAttribute encodingType =
                    bufferAttr.getChildByName("enc");

            const size_t initialPixelSize = getPixelSize(encodingType);
            const size_t adjustedPixelSize = getAdjustedPixelSize(encodingType);

            auto chanMsg = getChannelMessage(channelInfo, adjustedPixelSize);
            if (chanMsg) {
                sendData(chanMsg, dataAttr,
                         initialPixelSize, adjustedPixelSize,
                         xMin, yMin, yMax, dataWidth, dataHeight, frameYMax, isFlipped);
            }
        }
    }
}

void
MonitorFrame::sendData(Foundry::Katana::NewChannelMessage_v2* chanMsg,
                       const kodachi::DataAttribute& dataAttr,
                       const size_t initialPixelSize,
                       const size_t adjustedPixelSize,
                       int xMin, int yMin, int yMax, int dataWidth,
                       int dataHeight, int frameYMax, bool isFlipped)
{
    const uint8_t* data = nullptr;
    std::size_t dataLength = 0;

    switch(dataAttr.getType()) {
    case kodachi::kAttrTypeFloat:
    {
        const auto floatSamples = kodachi::FloatAttribute(dataAttr).getSamples();
        data = reinterpret_cast<const uint8_t*>(floatSamples.front().data());
        dataLength = floatSamples.getNumberOfValues() * sizeof(float);
        break;
    }
    case kodachi::kAttrTypeInt:
    {
        const auto intSamples = kodachi::IntAttribute(dataAttr).getSamples();
        data = reinterpret_cast<const uint8_t*>(intSamples.front().data());
        dataLength = intSamples.getNumberOfValues() * sizeof(int32_t);
        break;
    }
    default:
        KdLogError("Buffer is not int or float, skipping");
        return;
    }

    // Sanity check, since the data came from a DataBuffer instead of a
    // PixelBuffer, make sure it's all there
    const uint32_t expectedDataLength = initialPixelSize * dataWidth * dataHeight;
    if (expectedDataLength != dataLength) {
        KdLogWarn("Unexpected Data Length, expected: "
                         << expectedDataLength << ", actual: " << dataLength);
        return;
    }

    const uint32_t initialRowSize = initialPixelSize * dataWidth;
    const uint32_t adjustedRowSize = adjustedPixelSize * dataWidth;

    if (isFlipped) {
        FnKat::DataMessage dataMsg(*chanMsg,
                                   xMin,
                                   dataWidth,
                                   frameYMax - yMin - dataHeight + 1,
                                   dataHeight,
                                   adjustedPixelSize);

        // we can send the whole buffer at once
        if (initialPixelSize != adjustedPixelSize) {
            auto paddedData = padPixels(data, initialPixelSize, adjustedPixelSize, dataWidth * dataHeight);
            auto paddedDataLength = adjustedPixelSize * dataWidth * dataHeight;
            dataMsg.setData(paddedData.release(), paddedDataLength, paddedPixelDeleter);
        } else {
            registerPixelData(data, dataAttr);
            dataMsg.setData(data, dataLength, attributeDataDeleter);
        }

        mPipe->send(dataMsg);
    } else {
        for (int i = yMin; i <= yMax; ++i) {
            FnKat::DataMessage dataMsg(*chanMsg,
                                       xMin,
                                       dataWidth,
                                       frameYMax - i,
                                       1, // height
                                       adjustedPixelSize);

            FnPixelDataDeleter dataDeleter;
            if (initialPixelSize != adjustedPixelSize) {
                auto paddedRow = padPixels(data, initialPixelSize, adjustedPixelSize, dataWidth);
                dataMsg.setData(paddedRow.release(), adjustedRowSize, paddedPixelDeleter);
            } else {
                registerPixelData(data, dataAttr);
                dataMsg.setData(data, adjustedRowSize, attributeDataDeleter);
            }

            mPipe->send(dataMsg);

            data += initialRowSize;
        }
    }

    mPipe->flushPipe(*chanMsg);

    return;
}

} /* namespace mfk */

