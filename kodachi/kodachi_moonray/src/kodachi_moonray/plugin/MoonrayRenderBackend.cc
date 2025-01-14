// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include "MoonrayRenderBackend.h"

// kodachi
#include <kodachi/Kodachi.h>
#include <kodachi/ArrayView.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/attribute_function/AttributeFunctionUtil.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/OpTreeBuilder.h>
#include <kodachi/OpTreeUtil.h>
#include <kodachi/plugin_system/KdPlugin.h>
#include <kodachi/Traversal.h>

#include <kodachi_moonray/moonray_util/MoonrayUtil.h>
#include <kodachi_moonray/kodachi_runtime_wrapper/KodachiRuntimeWrapper.h>

// moonray
#include <arras/common/mcrt_util/ProcessStats.h>
#include <arras/rendering/rndr/RenderOutputDriver.h>

// moonshine
#include <rendering/shading/Shading.h>

// scene_rdl2
#include <scene_rdl2/common/fb_util/SparseTiledPixelBuffer.h>
#include <scene_rdl2/common/fb_util/PixelBufferUtilsGamma8bit.h>
#include <scene_rdl2/scene/rdl2/Camera.h>
#include <scene_rdl2/scene/rdl2/Utils.h>
#include <scene_rdl2/scene/rdl2/RenderOutput.h>
#include <scene_rdl2/scene/rdl2/Geometry.h>
#include <scene_rdl2/scene/rdl2/Material.h>
#include <scene_rdl2/scene/rdl2/Light.h>

// tbb
#include <tbb/parallel_for_each.h>
#include <tbb/task_group.h>

// stl
#include <iomanip>
#include <thread>
#include <mutex>
#include <stack>
#include <sys/time.h>
#include <sys/resource.h>

namespace {

KdLogSetup("MoonrayRenderBackend");

const std::string kType("type");
const std::string kRoot("/root");
const std::string kSetKPOPState("SetKPOPState");

void DeleteDataPtr(void* dataPtr)
{
    std::default_delete<arras::engine_tool::McrtFbSender::DataPtr>()(
            reinterpret_cast<arras::engine_tool::McrtFbSender::DataPtr*>(dataPtr));
}

kodachi::FloatAttribute
createFloatAttributeBuffer(const arras::engine_tool::McrtFbSender::DataPtr& dataPtr, uint32_t numBytes)
{
    const float* floatValues = reinterpret_cast<float*>(dataPtr.get());

    const int64_t numFloats = numBytes / sizeof(float);

    arras::engine_tool::McrtFbSender::DataPtr* contextPtr = new arras::engine_tool::McrtFbSender::DataPtr(dataPtr);

    return kodachi::FloatAttribute(floatValues, numFloats, 1, contextPtr, DeleteDataPtr);
}

void
alphaDataDeleter(uint8_t* data)
{
    std::default_delete<float[]>()(reinterpret_cast<float*>(data));
}

/**
 * Extracts the alpha channel from the RGBA beauty buffer and adds it to the
 * BaseFrame as its own buffer
 */
std::pair<arras::engine_tool::McrtFbSender::DataPtr, size_t>
createAlphaData(const arras::engine_tool::McrtFbSender::DataPtr& beautyBuffer,
                    size_t dataSize)
{
    constexpr std::size_t kFloatSize = sizeof(float);

    // RGBA format
    const float* beautyData = reinterpret_cast<float*>(beautyBuffer.get());
    const std::size_t beautySize = dataSize / kFloatSize;

    const std::size_t alphaSize = beautySize / 4;
    arras::engine_tool::McrtFbSender::DataPtr alphaPtr;
    {
        std::unique_ptr<float[]> alpha(new float[alphaSize]);

        for (size_t i = 0; i < alphaSize; ++i) {
            alpha[i] = beautyData[(i * 4) + 3];
        }

        alphaPtr = arras::engine_tool::McrtFbSender::DataPtr(
                reinterpret_cast<uint8_t*>(alpha.release()), alphaDataDeleter);
    }

    const std::size_t alphaNumBytes = alphaSize * kFloatSize;

    return {alphaPtr, alphaNumBytes};
}

void
addBufferToSnapshotMessage(const arras::engine_tool::McrtFbSender::DataPtr& data,
                           const size_t dataSize,
                           kodachi::string_view bufferName,
                           const arras::engine_tool::ImgEncodingType encodingType,
                           kodachi::BackendBase::DataMessage& dataMessage,
                           kodachi::GroupBuilder& bufferGb)
{
    static const kodachi::StringAttribute kEncodingRgb888Attr("RGB888");
    static const kodachi::StringAttribute kEncodingRgba8Attr("RGBA8");
    static const kodachi::StringAttribute kEncodingFloatAttr("FLOAT");
    static const kodachi::StringAttribute kEncodingFloat2Attr("FLOAT2");
    static const kodachi::StringAttribute kEncodingFloat3Attr("FLOAT3");
    static const kodachi::StringAttribute kEncodingFloat4Attr("FLOAT4");
    static const kodachi::StringAttribute kEncodingUnknownAttr("UNKNOWN");

    bool dataIsFloat = false;
    kodachi::StringAttribute encodingAttr;

    switch (encodingType) {
    case arras::engine_tool::ImgEncodingType::ENCODING_RGB888:
        encodingAttr = kEncodingRgb888Attr;
        break;
    case arras::engine_tool::ImgEncodingType::ENCODING_RGBA8:
        encodingAttr = kEncodingRgba8Attr;
        break;
    case arras::engine_tool::ImgEncodingType::ENCODING_FLOAT:
        dataIsFloat = true;
        encodingAttr = kEncodingFloatAttr;
        break;
    case arras::engine_tool::ImgEncodingType::ENCODING_FLOAT2:
        dataIsFloat = true;
        encodingAttr = kEncodingFloat2Attr;
        break;
    case arras::engine_tool::ImgEncodingType::ENCODING_FLOAT3:
        dataIsFloat = true;
        encodingAttr = kEncodingFloat3Attr;
        break;
    case arras::engine_tool::ImgEncodingType::ENCODING_LINEAR_FLOAT:
        dataIsFloat = true;
        encodingAttr = kEncodingFloat4Attr;
        break;
    case arras::engine_tool::ImgEncodingType::ENCODING_UNKNOWN:
        // ProgMcrt data uses this type
        encodingAttr = kEncodingUnknownAttr;
        break;
    }

    kodachi::GroupAttribute bufferAttr;
    if (dataIsFloat) {
        const kodachi::FloatAttribute dataAttr =
                createFloatAttributeBuffer(data, dataSize);

        bufferAttr = kodachi::GroupAttribute ("enc", encodingAttr,
                                              "data", dataAttr,
                                              false);
    } else {
        std::shared_ptr<void> payload(data, static_cast<void*>(data.get()));

        const std::size_t payloadIdx = dataMessage.addPayload(std::move(payload));

        bufferAttr = kodachi::GroupAttribute ("enc", encodingAttr,
                                              "pIdx", kodachi::IntAttribute(payloadIdx),
                                              "size", kodachi::IntAttribute(dataSize),
                                              false);
    }

    bufferGb.set(kodachi::DelimiterEncode(std::string(bufferName)), bufferAttr);
}

void
appendOpDescription(kodachi::GroupBuilder& opsBuilder,
                    const char* opType,
                    const kodachi::GroupAttribute& args,
                    bool addSystemOpArgs = false)
{
    kodachi::GroupBuilder opBuilder;
    opBuilder.set("opType", kodachi::StringAttribute(opType));
    opBuilder.set("opArgs", args);
    if (addSystemOpArgs) {
        opBuilder.set("addSystemOpArgs", kodachi::IntAttribute(true));
    }
    opsBuilder.setWithUniqueName("op", opBuilder.build());
}

void
setUpLogging(const kodachi::GroupAttribute& rootAttrs)
{
    // get the current filter and adjust its logging level
    const kodachi::IntAttribute logLevelAttr =
            rootAttrs.getChildByName("moonrayGlobalStatements.log limit");
    const int logLevel = logLevelAttr.getValue(kKdLoggingSeverityInfo, false);
    kodachi::KodachiLogging::setSeverity(static_cast<KdLoggingSeverity>(logLevel));
}

float
toGB(int64_t bytes)
{
    constexpr float kGB(1024 * 1024 * 1024);

    return static_cast<float>(bytes) / kGB;
}

float
getPeakMemoryGB()
{
    struct rusage rusage;
    getrusage(RUSAGE_SELF, &rusage);

    // maximum resident set size
    // ru_maxrss is in kilobytes
    return toGB(rusage.ru_maxrss * 1024);
}

float
getResidentMemoryGB()
{
    const arras::util::ProcessStats processStats;
    return toGB(processStats.getProcessMemory());
}

void
traverse(kodachi::Traversal& traversal,
         kodachi_moonray::MoonrayRenderState& renderState,
         bool multiThreaded)
{
    while (traversal.isValid()) {
        const auto locations = traversal.getLocations();
        if (multiThreaded) {
            tbb::parallel_for_each(locations.begin(), locations.end(),
            [&renderState](const kodachi::KodachiRuntime::LocationData& locationData)
            {
                if (locationData.doesLocationExist()) {
                    renderState.processLocation(locationData.getLocationPathAttr(),
                                                locationData.getAttrs());
                }
            });
        } else {
            for (const auto& it : locations) {
                if (it.doesLocationExist()) {
                    renderState.processLocation(it.getLocationPathAttr(),
                                                it.getAttrs());
                }
            }
        }
    }
}

constexpr const size_t kMoonrayTileSize = COARSE_TILE_SIZE * COARSE_TILE_SIZE;
constexpr const size_t kRenderBufferPixelSize =
                                   sizeof(arras::fb_util::RenderBuffer::PixelType);

} // anonymous namespace

namespace kodachi_moonray {

MoonrayRenderBackend::MoonrayRenderBackend()
: kodachi::BackendBase()
{
}

bool
MoonrayRenderBackend::initialize(const kodachi::GroupAttribute& opTreeAttr)
{
    kodachi::GroupAttribute rootAttrs;
    {
        // create a temporary client and transaction so that we can cook root
        auto initializeRuntime = kodachi::KodachiRuntime::createRuntime();

        auto client = kodachi::optree_util::loadOpTree(initializeRuntime, opTreeAttr);

        if (!client) {
            KdLogError("Failed to load optree");
            return false;
        }

        // only use this root data to get the backend settings.
        // Since implicit resolvers and terminal ops haven't necessarily been appended
        // to the op tree, we can't assume root contains any other data we need
        auto rootData = client->cookLocation(kRoot, false);
        if (!rootData.doesLocationExist()) {
            KdLogError("could not cook the initial '/root'");
            return false;
        }

        rootAttrs = rootData.getAttrs();
    }

    setUpLogging(rootAttrs);

    kodachi::OpTreeBuilder otb;
    const auto ops = otb.merge(opTreeAttr);
    auto op = ops.back();

    const kodachi::GroupAttribute backendSettingsAttr =
            rootAttrs.getChildByName("kodachi.backendSettings");

    const kodachi::IntAttribute isLiveRenderAttr =
            backendSettingsAttr.getChildByName("isLiveRender");

    const bool isLiveRender = isLiveRenderAttr.getValue(false, false);

    const kodachi::GroupAttribute systemOpArgsAttr =
                backendSettingsAttr.getChildByName("systemOpArgs");

    const kodachi::IntAttribute appendImplicitResolversAttr =
                backendSettingsAttr.getChildByName("appendImplicitResolvers");

    if (appendImplicitResolversAttr.getValue(true, false)) {
        KdLogDebug("Appending implicit resolvers to optree");

        kodachi::GroupAttribute implicitResolvers =
                kodachi::optree_util::loadImplicitResolversOpCollection();
        if (systemOpArgsAttr.isValid()) {
            implicitResolvers = kodachi::optree_util::addSystemOpArgsToOpCollection(
                                           implicitResolvers, systemOpArgsAttr);
        }

        op = otb.appendOpChain(op, implicitResolvers).back();
    }

    const kodachi::StringAttribute cryptomatteManifestAttr =
            backendSettingsAttr.getChildByName("cryptomatte.cryptomatte_manifest");
    if (cryptomatteManifestAttr.isValid()) {
        mCryptomatteManifest = cryptomatteManifestAttr.getValue();
    }

    const kodachi::IntAttribute appendTerminalOpsAttr =
                backendSettingsAttr.getChildByName("appendTerminalOps");

    if (appendTerminalOpsAttr.getValue(true, false)) {
        KdLogDebug("Appending terminal ops to optree");

        kodachi::GroupAttribute configAttr;
        if (isLiveRender) {
            configAttr = kodachi::GroupAttribute(kType, kodachi::StringAttribute("terminalOps"),
                                                 "renderType", kodachi::StringAttribute("liveRender"),
                                                 false);
        } else {
            configAttr = kodachi::GroupAttribute(kType, kodachi::StringAttribute("terminalOps"),
                                                 false);
        }

        kodachi::GroupAttribute terminalOps = getStaticData(configAttr);
        if (systemOpArgsAttr.isValid()) {
            terminalOps = kodachi::optree_util::addSystemOpArgsToOpCollection(terminalOps,
                                                                 systemOpArgsAttr);
        }

        op = otb.appendOpChain(op, terminalOps).back();
    }

    const auto finalOpTree = otb.build(op);

    mKodachiRuntime = kodachi::KodachiRuntime::createRuntime();
    const auto client = kodachi::optree_util::loadOpTree(mKodachiRuntime, finalOpTree);

    mCookOp = client->getOp();

    if (isLiveRender) {
        KdLogDebug("Searching for MoonrayBarnDoorsShadowLinkResolve op in optree to monitor for live-render");

        auto monitorOp = mCookOp;

        while (monitorOp) {
            if (monitorOp->getOpArgs().first == "MoonrayBarnDoorsShadowLinkResolve") {
                mMonitorOp = monitorOp;
                break;
            }

            const auto inputs = monitorOp->getInputs();
            if (inputs.empty()) {
                throw std::runtime_error("optree is missing the 'MoonrayBarnDoorsShadowLinkResolve' op used to monitor for live updates");
            }

            monitorOp = inputs.front();
        }
    }

    // cook root again using the completed op tree
    rootAttrs = client->cookLocation(kRoot, false).getAttrs();
    if (!rootAttrs.isValid()) {
        KdLogError("could not cook the finalized '/root'");
        return false;
    }

    initializeFromRoot(rootAttrs, finalOpTree);

    return true;
}

void
MoonrayRenderBackend::start()
{
    if (!isStateInitialized()) {
        throw std::runtime_error("RenderBackend has not been initialized");
    }

    auto& renderState = getMoonrayRenderState();

    {
        // If we are in cacheCreationMode, we won't be in a position to
        // render, so enforce that skip render needs to be set.
        const kodachi::IntAttribute skipRenderAttr =
                mMoonrayGlobalSettings.getChildByName("skip render");

        const kodachi::IntAttribute cachePassAttr =
                mMoonrayGlobalSettings.getChildByName("cacheCreationMode");

        const bool isCachePass = skipRenderAttr.getValue(false, false)
                && cachePassAttr.getValue(false, false);

        if (isCachePass) {
            KdLogInfo("------ Kodachi Cache Pass Begin ------");
        } else {
            KdLogInfo("----- Kodachi Scene Build Begin ------");
        }
        KdLogInfo("")
        KdLogInfo("Using " << kodachi::getNumberOfThreads() << " threads")
        KdLogInfo("");
        KdLogInfo("Package Versions");
        KdLogInfo("    moonshine       : " << ::getenv("REZ_MOONSHINE_VERSION"));
        KdLogInfo("    moonray         : " << ::getenv("REZ_MOONRAY_VERSION"));
        KdLogInfo("    scene_rdl2      : " << ::getenv("REZ_SCENE_RDL2_VERSION"));
        KdLogInfo("    kodachi_moonray : " << ::getenv("REZ_KODACHI_MOONRAY_VERSION"));
        KdLogInfo("    kodachi         : " << ::getenv("REZ_KODACHI_VERSION"));
        KdLogInfo("");
        KdLogInfo("--------------------------------------");
        KdLogInfo("");

        const auto start = std::chrono::system_clock::now();

        preTraversal();

        if (renderState.isLiveRender()) {
            mMonitoringTraversal.reset(new kodachi::MonitoringTraversal(mKodachiRuntime, mCookOp, mMonitorOp));
            mMonitoringTraversal->setLeafType(kodachi::StringAttribute("rdl2"));
            traverse(*mMonitoringTraversal, renderState, mMultiThreadedSceneBuild);
        } else {
            kodachi::Traversal traversal(mKodachiRuntime, mCookOp);
            if (!isCachePass) {
                traverse(traversal, renderState, mMultiThreadedSceneBuild);
            } else {
                // TODO: Make the Traversal have a mode where it doesn't
                // create location data
                while (traversal.isValid()) {
                    KdLogDebug("CachePass: getLocations");
                    traversal.getLocations();
                }
            }
        }

        postTraversal();

        /////////////////////// Report build completion
        const auto end = std::chrono::system_clock::now();
        auto dur = end - start;

        const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(dur);
        const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(dur -= minutes);
        const auto millis  = std::chrono::duration_cast<std::chrono::milliseconds>(dur -= seconds);

        if (isCachePass) {
            KdLogInfo("---- Kodachi Cache Pass Complete -----");
        } else {
            KdLogInfo("---- Kodachi Scene Build Complete ----");
        }
        KdLogInfo("");
        KdLogInfo("time  : "
                  << std::setfill('0') << std::setw(2)
                  << minutes.count() << ":" << std::setw(2)
                  << seconds.count() << "." << std::setw(3)
                  << millis.count()  << " (mm:ss.ms)");

        KdLogInfo("peak memory   : " << std::setprecision(2)
                          << getPeakMemoryGB() << "GB");

        KdLogInfo("current memory: " << std::setprecision(2)
                  << getResidentMemoryGB() << "GB");

        KdLogInfo("");
        KdLogInfo("--------------------------------------");
        KdLogInfo("");

        if (isCachePass) {
            return;
        }
    }

    if (mRenderContext) {
        // Enable Athena logging for disk renders
        arras::rndr::RenderContext::LoggingConfiguration loggingConfiguration = mWriteToDisk ?
                arras::rndr::RenderContext::LoggingConfiguration::ATHENA_ENABLED :
                arras::rndr::RenderContext::LoggingConfiguration::ATHENA_DISABLED;

        // Initialize the render context
        std::stringstream initmessages; // dummy
        mRenderContext->initialize(initmessages, loggingConfiguration);

        if (renderState.isLiveRender()) {
            // Now that initial scene build is complete we can
            // disable material caching for optree deltas
            if (mKPOPStateKey.isValid()) {
                kodachi::GroupBuilder gb;
                gb.set("key", mKPOPStateKey)
                  .set("values.materialCachingEnabled", kodachi::IntAttribute(false));

                kodachi::AttributeFunctionUtil::run(kSetKPOPState, gb.build());
            }
        }

        startFrame();
    } else {
        KdLogDebug("Skipping Render");
    }
}

void
MoonrayRenderBackend::stop()
{
    if (isFrameRendering()) {
        stopFrame();
    }
}

void
MoonrayRenderBackend::setData(const kodachi::GroupAttribute& dataAttr)
{
    enum class DataType {
        OPTREE_DELTAS,
        REQUEST_STOP
    };

    static const std::unordered_map<kodachi::StringAttribute, DataType, kodachi::AttributeHash> kDataTypes
    {
        {"opTreeDeltas", DataType::OPTREE_DELTAS},
        {"requestStop" , DataType::REQUEST_STOP}
    };

    const kodachi::StringAttribute typeAttr(dataAttr.getChildByName(kType));

    const auto iter = kDataTypes.find(typeAttr);
    if (iter != kDataTypes.end()) {
        switch (iter->second) {
        case DataType::OPTREE_DELTAS:
        {
            const kodachi::GroupAttribute deltasAttr(dataAttr.getChildByName("deltas"));
            if (deltasAttr.getNumberOfChildren() > 0) {
                applyOpTreeDeltas(dataAttr.getChildByName("deltas"));
            }
            break;
        }
        case DataType::REQUEST_STOP:
        {
            requestStop();
            break;
        }
        }

    } else {
        KdLogDebug("setData - Unsupported data type: " << dataAttr.getXML());
    }
}

MoonrayRenderBackend::DataMessage
MoonrayRenderBackend::getData(const kodachi::GroupAttribute& query)
{
    enum class QueryType {
        RENDER_SNAPSHOT,
        RENDER_PROGRESS,
        ID_REGISTRATIONS,
        ID_PASS,
        IS_FRAME_RENDERING,
        IS_FRAME_READY_FOR_DISPLAY,
        IS_FRAME_COMPLETE,
        PIXEL_PICK
    };

    static const std::unordered_map<kodachi::StringAttribute, QueryType, kodachi::AttributeHash> kQueryTypes
    {
        { "renderSnapshot"        , QueryType::RENDER_SNAPSHOT    },
        { "renderProgress"        , QueryType::RENDER_PROGRESS    },
        { "idRegistrations"       , QueryType::ID_REGISTRATIONS   },
        { "idPass"                , QueryType::ID_PASS   },
        { "isFrameRendering"      , QueryType::IS_FRAME_RENDERING },
        { "isFrameReadyForDisplay", QueryType::IS_FRAME_READY_FOR_DISPLAY },
        { "isFrameComplete"       , QueryType::IS_FRAME_COMPLETE  },
        { "pixelPick"             , QueryType::PIXEL_PICK         },
    };

    const kodachi::StringAttribute typeAttr(query.getChildByName(kType));

    const auto iter = kQueryTypes.find(typeAttr);
    if (iter != kQueryTypes.end()) {
        switch (iter->second) {
        case QueryType::RENDER_SNAPSHOT:
            return snapshotBuffers();
        case QueryType::RENDER_PROGRESS:
            return DataMessage(kodachi::FloatAttribute(getRenderProgress()));
        case QueryType::ID_REGISTRATIONS:
        {
            auto& idPassManager = getMoonrayRenderState().getIdPassManager();
            if (idPassManager.isEnabled()) {
                return DataMessage(idPassManager.getIdRegistrations());
            } else {
                KdLogWarn("getData - IDPass is not enabled");
                return DataMessage{};
            }
            break;
        }
        case QueryType::ID_PASS:
            return getIdPass();
        case QueryType::IS_FRAME_RENDERING:
            return DataMessage(kodachi::IntAttribute(isFrameRendering()));
        case QueryType::IS_FRAME_READY_FOR_DISPLAY:
            return DataMessage(kodachi::IntAttribute(isFrameReadyForDisplay()));
        case QueryType::IS_FRAME_COMPLETE:
            return DataMessage(kodachi::IntAttribute(isFrameComplete()));
        case QueryType::PIXEL_PICK:
            const auto mode =
                    kodachi::IntAttribute(query.getChildByName("pickMode"));
            const auto coords =
                    kodachi::IntAttribute(query.getChildByName("pickCoords"));
            kodachi::IntConstVector xy = coords.getNearestSample(0);

            const int x = xy[0];
            const int y = xy[1];
            return DataMessage(kodachi::StringAttribute(pickPixel(x, y, mode.getValue())));
        }
    }

    KdLogDebug("Unsupported query type: " << query.getXML());

    return {};
}

void
MoonrayRenderBackend::initializeFromRoot(const kodachi::GroupAttribute& rootAttrs,
                                         const kodachi::GroupAttribute& opTreeAttr)
{
    // process backend-specific attrs first
    const kodachi::GroupAttribute backendAttrs =
                       rootAttrs.getChildByName("kodachi.backendSettings");

    if (backendAttrs.isValid()) {
        const kodachi::IntAttribute writeToDiskAttr =
                backendAttrs.getChildByName("writeToDisk");

        mWriteToDisk = writeToDiskAttr.getValue(false, false);

        const kodachi::IntAttribute numThreadsAttr =
                backendAttrs.getChildByName("numThreads");

        mNumThreads = numThreadsAttr.getValue(0, false);
        kodachi::setNumberOfThreads(mNumThreads);

        const kodachi::IntAttribute machineIdAttr =
                backendAttrs.getChildByName("machineId");

        mMachineId = machineIdAttr.getValue(-1, false);

        const kodachi::IntAttribute numMachinesAttr =
                backendAttrs.getChildByName("numMachines");

        mNumMachines = numMachinesAttr.getValue(-1, false);

        const kodachi::IntAttribute progressiveFrameModeAttr =
                backendAttrs.getChildByName("progressiveFrameMode");

        mIsProgressiveFrameMode = progressiveFrameModeAttr.getValue(false, false);
    }

    const kodachi::GroupAttribute partialLiveRenderAttr =
            rootAttrs.getChildByName("kodachi.live_render_locations");
    if (partialLiveRenderAttr.isValid()) {
        const kodachi::IntAttribute partialLiveRenderEnableAttr =
                partialLiveRenderAttr.getChildByName("enable");
        mPerformPartialLiveRender = (partialLiveRenderEnableAttr.getValue(0, false) == 1);

        if (mPerformPartialLiveRender) {
            const kodachi::StringAttribute partialLiveRenderMethodStrAttr =
                    partialLiveRenderAttr.getChildByName("method");
            if (partialLiveRenderMethodStrAttr.isValid()) {
                const std::string plrMethodStr = partialLiveRenderMethodStrAttr.getValue();

                if (plrMethodStr == "Include") {
                    mPartialLiveRenderMethod = kodachi::Traversal::PartialLiveRenderMethod::Include;
                }
                else if (plrMethodStr == "Exclude") {
                    mPartialLiveRenderMethod = kodachi::Traversal::PartialLiveRenderMethod::Exclude;
                }
            }
        }
    }

    mMoonrayRenderState.reset(new MoonrayRenderState(rootAttrs));
    auto& moonrayRenderState = getMoonrayRenderState();

    const kodachi::GroupAttribute idPassAttr = backendAttrs.getChildByName("idPass");

    if (idPassAttr.isValid()) {
        const kodachi::IntAttribute enabledAttr = idPassAttr.getChildByName("enabled");

        if (enabledAttr.getValue(false, false)) {
            const kodachi::StringAttribute idAttrNameAttr =
                    idPassAttr.getChildByName("idAttrName");
            const kodachi::StringAttribute bufferNameAttr =
                    idPassAttr.getChildByName("bufferName");

            if (idAttrNameAttr.isValid() && bufferNameAttr.isValid()) {
                moonrayRenderState.getIdPassManager().enable(idAttrNameAttr, bufferNameAttr);
            } else {
                KdLogWarn("Cannot enable ID Pass, both 'idAttrName' and 'bufferName' must be specified");
            }
        }
    }

    if (moonrayRenderState.isLiveRender()) {
        const kodachi::StringAttribute renderIDAttr =
                rootAttrs.getChildByName("kodachi.renderID");

        if (renderIDAttr.isValid()) {
            KdLogDebug("Using state key:" << renderIDAttr.getValueCStr());
            mKPOPStateKey = renderIDAttr;

            kodachi::GroupBuilder gb;
            gb.set("key", renderIDAttr)
              .set("values.materialCachingEnabled", kodachi::IntAttribute(true));

            kodachi::AttributeFunctionUtil::run(kSetKPOPState, gb.build());
        }
    }

    mMoonrayGlobalSettings = rootAttrs.getChildByName("moonrayGlobalStatements");

    const bool skipRender = kodachi::IntAttribute(
            mMoonrayGlobalSettings.getChildByName("skip render")).getValue(false, false);

    if (!skipRender) {
        mRenderOptions.reset(new arras::rndr::RenderOptions);
        mRenderOptions->setThreads(mNumThreads);

        const kodachi::IntAttribute checkpointActiveAttr =
                mMoonrayGlobalSettings.getChildByName("checkpoint_active");
        if (checkpointActiveAttr.isValid()) {
            mIsCheckpointActive = checkpointActiveAttr.getValue();
        }

        const kodachi::IntAttribute vectorizedAttr =
                mMoonrayGlobalSettings.getChildByName("vectorized");
        if (vectorizedAttr.isValid()) {
            mRenderOptions->setDesiredExecutionMode(
                    static_cast<arras::mcrt_common::ExecutionMode>(vectorizedAttr.getValue()));
        }

        const kodachi::IntAttribute idPassDelayAttr =
                mMoonrayGlobalSettings.getChildByName("id pass snapshot delay");

        mIdPassSnapshotDelay =
                std::chrono::milliseconds(idPassDelayAttr.getValue(1000, false));

        moonray_util::initGlobalRenderDriver(*mRenderOptions);

        mRenderContext.reset(new arras::rndr::RenderContext(*mRenderOptions));

        if (mWriteToDisk) {
            if (mIsCheckpointActive) {
                mRenderContext->setRenderMode(arras::rndr::RenderMode::PROGRESS_CHECKPOINT);
            } else {
                // Render each tile to completion before moving onto the next.
                mRenderContext->setRenderMode(arras::rndr::RenderMode::BATCH);
            }
        } else {
            // Render samples to the GUI as soon as they're available.
            mRenderContext->setRenderMode(arras::rndr::RenderMode::PROGRESSIVE);
        }


        moonrayRenderState.useExternalSceneContext(&mRenderContext->getSceneContext());
    } else {
        moonrayRenderState.useNewSceneContext();
    }

    // scene file output
    const kodachi::StringAttribute sceneFileOutputAttr =
            mMoonrayGlobalSettings.getChildByName("scene file output");
    const kodachi::string_view sceneFileOutput = sceneFileOutputAttr.getValueCStr("", false);
    if (!sceneFileOutput.empty()) {
        const kodachi::IntAttribute useRdlGeometryAttr =
                mMoonrayGlobalSettings.getChildByName("use_rdl_geometry");
        if (!useRdlGeometryAttr.getValue(false, false)) {
            moonrayRenderState.initializeKodachiRuntimeObject(opTreeAttr);
        }
    }

    // Remember if we want to multi-thread the scene build process or not
    const kodachi::IntAttribute multiThreadedSceneBuild =
            mMoonrayGlobalSettings.getChildByName("multi threaded");
    if (multiThreadedSceneBuild.isValid()) {
        mMultiThreadedSceneBuild = multiThreadedSceneBuild.getValue(true, false);
    }

    // Load any scene file inputs from the global settings before
    // traversing the scene graph
    const kodachi::StringAttribute rdlFiles =
            mMoonrayGlobalSettings.getChildByName("scene file input");
    if (rdlFiles.isValid()) {
        for (auto path : rdlFiles.getNearestSample(0.f)) {
            moonrayRenderState.loadRdlSceneFile(path);
        }
    }
}

void
MoonrayRenderBackend::preTraversal()
{
}

void
MoonrayRenderBackend::postTraversal()
{
    auto& renderState = getMoonrayRenderState();

    renderState.processingComplete();

    if (!renderState.isLiveRender()) {
        mKodachiRuntime->flushCaches();
        kodachi::PluginManager::flushPluginCaches();
    }

    // scene file output
    const kodachi::StringAttribute sceneFileOutputAttr(
            mMoonrayGlobalSettings.getChildByName("scene file output"));
    const std::string filePath = sceneFileOutputAttr.getValue(std::string{}, false);
    if (!filePath.empty()) {
        renderState.writeSceneToFile(filePath);
    }

    // Cryptomatte file output
    if (mWriteToDisk && !mCryptomatteManifest.empty()) {
        renderState.writeCryptomatteManifest(mCryptomatteManifest);
    }
}

void
MoonrayRenderBackend::applyOpTreeDeltas(const kodachi::GroupAttribute& deltasAttr)
{
    auto& renderState = getMoonrayRenderState();

    if (!renderState.isLiveRender()) {
        KdLogError("Optree deltas can only be applied during live renders");
        return;
    }

    if (!mMonitoringTraversal) {
        KdLogError("Cannot apply optree deltas, mMonitoringTraversal has not been initialized");
        return;
    }

    bool stopRequested = false;
    if (isFrameRendering() && mRenderContext) {
        mRenderContext->requestStop();
        stopRequested = true;
    }

    const auto updateStart = std::chrono::system_clock::now();
    KdLogDebug("Begin processing updates");

    mMonitoringTraversal->applyOpTreeDeltas(deltasAttr,
            mPerformPartialLiveRender,
            (mPartialLiveRenderMethod == kodachi::Traversal::PartialLiveRenderMethod::Exclude));

    const auto locationUpdates = mMonitoringTraversal->getLocations();
    KdLogInfo("(live render) processing " << locationUpdates.size() << " locationUpdates");

    // We have to stop the frame before applying updates since it calls
    // resetUpdates on the SceneContext
    if (stopRequested) {
        stopFrame();
    }

    if (mMultiThreadedSceneBuild) {
        tbb::parallel_for_each(locationUpdates.begin(), locationUpdates.end(),
        [&renderState](const kodachi::KodachiRuntime::LocationData& locationData)
        {
            if (locationData.doesLocationExist()) {
                renderState.processLocation(locationData.getLocationPathAttr(),
                                            locationData.getAttrs());
            } else {
                renderState.deleteLocation(locationData.getLocationPathAttr());
            }
        });
    } else {
        for (const auto& locationData : locationUpdates) {
            if (locationData.doesLocationExist()) {
                renderState.processLocation(locationData.getLocationPathAttr(),
                                            locationData.getAttrs());
            } else {
                renderState.deleteLocation(locationData.getLocationPathAttr());
            }
        }
    }

    renderState.processingComplete();

    // check moonrayGlobalStatements for the hidden delta file attribute
    const kodachi::IntAttribute deltaFiles =
            mMoonrayGlobalSettings.getChildByName("delta files");
    if (deltaFiles.isValid()) {
        const kodachi::StringAttribute sceneFileOutput =
                mMoonrayGlobalSettings.getChildByName("scene file output");

        if (sceneFileOutput.isValid()) {
            std::string sceneFilePath = sceneFileOutput.getValue();
            const auto fileExtensionLocation = sceneFilePath.find(".rdl");

            // Write the deltas at the same location and with the same name
            // as the scene file output.  Only difference being a delta count
            // before the extension
            // Ex. scene file output /usr/pic1/katana/tmp/scene.rdla
            //     delta files       /usr/pic1/katana/tmp/scene.1.rdla
            //                       /usr/pic1/katana/tmp/scene.2.rdla
            sceneFilePath.insert(fileExtensionLocation,
                                 "." + std::to_string(++mDeltaFileCount));
            renderState.writeSceneToFile(sceneFilePath);
        }
    }

    const auto updateEnd = std::chrono::system_clock::now();
    auto updateDuration = updateEnd - updateStart;

    const auto minutes = std::chrono::duration_cast<std::chrono::minutes>(updateDuration);
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(updateDuration -= minutes);
    const auto millis  = std::chrono::duration_cast<std::chrono::milliseconds>(updateDuration -= seconds);

    KdLogInfo("-- Delta processing time: "
              << std::setfill('0') << std::setw(2)
              << minutes.count() << ":" << std::setw(2)
              << seconds.count() << "." << std::setw(3)
              << millis.count()  << " (mm:ss.ms) --");

    // Always restart the render when a delta is received.
    mRenderContext->setSceneUpdated();

    startFrame();
}

MoonrayRenderBackend*
MoonrayRenderBackend::create()
{
    return new MoonrayRenderBackend();
}

kodachi::GroupAttribute
MoonrayRenderBackend::getStaticData(const kodachi::GroupAttribute& configAttr)
{
    static const kodachi::StringAttribute kTerminalOpsAttr("terminalOps");

    const kodachi::StringAttribute typeAttr = configAttr.getChildByName(kType);
    if (typeAttr == kTerminalOpsAttr) {
        return getTerminalOps(configAttr);
    }

    return {};
}

kodachi::GroupAttribute
MoonrayRenderBackend::getTerminalOps(const kodachi::GroupAttribute& configAttr)
{
    static const kodachi::StringAttribute kLiveRender("liveRender");
    static const kodachi::StringAttribute kDiskRender("diskRender");

    const kodachi::StringAttribute renderTypeAttr =
            configAttr.getChildByName("renderType");

    kodachi::GroupBuilder opsBuilder;

    ////////////////////////////////////////////////////////////////////////////
    ///////////////////////////////// Prune Ops ////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    // PruneRenderTraversal
    {
        static const kodachi::GroupAttribute kPruneRenderTraversalAttr(
                "CEL", kodachi::StringAttribute("(/root//*{ @pruneRenderTraversal == 1 })</*>"), false);

        appendOpDescription(opsBuilder, "Prune", kPruneRenderTraversalAttr);
    }

    {
        static const kodachi::GroupAttribute kPruneLightsAttr(
                "CEL", kodachi::StringAttribute(
                        "//*{hasattr(\"info.light.muteState\") and @info.light.muteState!=\"muteEmpty\" and @type==\"light\"}"), false);

        // For preview or disk renders, we can delete muted lights altogether
        if (renderTypeAttr.isValid() && renderTypeAttr != kLiveRender) {
            appendOpDescription(opsBuilder, "Prune", kPruneLightsAttr);
        }
    }

    // localize 'visible' on geometry types
    {
        kodachi::GroupBuilder opArgs;
        opArgs.set("attributeNames", kodachi::StringAttribute("visible"));
        opArgs.set("CEL", kodachi::StringAttribute("/root/world/geo//*"));
        appendOpDescription(opsBuilder, "MoonrayLocalizeAttribute", opArgs.build());
    }

    kodachi::GroupAttribute noArgs(true);

    // Create source geometry for mesh lights (must be done here for visible to work right)
    appendOpDescription(opsBuilder, "MoonrayMeshLightSourceCopy", noArgs);

    // Prune invisible objects
    appendOpDescription(opsBuilder, "MoonrayPruneInvisibleMesh", noArgs);

    ////////////////////////////////////////////////////////////////////////////
    /////////////////////////////// Material Ops ///////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    /// MaterialResolve Terminal Ops ///

    // Copy light filter material to reference location
    appendOpDescription(opsBuilder, "MoonrayLightFilterReferencesResolve", noArgs);

    // Replace barn doors with geometry
    appendOpDescription(opsBuilder, "MoonrayBarnDoorsResolve", noArgs);

    // includes lights and light filters
    appendOpDescription(opsBuilder, "MaterialToNetworkMaterial", noArgs);

    // Copy network material's linked parameters directly into node itself
    appendOpDescription(opsBuilder, "MoonrayCookMaterialInterface", noArgs);

    // Merge material assignments to leaf locations
    appendOpDescription(opsBuilder, "MoonrayFlattenMaterial", noArgs);

    // Apply matte materials
    appendOpDescription(opsBuilder, "MoonrayMatteMaterial", noArgs);

    // Now that materials are localized, we don't need material locations anymore
    {
        kodachi::GroupBuilder opArgs;
        opArgs.set("CEL", kodachi::StringAttribute("//Looks /root/world/geo//*{@type==\"materialgroup\" or @type==\"constraintgroup\"}"));
        appendOpDescription(opsBuilder, "Prune", opArgs.build());
    }

    {
        static const kodachi::GroupAttribute kLocalizeXformAttr(
                "excludeCel", kodachi::StringAttribute("//*{@type==\"group\" or @type==\"component\" or @type==\"subcomponent\" or @type==\"assembly\" or @type==\"faceset\"}"), false);

        appendOpDescription(opsBuilder, "LocalizeXform", kLocalizeXformAttr);
    }

    // Localize 'moonrayStatements' and 'moonrayMeshStatements'
    {
        static const std::vector<std::string> kMoonrayStatementsOpArgs{
            "moonrayStatements", "moonrayMeshStatements" };

        static const kodachi::GroupAttribute kMoonrayLocalizeAttributeAttr(
                "attributeNames", kodachi::StringAttribute(kMoonrayStatementsOpArgs), false);

        appendOpDescription(opsBuilder, "MoonrayLocalizeAttribute", kMoonrayLocalizeAttributeAttr);
    }

    // Volumes don't work with instancing, so disable
    // auto-instancing for geometry with volume shaders
    {
        kodachi::AttributeSetOpArgsBuilder asb;
        asb.setCEL(kodachi::StringAttribute("/root/world/geo//*{hasattr(\"material.terminals.moonrayVolume\")}"));
        asb.setAttr("moonrayStatements.sceneBuild.autoInstancing", kodachi::IntAttribute(false));
        appendOpDescription(opsBuilder, "AttributeSet", asb.build());
    }

    // Designate a geometry as a cutout by adding a CutoutMaterial
    appendOpDescription(opsBuilder, "MoonrayCutoutMaterialResolve", noArgs);

    // Localize 'cameraName' in the case that the scene is using the default value
    {
        kodachi::GroupBuilder opArgs;
        opArgs.set("attributeName", kodachi::StringAttribute("renderSettings.cameraName"));
        opArgs.set("CEL", kodachi::StringAttribute("/root"));
        appendOpDescription(opsBuilder, "LocalizeAttribute", opArgs.build());
    }

    if (renderTypeAttr == kLiveRender) {
        appendOpDescription(opsBuilder, "MoonrayLiveRenderCamera", noArgs);
    }

    // Moonray will throw an error due to bad EXR headers even for
    // interactive renders
    appendOpDescription(opsBuilder, "ExrHeaderMerge", noArgs);

    // Only applies to disk render
    if (renderTypeAttr == kDiskRender)
        appendOpDescription(opsBuilder, "ExrMergePrep", noArgs);

    ////////////////////////////////////////////////////////////////////////////
    /////////////////////////////////// KPOPs //////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////

    // Converts all valid locations to type 'rdl2'
    appendOpDescription(opsBuilder, "KPOPMeta", noArgs, true);

    // adds meta tags that KPOPMeta already added for non-renderer procedural types
    appendOpDescription(opsBuilder, "KPOPRendererProcedural", noArgs, false);

    // These 3 ops create locations under /root/__scenebuild
    {
        appendOpDescription(opsBuilder, "KPOPGeometrySet", noArgs, false);
        appendOpDescription(opsBuilder, "KPOPLayer", noArgs, false);
        appendOpDescription(opsBuilder, "KPOPSceneVariables", configAttr, true);
    }

    // localize 'lightList' on layerAssignable types
    {
        kodachi::GroupBuilder opArgs;
        opArgs.set("attributeNames", kodachi::StringAttribute("lightList"));
        opArgs.set("CEL", kodachi::StringAttribute("/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isLayerAssignable\")}"));
        appendOpDescription(opsBuilder, "MoonrayLocalizeAttribute", opArgs.build());
    }

    // Find the non-muted light filters for lights so that we get location events
    // for the lights when their filters' muteState changes
    appendOpDescription(opsBuilder, "KPOPLightFilterList", noArgs, false);

    // Set the shadow linking for barn doors
    appendOpDescription(opsBuilder, "MoonrayBarnDoorsShadowLinkResolve", noArgs);

    ////////////////////////////////////////////////////////////////////////////
    ///////////// ALL LOCALIZATION MUST BE COMPLETED BY THIS POINT /////////////
    ///////////// THE ABOVE OP WILL BE MONITORED FOR LIVE RENDERS  /////////////
    ////////////////////////////////////////////////////////////////////////////

    // do this before interpolate curves as we'd assume users of omitList
    // do not need to take interpolating curves into account when they choose which cv's to omit
    // currently CurveOmit forces linear on curves that no longer satisfy the bezier requirement
    // due to loss of cv's, so curve interpolation would ignore those curves anyways
    {
        const kodachi::GroupAttribute opArgs("CEL", kodachi::StringAttribute(
                R"(/root/world/geo//*{@type=="rdl2" and @rdl2.meta.kodachiType=="curves"})"), false);

        appendOpDescription(opsBuilder, "CurveOmit", opArgs, false);
    }
    appendOpDescription(opsBuilder, "KPOPWidthScale", noArgs);
    appendOpDescription(opsBuilder, "KPOPInterpolateCurves", noArgs);

    {
        kodachi::AttributeSetOpArgsBuilder asBuilder;
        asBuilder.setCEL("//*{not hasattr(\"kodachi.parallelTraversal\")}");

        kodachi::GroupBuilder opArgs;
        opArgs.set("script", kodachi::StringAttribute(
                "Interface.CopyAttr('kodachi.parallelTraversal', 'moonrayStatements.sceneBuild.parallelTraversal')"));
        asBuilder.addSubOp("OpScript.Lua", opArgs.build());

        appendOpDescription(opsBuilder, "AttributeSet", asBuilder.build());
    }

    // disable parallel traversal for children of Node-type SceneObjects
    {
        kodachi::AttributeSetOpArgsBuilder asBuilder;
        asBuilder.setCEL(kodachi::StringAttribute("/root/world//*{@type==\"rdl2\" and (hasattr(\"rdl2.meta.isNode\") or hasattr(\"rdl2.meta.isPart\"))}"));
        asBuilder.setAttr(kodachi::Traversal::kParallelTraversal, kodachi::IntAttribute(0));
        appendOpDescription(opsBuilder, "AttributeSet", asBuilder.build());
    }

    // instance omit
    {
        const kodachi::GroupAttribute opArgs("CEL", kodachi::StringAttribute(
                R"(/root/world/geo//*{@type=="rdl2" and @rdl2.meta.kodachiType=="instance array"})"), false);

        appendOpDescription(opsBuilder, "InstanceOmit", opArgs, false);
    }

    appendOpDescription(opsBuilder, "KPOPInstanceSource", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPInstance", noArgs, false);

    appendOpDescription(opsBuilder, "KPOPNode", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPArbitraryAttrWhitelist", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPGeometry", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPCamera", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPGroupGeometry", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPInstanceArray", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPMeshWindingOrder", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPRdlMeshGeometry", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPCurveGeometry", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPPointGeometry", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPOpenVdbGeometry", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPGenerateRequiredAttrs", noArgs, false);
    // Resolve any deferred ops that were added to the optree after the implicit resolvers
    appendOpDescription(opsBuilder, "OpResolve", noArgs, false);

    appendOpDescription(opsBuilder, "KPOPMaterial", noArgs, false);
    appendOpDescription(opsBuilder, "MoonrayCryptomatte", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPPrimitiveAttributes", noArgs, false);

    appendOpDescription(opsBuilder, "KPOPLightFilter", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPLight", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPLightSet", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPGeometrySetAssign", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPLayerAssign", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPAutoInstancing", noArgs, false);
    appendOpDescription(opsBuilder, "KPOPFinalize", noArgs, false);

    return opsBuilder.build();
}

kodachi::KdPluginStatus
MoonrayRenderBackend::setHost(kodachi::KdPluginHost* host)
{
    KodachiRuntimeWrapper::setHost(host);
    return kodachi::BackendBase::setHost(host);
}

void
MoonrayRenderBackend::requestStop()
{
    if (!isFrameRendering()) {
        return;
    }

    if (isMultiMachine()) {
        mRenderContext->requestStopRenderAtPassBoundary();
    } else {
        mRenderContext->requestStop();
    }
}

void
MoonrayRenderBackend::writeRenderOutput()
{
    try {
        arras::fb_util::HeatMapBuffer heatMapBuffer;
        mRenderContext->snapshotHeatMapBuffer(&heatMapBuffer, true, true);

        std::vector<arras::fb_util::VariablePixelBuffer> aovBuffers;
        std::vector<arras::fb_util::VariablePixelBuffer> displayFilterBuffers;
        mRenderContext->snapshotAovBuffers(aovBuffers, true, true);
        mRenderContext->snapshotDisplayFilterBuffers(displayFilterBuffers, true, true);

        auto rod = mRenderContext->getRenderOutputDriver();

        if (!rod) {
            KdLogError("RenderOutputDriver is null");
            return;
        }

        arras::fb_util::FloatBuffer weightBuffer;
        mRenderContext->snapshotWeightBuffer(&weightBuffer, true, true);

        rod->write(mRenderContext->getDeepBuffer(),
                   mRenderContext->getCryptomatteBuffer(),
                   &heatMapBuffer,
                   &weightBuffer,
                   nullptr,
                   aovBuffers,
                   displayFilterBuffers,
                   nullptr);

        const std::vector<std::string>& errors = rod->getErrors();
        if (!errors.empty()) {
            KdLogError("Errors from writing Moonray render outputs:");
            for (const auto &e: errors) {
                KdLogError(e);
            }
            KdLogError("RenderOutputDriver Errors:");
        }
        const std::vector<std::string>& infos = rod->getInfos();
        if (!infos.empty()) {
            std::cout << "\nMessages from writing Moonray render outputs:\n";
            for (const auto &i: infos) {
                std::cout << i << "\n";
            }
            std::cout <<"\n";
        }
    } catch (...) {
        KdLogError("Failed to write out render output.");
        throw;
    }
}

float
MoonrayRenderBackend::getRenderProgress() const
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return 0.f;
    }

    return mRenderContext->getFrameProgressFraction(nullptr, nullptr);
}

bool
MoonrayRenderBackend::isFrameReadyForDisplay() const
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return false;
    }

    return mRenderContext->isFrameReadyForDisplay();
}

bool
MoonrayRenderBackend::isFrameRendering() const
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return false;
    }

    return mRenderContext->isFrameRendering();
}

bool
MoonrayRenderBackend::isFrameComplete() const
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return false;
    }

    return mRenderContext->isFrameComplete();
}

std::string
MoonrayRenderBackend::pickPixel(const int x, const int y,
                                const int mode)
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return "";
    }

    switch (mode) {
    case 0: // Material
        {
            KdLogDebug("Picking Material");
            const arras::rdl2::Material* materials =
                    mRenderContext->handlePickMaterial(x, y);

            // Format
            // <material name>
            // ib7mbejodukcwachhocopidkby__PresentMetalMaterial_DwaMetalMaterial

            const std::string material = materials ? materials->getName() : "";
            return material;
        }
        break;
    case 1: // Light contributions
        {
            KdLogDebug("Picking Light Contributions");
            arras::shading::LightContribArray rdlLights;
            mRenderContext->handlePickLightContributions(x, y, rdlLights);

            std::stringstream ss;

            // Format
            // <light name>|<contribution value>,<light name>|<contribution value>
            // Ex. MoonrayRectLight_RectLight|0.0535155,TubeLightBlue_CylinderLight|0

            for (uint i = 0; i  < rdlLights.size(); ++i) {
                ss << rdlLights[i].first->getName();
                ss << "|";
                ss << rdlLights[i].second;
                ss << ",";
            }
            return ss.str();
        }
        break;
    case 2: // Geometry
        {
            KdLogDebug("Picking Geometry");
            const arras::rdl2::Geometry* geometry =
                    mRenderContext->handlePickGeometry(x, y);

            // Format
            // <geometry name>
            // /root/world/geo/cylinder/cylinder/unnamed_RdlMeshGeometry

            const std::string geom = geometry ? geometry->getName() : "";
            return geom;
        }
        break;
    case 3: // Geometry and part
        {
            KdLogDebug("Picking Geometry and Part");
            std::string parts;
            const arras::rdl2::Geometry* geometry =
                    mRenderContext->handlePickGeometryPart(x, y, parts);

            // Format
            // <geometry name>|<part name>
            // /root/world/geo/cylinder/cylinder/unnamed_RdlMeshGeometry::topCap1

            const std::string geom = geometry ? geometry->getName() : "";
            return geom + "::" + parts;
        }
        break;
    default:
        KdLogWarn("Invalid pick type");
        return "";
    }

    return "";
}

void
MoonrayRenderBackend::startFrame()
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return;
    }

    mRenderContext->startFrame();

    if (mFirstFrame) {
        mFirstFrame = false;

        // now that we have started the Frame we can get AOV information
        const auto rod = mRenderContext->getRenderOutputDriver();
        const unsigned int nro = rod->getNumberOfRenderOutputs();
        KdLogDebug("Num render outputs: " << nro);

        for (unsigned int i = 0; i < nro; ++i) {
            const auto renderOutput = rod->getRenderOutput(i);

            switch(renderOutput->getResult()) {
            case arras::rdl2::RenderOutput::Result::RESULT_BEAUTY:
                mBeautyRenderOutputName = renderOutput->getName();
                break;
            case arras::rdl2::RenderOutput::Result::RESULT_HEAT_MAP:
                mTimePerPixelRenderOutputs.push_back(renderOutput->getName());
                break;
            case arras::rdl2::RenderOutput::Result::RESULT_ALPHA:
                mAlphaRenderOutputs.push_back(renderOutput->getName());
                break;
            default:
                break;
            }
        }
    } else {
        if (mIsProgressiveFrameMode) {
            // we need to reset previous fb result to
            // create fresh activePixels information
            mFbSender.fbReset();
        }
    }

    syncFbSender();

    mSnapshotId = 0;

    resetIdPassSnapshotTimer();
}

void
MoonrayRenderBackend::stopFrame()
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return;
    }

    mRenderContext->stopFrame();

    if (isFrameComplete() && mWriteToDisk) {
        writeRenderOutput();
    }
}

MoonrayRenderBackend::DataMessage
MoonrayRenderBackend::snapshotBuffers()
{
    if (!mRenderContext) {
        KdLogWarn("RenderContext not initialized");
        return {};
    }

    auto& renderState = getMoonrayRenderState();

    // get the state of the render first in case is completes after we
    // have taken the snapshot
    const bool frameComplete = isFrameComplete();
    const float progress = getRenderProgress();

    kodachi::GroupBuilder snapshotGb;

    // TODO: doParallel should only be true for REALTIME rendering.
    // Determine if that is ever relevant for us.
    constexpr bool kDoParallel = false;

    // TODO: Are there cases where we want to return the PixelInfo buffer?
    constexpr bool kDoPixelInfo = false;

    const bool multiMachine = isMultiMachine();

    const bool coarsePassesComplete = mRenderContext->areCoarsePassesComplete();

    const auto& idPassManager = renderState.getIdPassManager();

    // The ID pass will be unchanged once coarse passes are complete
    const bool snapshotIdPass = !mIsProgressiveFrameMode &&
            idPassManager.isEnabled() &&
            (!coarsePassesComplete || mLastSnapshotWasCoarse);

    mLastSnapshotWasCoarse = !coarsePassesComplete;

    // Return true if the AOV should be snapshotted
    // We are going to use the beauty buffer as an RGBA AOV, so skip the creation
    // of the actual RGB AOV.
    const auto intervalFunc = [&](const std::string& bufName) -> bool
            {
                if (bufName == mBeautyRenderOutputName) {
                    return false;
                }

                if (bufName == idPassManager.getBufferName()) {
                    return snapshotIdPass;
                }

                return true;
            };

    DataMessage snapshotMessage;
    kodachi::GroupBuilder bufferGb;

    const auto addBeautyFunc = [&](const arras::engine_tool::McrtFbSender::DataPtr& data,
                                   const size_t dataSize,
                                   const char* aovName,
                                   const arras::engine_tool::ImgEncodingType encodingType)
            {
                kodachi::string_view bufferName;
                if (mBeautyRenderOutputName.empty()) {
                    bufferName = aovName;
                } else {
                    bufferName = mBeautyRenderOutputName;
                }

                addBufferToSnapshotMessage(data, dataSize, bufferName,
                                           encodingType, snapshotMessage, bufferGb);
            };

    const auto addAovFunc = [&](const arras::engine_tool::McrtFbSender::DataPtr& data,
                                const size_t dataSize,
                                const char* aovName,
                                const arras::engine_tool::ImgEncodingType encodingType)
            {
                addBufferToSnapshotMessage(data, dataSize, aovName,
                                           encodingType, snapshotMessage, bufferGb);
            };

    if (mIsProgressiveFrameMode) {

        const bool directToClient   = !multiMachine;
        const bool lowPrecisionMode = !coarsePassesComplete;
        arras::engine_tool::McrtFbSender::PrecisionMode precisionMode =
            arras::engine_tool::McrtFbSender::PrecisionMode::F32;
        if (!coarsePassesComplete) { // 0:coarsePass 1:CoarsePassDone
            if (!mFbSender.beautyHDRITest()) {
                // beauty buffer does not include HDRI pixels
                precisionMode = arras::engine_tool::McrtFbSender::PrecisionMode::UC8;
            } else {
                // beauty buffer has HDRI pixels
                precisionMode = arras::engine_tool::McrtFbSender::PrecisionMode::H16;
            }
        }


        // ProgressiveFrame snapshot
        mFbSender.snapshotDelta(*mRenderContext, kDoPixelInfo,
                                kDoParallel, mSnapshotId, intervalFunc);
        // beauty
        mFbSender.addBeautyToProgressiveFrame(precisionMode, directToClient, addBeautyFunc);

        // heatMap
        if (mFbSender.getHeatMapStatus() && !mFbSender.getHeatMapSkipCondition()) {
            mFbSender.addHeatMapToProgressiveFrame(directToClient,
            [&](const arras::engine_tool::McrtFbSender::DataPtr& data,
                const size_t dataSize,
                const char* aovName,
                const arras::engine_tool::ImgEncodingType encodingType)
                {
                    // The FbSender names the heatmap after the AOV that wants the heatmap.
                    // 'addRenderOutputToProgressiveFrame' adds a Reference-Type buffer
                    // with the same name. the Snapshot message can't handle buffers with
                    // the same name, so rename the heatmap.
                    addBufferToSnapshotMessage(data, dataSize, "__heatmap__",
                                               encodingType, snapshotMessage, bufferGb);
                });
        }

        // AOVs
        mFbSender.addRenderOutputToProgressiveFrame(precisionMode, directToClient, addAovFunc);

        mFbSender.addLatencyLog(addAovFunc);

        // 0:coarsePass 1:CoarsePassDone 2:unknown
        const int32_t coarsePass = lowPrecisionMode ? 0 : 1;
        snapshotGb.set("coarsePass", kodachi::IntAttribute(coarsePass));
        snapshotGb.set("isProgressive", kodachi::IntAttribute(true));
        {
            union TimeUnion {
                uint64_t u64;
                int32_t i32[2];
            } u;

            u.u64 = mFbSender.getSnapshotStartTime();

            snapshotGb.set("snapshotTime", kodachi::IntAttribute(&u.i32[0], 2, 1));
        }
    } else {
        constexpr bool kUntileDuringSnapshot = true;

        // RendererFrame snapshot
        mFbSender.snapshot(*mRenderContext, kDoPixelInfo, kUntileDuringSnapshot,
                           kDoParallel, mSnapshotId, intervalFunc);
        // beauty
        {
            arras::engine_tool::McrtFbSender::DataPtr beautyData;
            size_t beautySize;
            const char* beautyName;
            arras::engine_tool::ImgEncodingType beautyEncoding;

            mFbSender.addBeautyToRenderedFrame(kDoParallel,
                    [&](const arras::engine_tool::McrtFbSender::DataPtr& data,
                    const size_t dataSize,
                    const char* aovName,
                    const arras::engine_tool::ImgEncodingType encodingType)
                    {
                        beautyData = data;
                        beautySize = dataSize;
                        beautyName = aovName;
                        beautyEncoding = encodingType;
                    });

            addBeautyFunc(beautyData, beautySize, beautyName, beautyEncoding);

            // McrtFbSender ignores Alpha AOVs so copy the alpha channel from beauty
            // and manually add the Alpha AOVs
            if (!mAlphaRenderOutputs.empty()) {
                const auto alphaData = createAlphaData(beautyData, beautySize);

                for (const auto& alphaName : mAlphaRenderOutputs) {
                    addBufferToSnapshotMessage(
                            alphaData.first, alphaData.second,
                            alphaName, arras::engine_tool::ImgEncodingType::ENCODING_FLOAT,
                            snapshotMessage, bufferGb);
                }
            }
        }

        // heatmap
        if (mFbSender.getHeatMapStatus() && !mFbSender.getHeatMapSkipCondition()) {
            arras::engine_tool::McrtFbSender::DataPtr heatmapData;
            size_t heatmapSize;
            arras::engine_tool::ImgEncodingType heatmapEncoding;

            mFbSender.addHeatMapToRenderedFrame(
                    [&](const arras::engine_tool::McrtFbSender::DataPtr& data,
                    const size_t dataSize,
                    const char* aovName,
                    const arras::engine_tool::ImgEncodingType encodingType)
                    {
                        heatmapData = data;
                        heatmapSize = dataSize;
                        heatmapEncoding = encodingType;
                    });

            for (const auto& tppName : mTimePerPixelRenderOutputs) {
                addBufferToSnapshotMessage(heatmapData, heatmapSize, tppName,
                        heatmapEncoding, snapshotMessage, bufferGb);
            }
        }

        // AOVs
        mFbSender.addRenderOutputToRenderedFrame(addAovFunc);
    }

    const auto& apertureWindow = renderState.getApertureWindow();
    const auto& regionWindow = renderState.getRegionWindow();
    const auto& subViewport = renderState.getSubViewport();

    snapshotGb
        .set("id", kodachi::IntAttribute(mSnapshotId++))
        .set("avp", kodachi::IntAttribute(&apertureWindow.mMinX, 4, 1))
        .set("rvp", kodachi::IntAttribute(&regionWindow.mMinX, 4, 1))
        .set("svp", kodachi::IntAttribute(&subViewport.mMinX, 4, 1))
        .set("prog", kodachi::FloatAttribute(progress))
        .set("bufs", bufferGb.build())
        ;

    if (frameComplete) {
        snapshotGb.set("frameComplete", kodachi::IntAttribute(true));
        mRenderContext->stopFrame();
    }

    snapshotMessage.mAttr = snapshotGb.build();
    return snapshotMessage;
}

void
MoonrayRenderBackend::resetIdPassSnapshotTimer()
{
    if (getMoonrayRenderState().getIdPassManager().isEnabled()) {
        mNextIdPassSnapshotTime = std::chrono::system_clock::now() + mIdPassSnapshotDelay;
    }
}

MoonrayRenderBackend::DataMessage
MoonrayRenderBackend::getIdPass()
{
    if (!mIsProgressiveFrameMode) {
        return DataMessage{};
    }

    const auto now = std::chrono::system_clock::now();
    if (now < mNextIdPassSnapshotTime) {
        return DataMessage{};
    }

    KdLogDebug("Begin ID Pass Snapshot");

    static const kodachi::StringAttribute kEncodingInt3Attr("INT3");

    auto& renderState = getMoonrayRenderState();

    const auto& apertureWindow = renderState.getApertureWindow();
    const auto& regionWindow = renderState.getRegionWindow();
    const auto& subViewport = renderState.getSubViewport();

    auto& idPassManager = renderState.getIdPassManager();

    const auto tiles = mRenderContext->getTiles();

    const unsigned svMinX = subViewport.mMinX;
    const unsigned svMinY = subViewport.mMinY;
    const unsigned svMaxX = subViewport.mMaxX;
    const unsigned svMaxY = subViewport.mMaxY;

    const std::size_t numTiles = tiles->size();
    kodachi::GroupBuilder tilesBuilder;
    tilesBuilder.reserve(numTiles);

    const std::string idName = "bufs." + idPassManager.getBufferName();

    // For multi-machine cases, we only want to get the IDs for the objects
    // at the pixels we are responsible for rendering.
    for (std::size_t i = 0; i < numTiles; ++i) {
        const auto& tile = tiles->at(i);

        const int32_t minX = std::max(svMinX, tile.mMinX);
        const int32_t minY = std::max(svMinY, tile.mMinY);
        const int32_t maxX = std::min(svMaxX, tile.mMaxX);
        const int32_t maxY = std::min(svMaxY, tile.mMaxY);
        const std::array<int32_t, 4> vpArray{minX, minY, maxX, maxY};

        const int width = maxX - minX;
        const int height = maxY - minY;

        const std::size_t size = 3 * width * height;
        std::unique_ptr<int32_t[]> tileBuffer(new int32_t[size]);
        int32_t* iter = tileBuffer.get();

        for (int y = minY; y < maxY; ++y) {
            for (int x = minX; x < maxX; ++x) {
                iter[0] = 0;
                ++iter;

                uint64_t id = 0;

                std::string part;
                const arras::rdl2::Geometry* geo =
                        mRenderContext->handlePickGeometryPart(x, y, part);

                if (geo) {
                    id = idPassManager.getGeometryId(geo, part);
                }

                std::memcpy(iter, &id, sizeof(uint64_t));
                iter += 2;
            }
        }

        kodachi::GroupBuilder tileBuilder;
        tileBuilder.set("vp", kodachi::IntAttribute(vpArray.data(), vpArray.size(), 1));

        kodachi::GroupAttribute tileAttr("enc", kEncodingInt3Attr,
                                         "data", kodachi::ZeroCopyIntAttribute::create(std::move(tileBuffer), size),
                                         false);
        tileBuilder.set(idName, tileAttr);

        // set with unique name get very slow when the number of children
        // gets large, so use the index
        tilesBuilder.set(std::to_string(i), tileBuilder.build());
    }

    kodachi::GroupBuilder idPassBuilder;
    idPassBuilder
    .set("avp", kodachi::IntAttribute(&apertureWindow.mMinX, 4, 1))
    .set("rvp", kodachi::IntAttribute(&regionWindow.mMinX, 4, 1))
    .set("tiles", tilesBuilder.build());

    mNextIdPassSnapshotTime = std::chrono::system_clock::time_point::max();

    KdLogDebug("End ID Pass Snapshot");

    return DataMessage(idPassBuilder.build());
}

void
MoonrayRenderBackend::syncFbSender()
{
    auto& moonrayRenderState = getMoonrayRenderState();

    const auto& regionWindow = moonrayRenderState.getRegionWindow();
    const int regionWindowWidth = regionWindow.width();
    const int regionWindowHeight = regionWindow.height();

    if (mFbSender.getWidth() != regionWindowWidth
            || mFbSender.getHeight() != regionWindowHeight) {
        // Either we are initializing for the first time of the data window
        // has changed
        mFbSender.init(regionWindowWidth, regionWindowHeight);

        if (mRenderContext->hasPixelInfoBuffer()) {
            mFbSender.initPixelInfo(true);
        }

        mFbSender.setMachineId(mMachineId);

        // initialize render outputs
        auto rod = mRenderContext->getRenderOutputDriver();
        const unsigned int nro = rod->getNumberOfRenderOutputs();
        if (nro == 0) {
            mFbSender.initRenderOutput(nullptr);
        } else {
            mFbSender.initRenderOutput(rod);
        }

        if (!mIsProgressiveFrameMode) {
            mFbSender.initWorkNonProgressiveFrameMode(
                    arras::engine_tool::ImgEncodingType::ENCODING_LINEAR_FLOAT);
        }
    }

    if (getMoonrayRenderState().isROIEnabled()) {
        const auto& subViewport = getMoonrayRenderState().getSubViewport();

        if (!mFbSender.getRoiViewportStatus() || subViewport != mFbSender.getRoiViewport()) {
            // TODO: McrtFbSender::setRoiViewport doesn't take ROI by const reference
            auto tmpViewport = subViewport;
            mFbSender.setRoiViewport(tmpViewport);

            if (!mIsProgressiveFrameMode) {
                mFbSender.initWorkRoiNonProgressiveFrameMode();
                mFbSender.initWorkRoiRenderOutputNonProgressiveFrameMode();
            }
        }
    } else {
        mFbSender.resetRoiViewport();
    }
}

} // namespace kodachi_moonray

// define and register the plugin
namespace {
using namespace kodachi_moonray;

DEFINE_KODACHI_BACKEND_PLUGIN(MoonrayRenderBackend);
} // anonymous namespace

void registerPlugins() {
    REGISTER_PLUGIN(MoonrayRenderBackend, "MoonrayRenderBackend", 0, 1);
}

