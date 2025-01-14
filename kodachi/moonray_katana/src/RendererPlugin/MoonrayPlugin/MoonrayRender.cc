// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "MoonrayRender.h"

// Local includes
#include "KodachiRenderMethod.h"
#include "MoonrayRenderSettings.h"

// Katana includes
#include <FnAPI/FnAPI.h>
#include <FnRenderOutputUtils/FnRenderOutputUtils.h>
#include <FnRendererInfo/suite/RendererObjectDefinitions.h>
#include <FnRendererInfo/plugin/RenderMethod.h>
#include <FnRender/plugin/CameraSettings.h>

// katana image driver stuff
#include <FnDisplayDriver/FnKatanaDisplayDriver.h>
#include <FnLogging/FnLogging.h>

// kodachi
#include <kodachi/OpTreeBuilder.h>
#include <kodachi/OpTreeUtil.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/plugin_system/PluginManager.h>

// system
#include <cmath>
#include <fstream>
#include <chrono>
#include <thread>

using namespace FnKat;
namespace FnKatRender = FnKat::Render;

namespace {

KdLogSetup("MoonrayRender");

// terminal color code
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"

void
FnLoggingHandler(const char* message,
             KdLoggingSeverity severity,
             const char* module,
             const char* file,
             int line,
             int indentDepth,
             void* userData)
{
    std::ostringstream msgBuf;
    unsigned int fnSeverity = 0;
    // color the message and get Katana's severity number
    switch (severity) {
        case kKdLoggingSeverityFatal:
            fnSeverity = kFnLoggingSeverityCritical;
            msgBuf << RED;
            break;
        case kKdLoggingSeverityError:
            fnSeverity = kFnLoggingSeverityError;
            msgBuf << RED;
            break;
        case kKdLoggingSeverityWarning:
            fnSeverity = kFnLoggingSeverityWarning;
            msgBuf << YELLOW;
            break;
        case kKdLoggingSeverityInfo:
            fnSeverity = kFnLoggingSeverityInfo;
            msgBuf << GREEN;
            break;
        case kKdLoggingSeverityDebug:
            // FnLogging Debug level isn't enabled by default, so use Info
            fnSeverity = kFnLoggingSeverityInfo;
            msgBuf << CYAN;
            break;
        default:
            break;
    }
    // add indent
    for (uint i = 0; i < indentDepth; i++) {
        msgBuf << "   ";
    }
    // add message and reset the color
    msgBuf << message << RESET;

    // Call Fn log directly.
    try {
        const auto fnLoggingSuite = FnLog::getSuite();
        if (fnLoggingSuite) {
            fnLoggingSuite->log(msgBuf.str().c_str(), fnSeverity, module, file, line);
        } else {
            std::cerr << "FnLoggingHandler: FnLogging is not initialized";
        }
    } catch (...) {
        // fail silently in the off chance this happens
    }
}

void
setupLogging(const kodachi::IntAttribute& logLimitAttr)
{
    // KodachiLogging defaults to ERROR severity, but we want the default to be
    // INFO for Katana renders.
    const int logLevel = logLimitAttr.getValue(kKdLoggingSeverityInfo, false);

    kodachi::KodachiLogging::setSeverity(static_cast<KdLoggingSeverity>(logLevel));

    KdLogDebug("Logging threshold " << logLevel);
}

std::string
insertDenoiserSuffixToFilePath(const std::string& filePath,
                               const std::string& suffix)
{
    std::string suffixedFilePath = filePath;

    // Find the last period.  Hopefully this is where the file extension is
    std::size_t period = suffixedFilePath.find_last_of(".");
    if (period != std::string::npos) {
        // Insert the suffix right before the extension
        suffixedFilePath.insert(period, suffix);
    } else {
        throw std::runtime_error("File path " + filePath + " does not have a valid file extension");
    }

    return suffixedFilePath;
}

// Helper to create the DiskRenderOutputProcess post command
// strings to copy the odd and even denoiser outputs.
void
getDenoiserPostCommands(const std::string& tmpRenderLoc,
                        const std::string& targetRenderLoc,
                        std::string& postCommandEven,
                        std::string& postCommandOdd)
{
    // Get the tmp location for the _even denoiser output
    const std::string evenTmpRenderLoc =
            insertDenoiserSuffixToFilePath(tmpRenderLoc, ".1");

    // Add _even to the end of the target path before the extension
    const std::string evenTargetRenderLoc =
            insertDenoiserSuffixToFilePath(targetRenderLoc, ".1");

    // Create the postCommand to copy the temporary even exr to
    // the target even exr
    postCommandEven = "cp -f " + evenTmpRenderLoc + " " + evenTargetRenderLoc;

    // Get tmp and target locations for the _odd buffer
    const std::string oddTmpRenderLoc =
         insertDenoiserSuffixToFilePath(tmpRenderLoc, ".0");

    // Add _odd to the end of the target path before the extension
    std::string oddTargetRenderLoc =
            insertDenoiserSuffixToFilePath(targetRenderLoc, ".0");

    // Create the postCommand to copy the temporary odd exr to
    // the target odd exr
    postCommandOdd = "cp -f " + oddTmpRenderLoc + " " + oddTargetRenderLoc;
}

} // anonymous namespace

MoonrayRender::MoonrayRender(FnScenegraphIterator rootIterator,
                             FnAttribute::GroupAttribute arguments)
    :   RenderBase(rootIterator, arguments)
{
    // NOTE: There won't necessarily be any arguments, such as in the case
    // where Katana is doing in-process pre-work for disk renders.

    // Register the FnLogging handler
    kodachi::KodachiLogging::registerHandler(FnLoggingHandler, nullptr,
                                             kKdLoggingSeverityDebug, "MoonrayRender");

    // If renderOutputFile is set then we are only outputting rdla, and not
    // starting a render, arras or otherwise
    const kodachi::StringAttribute renderOutputFile =
            arguments.getChildByName("renderOutputFile");

    const bool isDebugRender = renderOutputFile.isValid();

    mfk::KatanaRenderMethod renderMethod = mfk::KatanaRenderMethod::DISK;
    {
        const std::string renderMethodName = getRenderMethodName();
        if (!renderMethodName.empty()) {
            renderMethod = mfk::parseMethod(renderMethodName);
        }
    }

    // Memory usage warnings for live render
    {
        kodachi::KodachiLogging::setSeverity(kKdLoggingSeverityWarning);
        if (renderMethod == mfk::KatanaRenderMethod::LIVE) {
            KdLogWarn("!!! AUTO-INSTANCING DISABLED !!!")
            KdLogWarn("Auto-instancing not compatible with Live render. "
                      "Interactive memory usage will be higher than farm memory usage.");
        }
    }

    setupLogging(rootIterator.getAttribute("moonrayGlobalStatements.log limit"));

    kodachi::GroupAttribute opTreeAttr;
    if (renderMethod == mfk::KatanaRenderMethod::DISK || isDebugRender) {
        // Since we won't have the MoonrayRenderManager to send us the optree
        // in headless mode, modify the optree provided by renderboot to look
        // like a multi-context kodachi optree. We also have to add the terminal
        // ops here so that we can build them with the correct systemOpArgs.

        const kodachi::StringAttribute geolib3OpTreeAttr =
                arguments.getChildByName("geolib3OpTree");

        if (geolib3OpTreeAttr.isValid()) {
            opTreeAttr = FnKat::FnScenegraphIterator::getOpTreeDescriptionFromFile(
                    geolib3OpTreeAttr.getValue());

            opTreeAttr = kodachi::optree_util::convertToKodachiOpTree(opTreeAttr);

            // cacheCreationMode. If we are doing a cache pass, then we don't
            // need the terminal ops for anything.
            const kodachi::IntAttribute skipRenderAttr =
                    rootIterator.getAttribute("moonrayGlobalStatements.skip render");

            const kodachi::IntAttribute cacheCreationModeAttr =
                    rootIterator.getAttribute("moonrayGlobalStatements.cacheCreationMode");

            const bool isCachePass = skipRenderAttr.getValue(false, false) &&
                    cacheCreationModeAttr.getValue(false, false);

            if (!isCachePass) {
                kodachi::OpTreeBuilder otb;
                const auto initialOps = otb.merge(opTreeAttr);

                kodachi::GroupBuilder systemOpArgsBuilder;
                {
                const kodachi::GroupAttribute renderSettingsAttr =
                                    getRootIterator().getAttribute("renderSettings");
                    systemOpArgsBuilder
                        .set("timeSlice.currentTime", kodachi::FloatAttribute(getRenderTime()))
                        .set("timeSlice.numSamples", renderSettingsAttr.getChildByName("maxTimeSamples"))
                        .set("timeSlice.shutterOpen", renderSettingsAttr.getChildByName("shutterOpen"))
                        .set("timeSlice.shutterClose", renderSettingsAttr.getChildByName("shutterClose"));
                }

                kodachi::GroupBuilder terminalOpsConfigBuilder;
                terminalOpsConfigBuilder.set("type", kodachi::StringAttribute("terminalOps"));
                terminalOpsConfigBuilder.set("renderType", kodachi::StringAttribute("diskRender"));

                kodachi::GroupAttribute terminalOpsAttr =
                            kodachi::BackendClient::getStaticData("MoonrayRenderBackend",
                                                                   terminalOpsConfigBuilder.build());

                terminalOpsAttr = kodachi::optree_util::addSystemOpArgsToOpCollection(terminalOpsAttr, systemOpArgsBuilder.build());

                const auto terminalOps = otb.appendOpChain(initialOps.back(), terminalOpsAttr);
                opTreeAttr = otb.build(terminalOps.back());
            }

            kodachi::GroupBuilder gb;
            gb.set("type", kodachi::StringAttribute("OPTREE_INITIAL"));
            gb.set("activeContext", kodachi::StringAttribute("diskRender"));
            gb.set("contexts.diskRender.optree", opTreeAttr);
            opTreeAttr = gb.build();
        }

    } else {
        // we should be communicating with the MoonrayRenderManager
        const kodachi::StringAttribute geolib3OpTreeAttr =
                        arguments.getChildByName("geolib3OpTree");

        if (geolib3OpTreeAttr.isValid()) {
            std::string opTreePath = geolib3OpTreeAttr.getValue();
            const auto pos = opTreePath.rfind('.');

            if (pos != std::string::npos) {
                opTreePath = opTreePath.substr(0, pos);
                mSubscriberSocket.reset(new mfk::SubscriberSocket("ipc://" + opTreePath));

                opTreeAttr = mSubscriberSocket->getMessage();
                KdLogDebug("Received optree from MoonrayRenderManager");
            } else {
                throw std::runtime_error("Unable to setup ZMQ socket");
            }
        }
    }

    mRenderHandler.reset(new mfk::KodachiRenderMethod(
            this, renderMethod, opTreeAttr, renderOutputFile));

    if (renderMethod == mfk::KatanaRenderMethod::LIVE) {
        mSubscriberSocket->startCallbackLoop(
                [this](const kodachi::Attribute& attr)
                               { mRenderHandler->queueDataUpdates(attr); }, 1);
    } else {
        mSubscriberSocket.reset();
    }
}

MoonrayRender::~MoonrayRender()
{
}

int MoonrayRender::start()
{
    KdLogDebug(__PRETTY_FUNCTION__);

    try {
        return mRenderHandler->start();
    } catch (const std::exception& e) {
        KdLogFatal(e.what());

        // Returning -1 isn't enough to stop a live render, but in this case
        // renderboot will catch the exception
        if (getRenderMethodName() == FnKat::RendererInfo::LiveRenderMethod::kDefaultName) {
            throw;
        }

        KdLogFatal("Terminating render...");
        return -1;
    }
}

int MoonrayRender::pause()
{
    KdLogDebug(__PRETTY_FUNCTION__);
    return mRenderHandler->pause();
}

int MoonrayRender::resume()
{
    KdLogDebug(__PRETTY_FUNCTION__);
    return mRenderHandler->resume();
}

int MoonrayRender::stop()
{
    KdLogDebug(__PRETTY_FUNCTION__);
    return mRenderHandler->stop();
}

int MoonrayRender::processControlCommand(const std::string& command)
{
    return 0;
}

void MoonrayRender::configureDiskRenderOutputProcess(
        FnKatRender::DiskRenderOutputProcess& diskRenderOutputProcess,
        const std::string& outputName,
        const std::string& outputPath,
        const std::string& renderMethodName,
        const float& frameTime) const
{
    KdLogDebug("MoonrayRender::configureDiskRenderOutputProcess()");

    std::string ext = outputPath.substr(outputPath.find_last_of(".") + 1);

    std::string tmpRenderLoc =
            FnKat::RenderOutputUtils::buildTempRenderLocation(getRootIterator(),
                                                              outputName,
                                                              "render",
                                                              ext.c_str(),
                                                              frameTime);
    std::string targetRenderLoc = outputPath;

    KdLogDebug("tmpRenderLoc: " << tmpRenderLoc);
    KdLogDebug("targetRenderLoc: " << targetRenderLoc);

    // Add the tile id to the filename for tile renders.
    // Ex for a 2x2 tile render: tile_0_0.scene.1.exr
    //                           tile_0_1.scene.1.exr
    //                           tile_1_0.scene.1.exr
    //                           tile_1_1.scene.1.exr
    const bool isTileRender =
            mRenderHandler->getRenderSettings().isTileRender();
    if (isTileRender) {
        targetRenderLoc =
                FnKat::RenderOutputUtils::buildTileLocation(getRootIterator(), outputPath);
    }

    mfk::internal::RenderSettings::RenderOutput output =
            mRenderHandler->getRenderSettings().getRenderOutputByName(outputName);

    KdLogDebug("output.type: " << output.type);

    FnKatRender::DiskRenderOutputProcess::RenderActionPtr renderActionPtr;

    if (output.type == kFnRendererOutputTypeColor) {
        // Do some special stuff if this output was told to generate denoiser
        // outputs.
        const auto& outputRenderSettings = output.rendererSettings;
        const auto denoiseIter =
                outputRenderSettings.find("generate_denoiser_outputs");
        if (denoiseIter != outputRenderSettings.end() &&
                kodachi::StringAttribute(denoiseIter->second) == "on") {

            // Create postcommands to copy the temp even and odd files
            // to the even and odd target locations
            std::string postCommandEven;
            std::string postCommandOdd;
            getDenoiserPostCommands(tmpRenderLoc,
                                    targetRenderLoc,
                                    postCommandEven,
                                    postCommandOdd);
            diskRenderOutputProcess.addPostCommand(postCommandEven);
            diskRenderOutputProcess.addPostCommand(postCommandOdd);
        }

        if (isTileRender) {
            renderActionPtr.reset(new FnKatRender::CopyRenderAction(targetRenderLoc,
                                                                 tmpRenderLoc));
        } else {
            auto* copyConvertAction =
                new FnKatRender::CopyAndConvertRenderAction(targetRenderLoc,
                                                            tmpRenderLoc,
                                                            output.clampOutput,
                                                            output.colorConvert,
                                                            output.computeStats,
                                                            output.convertSettings);
            copyConvertAction->setOffsetForOverscan(false);
            renderActionPtr.reset(copyConvertAction);
        }
    } else if (output.type == kFnRendererOutputTypeRaw) {
        int rawHasOutput =
                IntAttribute(output.rendererSettings["rawHasOutput"]).getValue(0,
                                                                               false);

        // Do some special stuff if this output was told to generate denoiser
        // outputs.
        const auto& outputRenderSettings = output.rendererSettings;
        const auto denoiseIter =
                outputRenderSettings.find("generate_denoiser_outputs");
        if (denoiseIter != outputRenderSettings.end() &&
                kodachi::StringAttribute(denoiseIter->second) == "on") {

            // Create postcommands to copy the temp even and odd files
            // to the even and odd target locations
            std::string postCommandEven;
            std::string postCommandOdd;
            getDenoiserPostCommands(tmpRenderLoc,
                                    targetRenderLoc,
                                    postCommandEven,
                                    postCommandOdd);
            diskRenderOutputProcess.addPostCommand(postCommandEven);
            diskRenderOutputProcess.addPostCommand(postCommandOdd);
        }

        renderActionPtr.reset(new FnKatRender::CopyRenderAction(tmpRenderLoc,
                                                                targetRenderLoc));
        if (rawHasOutput) {
            renderActionPtr.reset(new FnKatRender::CopyRenderAction(targetRenderLoc,
                                                                 tmpRenderLoc));
        } else {
            renderActionPtr.reset(new FnKatRender::TemporaryRenderAction(tmpRenderLoc));
            renderActionPtr->setLoadOutputInMonitor(false);
        }
    }
    diskRenderOutputProcess.setRenderAction(std::move(renderActionPtr));
}

/**
 * Output to the log progress info. Presumes 0-1 float.
 * Limits output by only logging at certain percentage intervals.
 */
void
MoonrayRender::logProgress(const float progress)
{
    static float prevProgress = 0; // track what we've already logged
    constexpr float nextStepSize = 0.05;

    // Check to see if we've restarted
    if (progress < prevProgress) {
        resetProgress();
    }
    prevProgress = progress;

    // See if we've gone far enough to warrant another log output
    if (progress >= mNextProgress) {
        const int pct = static_cast<int>(floorf(progress * 100));

        // This is all Katana looks for to show the progress bar.
        // Use a direct FnLog call instead of our logging facility so we always send the progress,
        // regardless of filtering.
        const std::string pctStr = std::to_string(pct) + "%";
        FnLog::getSuite()->log(pctStr.c_str(), kFnLoggingSeverityInfo, "Render Progress", nullptr, -1);

        // make sure we don't miss the 100% mark
        if (mNextProgress < 1.0 && (mNextProgress + nextStepSize) >= 1.0) {
            mNextProgress = 1.0;
        } else {
            mNextProgress += nextStepSize;
        }
    }
}

FnPluginStatus
MoonrayRender::setHost(FnPluginHost* host)
{
    kodachi::Attribute::setHost(host);
    kodachi::GroupBuilder::setHost(host);
    kodachi::BackendClient::setHost(host);
    kodachi::KodachiLogging::setHost(host);
    kodachi::OpTreeBuilder::setHost(host);

    return FnKat::Render::RenderBase::setHost(host);
}

/**
 * Parse the given host info, adjust accordingly, open the image pipe, and
 * hang onto it.
 */
bool
MoonrayRender::openPipe()
{
    if (mImagePipe) {
        return true;
    }

    std::string hostString = getKatanaHost();
    if (hostString.empty()) {
        return false;
    }

    // Parse host string into name and port
    auto splitPos = hostString.find(':');
    std::string hostname = hostString.substr(0, splitPos);

    std::string portstr = hostString.substr(splitPos+1, std::string::npos);

    constexpr int PORT_OFFSET = 100;
    unsigned int portNum = std::stoi(portstr) + PORT_OFFSET;

    // Open the pipe //////////////////////////////////////////
    mImagePipe = FnKat::PipeSingleton::Instance(hostname, portNum);
    int connectResult = mImagePipe->connect();
    if (connectResult) {
        // log error
        KdLogError("KatanaPipe: Couldn't connect with " << hostname << ":" << portNum << "(" << hostString << ")");
        mImagePipe = nullptr; // clear so we try to connect again??? (should be rare)
        return false;
    }
    KdLogDebug("KatanaPipe: Connected to " << hostname << ":" << portNum << "(" << hostString << ")");
    return true;
}

Foundry::Katana::KatanaPipe *
MoonrayRender::getImagePipe()
{
    openPipe();
    return mImagePipe;
}

// Plugin Registration code
DEFINE_RENDER_PLUGIN(MoonrayRender)

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayRender, "moonray", 0, 1);
}


