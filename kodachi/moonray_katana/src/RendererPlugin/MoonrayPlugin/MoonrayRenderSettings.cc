// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayRenderSettings.h"

#include <kodachi/logging/KodachiLogging.h>

namespace {
KdLogSetup("MfK");
}

#include <cmath>
#include <iostream>
#include <limits>
#include <mutex>
#include <set>

namespace mfk {

namespace internal {

RenderSettings::RenderSettings()
{}

void RenderSettings::initialize(const FnAttribute::GroupAttribute& renderSettingsAttr)
{
    if(!renderSettingsAttr.isValid())
        return;

    // Indicate that there is a valid renderSettings attribute which is used to
    // initialise one or more render setting variables.
    _valid = true;

    FnAttribute::StringAttribute rendererAttr = renderSettingsAttr.getChildByName("renderer");
    if(rendererAttr.isValid())
        _renderer = rendererAttr.getValue();

    // Extract xRes and yRes from the Resolution Table
    FnAttribute::IntAttribute xyResAttr = renderSettingsAttr.getChildByName("xyRes");
    if(xyResAttr.isValid())
    {
        FnAttribute::IntConstVector xyRes = xyResAttr.getNearestSample(0);
        _xRes = xyRes[0];
        _yRes = xyRes[1];
    }
    else
    {
        _xRes = 512;
        _yRes = 512;
    }

    FnAttribute::FloatAttribute sampleRateAttr = renderSettingsAttr.getChildByName("sampleRate");
    if(sampleRateAttr.isValid())
    {
        FnAttribute::FloatConstVector sampleRate = sampleRateAttr.getNearestSample(0);
        _sampleRate[0] = sampleRate[0];
        _sampleRate[1] = sampleRate[1];
    }
    else
    {
        _sampleRate[0] = 0.0f;
        _sampleRate[1] = 0.0f;
    }

    // Reset overscan
    _overscan[0] = 0;
    _overscan[1] = 0;
    _overscan[2] = 0;
    _overscan[3] = 0;

    int overscan;
    bool fillOverscanWithSingleValue = false;
    // This causes problems if the value is an int!
    FnAttribute::FloatAttribute overscanAttr = renderSettingsAttr.getChildByName("overscan");
    FnAttribute::IntAttribute overscanIntAttr = renderSettingsAttr.getChildByName("overscan");
     if(overscanAttr.isValid())
     {
        FnAttribute::FloatConstVector overScanVector = overscanAttr.getNearestSample(0);
        if(overScanVector.size() > 1)
        {
            for(size_t i=0; i<4; ++i)
            {
                if(i < overScanVector.size())
                    _overscan[i] = static_cast<int>(overScanVector[i]);
            }
        }
        else if(overScanVector.size() == 1)
        {
            overscan = static_cast<int>(overScanVector[0]);
            fillOverscanWithSingleValue = true;
        }
     }
    else if(overscanIntAttr.isValid())
    {

        FnAttribute::IntConstVector overScanVector = overscanIntAttr.getNearestSample(0);
        if(overScanVector.size() > 1)
        {
            for(size_t i=0; i<4; ++i)
            {
                if(i < overScanVector.size())
                    _overscan[i] = overScanVector[i];
            }
        }
        else if(overScanVector.size() == 1)
        {
            overscan = overScanVector[0];
            fillOverscanWithSingleValue = true;
        }
    }

    if(fillOverscanWithSingleValue)
     {
        _overscan[0] = overscan;
        _overscan[1] = overscan;
        _overscan[2] = overscan;
        _overscan[3] = overscan;
     }

    _displayWindow[0] = 0;
    _displayWindow[1] = 0;
    _displayWindow[2] = _xRes;
    _displayWindow[3] = _yRes;

    _dataWindow[0] = -_overscan[0];
    _dataWindow[1] = -_overscan[1];
    _dataWindow[2] = _xRes + _overscan[2];
    _dataWindow[3] = _yRes + _overscan[3];

    FnAttribute::StringAttribute cameraNameAttr = renderSettingsAttr.getChildByName("cameraName");
    if(cameraNameAttr.isValid())
        _cameraName = cameraNameAttr.getValue();

    // Shutter
    FnAttribute::IntAttribute maxTimeSamplesAttr = renderSettingsAttr.getChildByName("maxTimeSamples");
    if(maxTimeSamplesAttr.isValid())
        _maxTimeSamples = maxTimeSamplesAttr.getValue();

    FnAttribute::FloatAttribute shutterOpenAttr = renderSettingsAttr.getChildByName("shutterOpen");
    if(shutterOpenAttr.isValid())
        _shutterOpen = shutterOpenAttr.getValue();

    FnAttribute::FloatAttribute shutterCloseAttr = renderSettingsAttr.getChildByName("shutterClose");
    if(shutterCloseAttr.isValid())
        _shutterClose = shutterCloseAttr.getValue();

    // -- Resolution name --
    FnAttribute::StringAttribute resolutionAttr = renderSettingsAttr.getChildByName("resolution");
    if(resolutionAttr.isValid())
        _resolution = resolutionAttr.getValue();

    // -- Render threads --
    _renderThreadsAttr = renderSettingsAttr.getChildByName("renderThreads");

    _useTileRender = false;
    FnAttribute::IntAttribute tileRenderAttr = renderSettingsAttr.getChildByName("tileRender");
    if(tileRenderAttr.isValid())
    {
        FnAttribute::IntConstVector tileRender = tileRenderAttr.getNearestSample(0);
        if(tileRender.size() == 4)
        {
            _tileRender[0] = tileRender[0];
            _tileRender[1] = tileRender[1];
            _tileRender[2] = tileRender[2];
            _tileRender[3] = tileRender[3];
            _useTileRender = true;
        }
        else
        {
            std::cerr << "Ignoring renderSettings attribute tileRender. Reason: 4 values required." << std::endl;
        }
    }

    FnAttribute::IntAttribute roiAttr = renderSettingsAttr.getChildByName("ROI");
    if(roiAttr.isValid())
    {
        FnAttribute::IntConstVector roi = roiAttr.getNearestSample(0);
        _regionOfInterest[0] = std::max(roi[0], _dataWindow[0]);
        _regionOfInterest[1] = std::max(roi[1], _dataWindow[1]);
        _regionOfInterest[2] = std::min(roi[0] + roi[2], _dataWindow[2]);
        _regionOfInterest[3] = std::min(roi[1] + roi[3], _dataWindow[3]);
    }
    else
    {
        _regionOfInterest[0] = _dataWindow[0];
        _regionOfInterest[1] = _dataWindow[1];
        _regionOfInterest[2] = _dataWindow[2];
        _regionOfInterest[3] = _dataWindow[3];
    }

    FnAttribute::FloatAttribute cropWindowAttr = renderSettingsAttr.getChildByName("cropWindow");
    if(cropWindowAttr.isValid())
    {
      FnAttribute::FloatConstVector cropWindow = cropWindowAttr.getNearestSample(0);
        _cropWindow[0] = cropWindow[0];
        _cropWindow[1] = cropWindow[1];
        _cropWindow[2] = cropWindow[2];
        _cropWindow[3] = cropWindow[3];

        // Adjust the region of interest to take the crop region into account
        int dataWindowSize[2];
        getDataWindowSize(dataWindowSize);

        int cropRegion[4];
        cropRegion[0] = static_cast<int>(_cropWindow[0] * (float) dataWindowSize[0] + (float) _dataWindow[0]);
        cropRegion[1] = static_cast<int>((1.0f - _cropWindow[3]) * (float) dataWindowSize[1] + (float) _dataWindow[1]);
        cropRegion[2] = static_cast<int>(_cropWindow[1] * (float) dataWindowSize[0] + (float) _dataWindow[0]);
        cropRegion[3] = static_cast<int>((1.0f - _cropWindow[2]) * (float) dataWindowSize[1] + (float) _dataWindow[1]);

        _regionOfInterest[0] = std::max(_regionOfInterest[0], cropRegion[0]);
        _regionOfInterest[1] = std::max(_regionOfInterest[1], cropRegion[1]);
        _regionOfInterest[2] = std::min(_regionOfInterest[2], cropRegion[2]);
        _regionOfInterest[3] = std::min(_regionOfInterest[3], cropRegion[3]);
    }
    else
    {
        _cropWindow[0] = 0.0f;
        _cropWindow[1] = 1.0f;
        _cropWindow[2] = 0.0f;
        _cropWindow[3] = 1.0f;
    }

    // Get interactive outputs (AOVs)
    FnAttribute::StringAttribute interactiveOutputsAttr = renderSettingsAttr.getChildByName("interactiveOutputs");
    if(interactiveOutputsAttr.isValid())
        _interactiveOutputs = interactiveOutputsAttr.getValue();

    // Get Katana's temp directory
    FnAttribute::StringAttribute tempDirAttr = renderSettingsAttr.getChildByName("tempDir");
    if(tempDirAttr.isValid())
        _tempDir = tempDirAttr.getValue();

    // renderFinishedFilename
    FnAttribute::StringAttribute renderFinishedFilenameAttr =
            renderSettingsAttr.getChildByName("renderFinishedFilename");
    _renderFinishedFilename = renderFinishedFilenameAttr.getValue("", false);

    // Render outputs
    FnAttribute::GroupAttribute outputsAttr = renderSettingsAttr.getChildByName("outputs");
    if(outputsAttr.isValid())
    {
        int64_t noOutputs = outputsAttr.getNumberOfChildren();
        for(int64_t i = 0; i < noOutputs; ++i)
        {
            std::string outputName = outputsAttr.getChildName(i);
            FnAttribute::GroupAttribute outputAttr = outputsAttr.getChildByIndex(i);

            if(outputAttr.isValid())
            {
                RenderOutput output;
                output.enabled = false;

                FnAttribute::StringAttribute outputTypeAttr = outputAttr.getChildByName("type");
                if(outputTypeAttr.isValid())
                    output.type = outputTypeAttr.getValue();

                if(output.type == "merge" || output.type == "script" || output.type == "prescript")
                    continue;

                FnAttribute::StringAttribute outputLocationTypeAttr = outputAttr.getChildByName("locationType");
                if(outputLocationTypeAttr.isValid())
                    output.locationType = outputLocationTypeAttr.getValue();

                FnAttribute::StringAttribute tempRenderIdAttr = outputAttr.getChildByName("tempRenderId");
                if(tempRenderIdAttr.isValid())
                    output.tempRenderId = tempRenderIdAttr.getValue();

                FnAttribute::GroupAttribute rendererSettingsAttr = outputAttr.getChildByName("rendererSettings");
                if(rendererSettingsAttr.isValid())
                {
                    for(int i = 0; i < rendererSettingsAttr.getNumberOfChildren(); i++)
                    {
                        std::string childName = rendererSettingsAttr.getChildName(i);
                        FnAttribute::Attribute childAttr = rendererSettingsAttr.getChildByIndex(i);

                        FnAttribute::StringAttribute stringAttr = childAttr;
                        if(stringAttr.isValid())
                        {
                            // Check if the attribute is an explicit standard string
                            // parameter and associate it with a corresponding
                            // variable. Additionally, we preserve the attribute along
                            // with its existing implicit type where the renderer
                            // plug-in can deal with it.
                            if(childName == "colorSpace")
                                output.colorSpace = stringAttr.getValue();
                            else if(childName == "channel")
                                output.channel = stringAttr.getValue();
                            else if(childName == "fileExtension")
                                output.fileExtension = stringAttr.getValue();
                            else if(childName == "tempRenderLocation")
                                output.tempRenderLocation = stringAttr.getValue();
                        }

                        output.rendererSettings[childName] = childAttr;
                    }

                    if(output.type == "color")
                        processColorOutput(output, rendererSettingsAttr);
                }

                FnAttribute::StringAttribute renderLocationAttr = outputAttr.getChildByName("renderLocation");
                if(renderLocationAttr.isValid())
                {
                    output.renderLocation = renderLocationAttr.getValue();
                }

                FnAttribute::StringAttribute enabledAttr = outputAttr.getChildByName("enabled");
                if(enabledAttr.isValid() && enabledAttr.getValue() == "true")
                {
                    output.enabled = true;
                    _enabledRenderOutputNames.push_back(outputName);
                }
                _renderOutputs[outputName] = output;
                _renderOutputNames.push_back(outputName);
            }
        }
    }

    // AOV buffers
    FnAttribute::GroupAttribute seqIdMapAttr = renderSettingsAttr.getChildByName("seqIDMap");
    if(seqIdMapAttr.isValid())
    {
        int64_t noSeqMaps = seqIdMapAttr.getNumberOfChildren();
        for(int64_t i = 0; i < noSeqMaps; i++)
        {
            std::string seqIdName = seqIdMapAttr.getChildName(i);
            FnAttribute::StringAttribute seqIdAttr = seqIdMapAttr.getChildByIndex(i);

            if(seqIdAttr.isValid())
            {
                ChannelBuffer buffer;
                buffer.bufferId = seqIdAttr.getValue();

                std::string attributeName = "outputs." + seqIdName + ".rendererSettings.channel";
                FnAttribute::StringAttribute channelAttr = renderSettingsAttr.getChildByName(attributeName);
                if(channelAttr.isValid()) {
                    buffer.channelName = channelAttr.getValue();
                } else {
                    buffer.channelName = "rgba";
                }
                _buffers[seqIdName] = buffer;
            }
        }
    }
}

std::vector<std::string> RenderSettings::getRenderOutputNames(const bool onlyEnabledOutputs) const
{
    if(onlyEnabledOutputs)
        return _enabledRenderOutputNames;
    else
        return _renderOutputNames;
}

void RenderSettings::getSampleRate(float sampleRate[2]) const
{
    sampleRate[0] = _sampleRate[0];
    sampleRate[1] = _sampleRate[1];
}

void RenderSettings::getDisplayWindow(int displayWindow[4]) const
{
    displayWindow[0] = _displayWindow[0];
    displayWindow[1] = _displayWindow[1];
    displayWindow[2] = _displayWindow[2];
    displayWindow[3] = _displayWindow[3];
}

void RenderSettings::getDataWindow(int dataWindow[4]) const
{
    dataWindow[0] = _dataWindow[0];
    dataWindow[1] = _dataWindow[1];
    dataWindow[2] = _dataWindow[2];
    dataWindow[3] = _dataWindow[3];
}

void RenderSettings::getOverscan(int overscan[4]) const
{
    overscan[0] = _overscan[0];
    overscan[1] = _overscan[1];
    overscan[2] = _overscan[2];
    overscan[3] = _overscan[3];
}

void RenderSettings::getRegionOfInterest(int regionOfInterest[4]) const
{
    regionOfInterest[0] = _regionOfInterest[0];
    regionOfInterest[1] = _regionOfInterest[1];
    regionOfInterest[2] = _regionOfInterest[2];
    regionOfInterest[3] = _regionOfInterest[3];
}

bool RenderSettings::applyRenderThreads(int& renderThreads) const
{
    if(_renderThreadsAttr.isValid())
    {
        renderThreads = _renderThreadsAttr.getValue();
        return true;
    }

    return false;
}

void RenderSettings::processColorOutput(RenderOutput& output, FnAttribute::GroupAttribute rendererSettingsAttr) const
{
    FnAttribute::GroupAttribute convertSettingsAttr = rendererSettingsAttr.getChildByName("convertSettings");
    if(convertSettingsAttr.isValid())
    {
        for(int i = 0; i < convertSettingsAttr.getNumberOfChildren(); i++)
        {
            FnAttribute::Attribute childAttr = convertSettingsAttr.getChildByIndex(i);
            if(childAttr.isValid())
                output.convertSettings[convertSettingsAttr.getChildName(i)] = childAttr;
        }
    }

    FnAttribute::IntAttribute clampOutputAttr = rendererSettingsAttr.getChildByName("clampOutput");
    if(clampOutputAttr.isValid())
    {
        clampOutputAttr.getValue() ? output.clampOutput = true : output.clampOutput = false;
    }

    FnAttribute::IntAttribute colorConvertAttr = rendererSettingsAttr.getChildByName("colorConvert");
    if(colorConvertAttr.isValid())
    {
        colorConvertAttr.getValue() ? output.colorConvert = true : output.colorConvert = false;
    }

    FnAttribute::StringAttribute computeStatsAttr = rendererSettingsAttr.getChildByName("computeStats");
    if(computeStatsAttr.isValid())
    {
        output.computeStats = computeStatsAttr.getValue();
    }
    else
    {
        output.computeStats = "None";
    }

}

void RenderSettings::getInteractiveOutputs(std::vector<std::string>& outputs) const
{
    if(_interactiveOutputs != "")
    {
        std::stringstream outputStream(_interactiveOutputs);
        std::string output;

        while(getline(outputStream, output, ','))
        {
            outputs.push_back(output);
        }
    }
}

void RenderSettings::getChannelBuffers(ChannelBuffers& channelBuffers)
{
    std::vector<std::string> interactiveOutputs;
    getInteractiveOutputs(interactiveOutputs);

    const RenderOutputs outputs = getRenderOutputs();

    for (std::vector<std::string>::const_iterator it = interactiveOutputs.begin();
            it != interactiveOutputs.end(); ++it) {
        std::string interactiveChannelBufferName = *it;
        if (it == interactiveOutputs.begin()) {
            interactiveChannelBufferName = std::string("0") + interactiveChannelBufferName;
        }

        if (_buffers.find(*it) == _buffers.end()) {
            // There shouldn't be an Interactive Output without a regular
            // RenderOutput
            const auto outputIt = outputs.find(*it);
            if (outputIt == outputs.end()) {
                continue;
            }

            ChannelBuffer buffer;
            buffer.channelName = outputIt->second.channel;
            _buffers[*it] = buffer;
        }

        channelBuffers[interactiveChannelBufferName] = _buffers[*it];
    }
}

RenderSettings::RenderOutput RenderSettings::getRenderOutputByName(const std::string& outputName) const
{
    RenderSettings::RenderOutput renderOutput;

    std::map<std::string, RenderOutput>::const_iterator findOutputIt =
        _renderOutputs.find(outputName);
    if(findOutputIt != _renderOutputs.end())
    {
        renderOutput = findOutputIt->second;
    }

    return renderOutput;
}

void RenderSettings::getWindowOrigin(int windowOrigin[2]) const
{
    windowOrigin[0] = _regionOfInterest[0];
    windowOrigin[1] = _displayWindow[3] - _regionOfInterest[3];
}

void RenderSettings::getDisplayWindowSize(int displayWindowSize[2]) const
{
    displayWindowSize[0] = _displayWindow[2] - _displayWindow[0];
    displayWindowSize[1] = _displayWindow[3] - _displayWindow[1];
}

void RenderSettings::getDataWindowSize(int dataWindowSize[2]) const
{
    dataWindowSize[0] = _dataWindow[2] - _dataWindow[0];
    dataWindowSize[1] = _dataWindow[3] - _dataWindow[1];
}

void RenderSettings::getCropWindow(float cropWindow[4]) const
{
    calculateCropWindow(cropWindow);
}

void RenderSettings::calculateCropWindow(float calculatedCropWindow[4]) const
{
    float originX = static_cast<float>(_dataWindow[0]);
    float originY = static_cast<float>(_dataWindow[1]);
    float frameWidth = static_cast<float>(_dataWindow[2] - _dataWindow[0]);
    float frameHeight = static_cast<float>(_dataWindow[3] - _dataWindow[1]);

    calculatedCropWindow[0] = std::max(0.0f, (static_cast<float>(_regionOfInterest[0]) - originX) / frameWidth);
    calculatedCropWindow[1] = std::min(1.0f, (static_cast<float>(_regionOfInterest[2]) - originX) / frameWidth);
    calculatedCropWindow[2] = std::max(0.0f, 1.0f - (static_cast<float>(_regionOfInterest[3]) - originY) / frameHeight);
    calculatedCropWindow[3] = std::min(1.0f, 1.0f - (static_cast<float>(_regionOfInterest[1]) - originY) / frameHeight);

    // Intersect with the render settings crop window
    calculatedCropWindow[0] = std::max(_cropWindow[0], calculatedCropWindow[0]);
    calculatedCropWindow[1] = std::min(_cropWindow[1], calculatedCropWindow[1]);
    calculatedCropWindow[2] = std::max(_cropWindow[2], calculatedCropWindow[2]);
    calculatedCropWindow[3] = std::min(_cropWindow[3], calculatedCropWindow[3]);
}

} // namespace internal

MChannelInfo::MChannelInfo(const std::string& rawName,
                           const std::string& returnName,
                           const std::string& altName,
                           const int bufId,
                           const internal::RenderSettings::RenderOutput* output,
                           ChannelType channelType)
    :   mRenderOutputName(rawName)
    ,   mChanReturnName(returnName)
    ,   mMoonrayChannelName(altName)
    ,   mLocationPath("/root/__scenebuild/renderoutput/" + mRenderOutputName)
    ,   mBufferId(bufId)
    ,   mRenderOutput(output)
    ,   mChannelType(channelType)
{
    KdLogDebug("RenderOutput '" << mRenderOutputName << "' - returnName: "
               << mChanReturnName << ", channel name: " << mMoonrayChannelName
               << ", scenegraph location: " << mLocationPath);
}


/////////////////////////////////////// MoonrayRenderSettings /////////////////
void
MoonrayRenderSettings::initialize(const FnAttribute::GroupAttribute& renderSettingsAttr)
{
    internal::RenderSettings::initialize(renderSettingsAttr);

    ChannelBuffers channelBuffers;
    getChannelBuffers(channelBuffers);
    if (channelBuffers.size() > 0) {
        if (!channelBuffers.begin()->second.bufferId.empty()) {
            mFrameId = std::stol(channelBuffers.begin()->second.bufferId);
        }
    }

    // Build Channel Info //

    // First, get the list of raw interactive output names
    std::vector<std::string> outputs;
    getInteractiveOutputs(outputs);

    std::string firstInteractiveOutput = outputs.empty() ? "" : outputs.front();
    std::set<std::string> interactiveOutputSet(outputs.begin(), outputs.end());

    // Now we build one each, but we have to hack the return name for primary
    // to match what katana does.
    for (const auto &renderOutput : _renderOutputs) {
        std::string rawOutputName = renderOutput.first;

        // hack name  - match interactive output logic
        std::string returnName;
        if (rawOutputName == firstInteractiveOutput) {
            returnName = "0" + firstInteractiveOutput;
        } else {
            returnName = rawOutputName;
        }

        // beauty check
        static const std::string kBeauty("beauty");
        const MChannelInfo::ChannelType channelType =
                (renderOutput.second.channel == kBeauty) ?
                        MChannelInfo::ChannelType::BEAUTY : MChannelInfo::ChannelType::AOV;

        // find channel ID entry
        const auto iter = _buffers.find(rawOutputName);

        // Create and store it
        try {
            if (iter == _buffers.end()) {
                mChannels.emplace_back(new MChannelInfo(rawOutputName, returnName,
                                               renderOutput.second.channel, -1,
                                               &renderOutput.second, channelType));
            } else {
                int bufferId = 0;
                if (!iter->second.bufferId.empty()) {
                    bufferId = stoi(iter->second.bufferId);
                }
                mChannels.emplace_back(new MChannelInfo(rawOutputName, returnName,
                                               iter->second.channelName, bufferId,
                                               &renderOutput.second, channelType));
            }
            MChannelInfo* newChan = mChannels.back().get();
            mChannelsByName[newChan->getRenderOutputName()] = newChan;
            mChannelsByLocation[newChan->getLocationPath()] = newChan;

            if (renderOutput.second.enabled) {
                mEnabledChannels.push_back(newChan);
            }
            if (interactiveOutputSet.count(rawOutputName)) {
                mInteractiveChannels.push_back(newChan);
            }
        }
        catch (const std::logic_error &e) {
            if (iter == _buffers.end()) {
                KdLogError("Unexpected error for channel " << returnName << "/"
                           << rawOutputName << "/" << renderOutput.second.channel <<
                           " Exception: " << e.what());
            } else {
                KdLogError("Possible invalid buffer id for channel " << returnName << "/"
                           << rawOutputName << "/" << iter->second.channelName <<
                           ":" << iter->second.bufferId <<
                           " Exception: " << e.what());
            }
        }
    }
}

const MChannelInfo*
MoonrayRenderSettings::getChannelByName(const kodachi::string_view& name) const
{
    const auto iter = mChannelsByName.find(name);
    if (iter != mChannelsByName.end()) {
        return iter->second;
    }
    return nullptr;
}

const MChannelInfo*
MoonrayRenderSettings::getChannelByLocationPath(const kodachi::string_view& name) const
{
    const auto iter = mChannelsByLocation.find(name);
    if (iter != mChannelsByLocation.end()) {
        return iter->second;
    }
    return nullptr;
}

void
MoonrayRenderSettings::initializeIdPassChannel()
{
    if (!mIdPassChannel) {
        KdLogDebug("Initializing ID Pass Channel");
        // There isn't a channelID specified for the ID pass. So use 1 higher than
        // the last channel
        const int64_t chanId = mFrameId + mChannels.size();

        // create the channel
        // Katana requires that the return name be "__id"
        mIdPassChannel.reset(new MChannelInfo("katana_id",
                                              "__id",
                                              "katana_id",
                                              chanId,
                                              nullptr,
                                              MChannelInfo::ChannelType::ID));

        mChannelsByLocation[mIdPassChannel->getLocationPath()] = mIdPassChannel.get();

        mInteractiveChannels.emplace_back(mIdPassChannel.get());
    } else {
        KdLogWarn("ID Pass Channel already initialized");
    }
}

const MChannelInfo*
MoonrayRenderSettings::getIdPassChannel() const
{
    return mIdPassChannel.get();
}

} // namespace mfk

