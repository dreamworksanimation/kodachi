// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include "KodachiRenderMethod.h"

// local
#include "MonitorFrame.h"
#include "MoonrayRender.h"

// katana
#include <FnRender/plugin/IdSenderFactory.h>
#include <FnRendererInfo/plugin/RenderMethod.h>
#include <FnGeolib/util/Path.h>
#include <FnAsset/FnDefaultFileSequencePlugin.h>

// kodachi
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/OpTreeBuilder.h>
#include <kodachi/OpTreeUtil.h>
#include <kodachi/Traversal.h>

// tbb
#include <tbb/task_scheduler_init.h>

// stl
#include <fstream>

namespace {
KdLogSetup("KodachiRenderMethod");

std::chrono::milliseconds
getDefaultSnapshotInterval(mfk::KatanaRenderMethod method, const kodachi::GroupAttribute& globalSettings)
{
    switch(method) {
        case mfk::KatanaRenderMethod::DISK: {
            return std::chrono::milliseconds(500);
        }
        case mfk::KatanaRenderMethod::LIVE: {
            const kodachi::FloatAttribute FPSAttr = globalSettings.getChildByName("live_render_fps");
            float fps = FPSAttr.getValue(50, false);
            if (fps <= 0.0) {
                fps = 50;
            }
            const int snapshotTime = 1000 / fps;
            return std::chrono::milliseconds(snapshotTime);
        }
        default: {
            const kodachi::FloatAttribute FPSAttr = globalSettings.getChildByName("preview_render_fps");
            float fps = FPSAttr.getValue(10, false);
            if (fps <= 0.0) {
                fps = 50;
            }
            const int snapshotTime = 1000 / fps;
            return std::chrono::milliseconds(snapshotTime);
        }
    }
}

bool
isFrameRendering(kodachi::BackendClient& client)
{
    static const kodachi::GroupAttribute kQuery("type",
            kodachi::StringAttribute("isFrameRendering"), false);

    const auto data = client.getData(kQuery);
    const kodachi::IntAttribute isFrameRenderingAttr(data.getAttr());

    return isFrameRenderingAttr.getValue(false, false);
}

bool
isFrameReadyForDisplay(kodachi::BackendClient& client)
{
    static const kodachi::GroupAttribute kQuery("type",
            kodachi::StringAttribute("isFrameReadyForDisplay"), false);

    const auto data = client.getData(kQuery);
    const kodachi::IntAttribute isFrameReadyForDisplay(data.getAttr());

    return isFrameReadyForDisplay.getValue(false, false);
}

bool
isFrameComplete(kodachi::BackendClient& client)
{
    static const kodachi::GroupAttribute kQuery("type",
            kodachi::StringAttribute("isFrameComplete"), false);

    const auto data = client.getData(kQuery);
    const kodachi::IntAttribute isFrameCompleteAttr(data.getAttr());

    return isFrameCompleteAttr.getValue(false, false);
}

float
getRenderProgress(kodachi::BackendClient& client)
{
    static const kodachi::GroupAttribute kQuery("type",
            kodachi::StringAttribute("renderProgress"), false);

    const auto data = client.getData(kQuery);
    const kodachi::FloatAttribute renderProgressAttr(data.getAttr());

    return renderProgressAttr.getValue(0.f, false);
}

kodachi::GroupAttribute
getIDRegistrations(kodachi::BackendClient& client)
{
    static const kodachi::GroupAttribute kQuery("type",
            kodachi::StringAttribute("idRegistrations"), false);

    const auto data = client.getData(kQuery);

    return data.getAttr();
}

bool
isRez2Environment()
{
    const char* rezVersionEnvVar = ::getenv("REZ_VERSION");

    return rezVersionEnvVar && rezVersionEnvVar[0] == '2';
}

kodachi::GroupAttribute
buildArrasSettings(const kodachi::GroupAttribute& moonrayArrasSettingsAttr,
                   const kodachi::StringAttribute& rezContextFileAttr,
                   bool idPassEnabled)
{
    const kodachi::StringAttribute datacenterAttr =
            moonrayArrasSettingsAttr.getChildByName("datacenter");

    const kodachi::StringAttribute environmentAttr =
            moonrayArrasSettingsAttr.getChildByName("environment");

    const kodachi::IntAttribute connectionTimeoutAttr =
            moonrayArrasSettingsAttr.getChildByName("connectionTimeout");

    const kodachi::IntAttribute connectionRetryCountAttr =
            moonrayArrasSettingsAttr.getChildByName("connectionRetryCount");

    const kodachi::IntAttribute connectionRetryDelayAttr =
            moonrayArrasSettingsAttr.getChildByName("connectionRetryDelay");

    const kodachi::StringAttribute loggingModeAttr =
            moonrayArrasSettingsAttr.getChildByName("logging mode");

    const kodachi::IntAttribute renderComputationsAttr =
            moonrayArrasSettingsAttr.getChildByName("render computations");

    const kodachi::IntAttribute coresPerComputationAttr =
            moonrayArrasSettingsAttr.getChildByName("cores per computation");

    const kodachi::StringAttribute coresPerComputationModeAttr =
            moonrayArrasSettingsAttr.getChildByName("cores per computation mode");

    const kodachi::IntAttribute memoryPerComputationAttr =
            moonrayArrasSettingsAttr.getChildByName("memory per computation");

    const kodachi::StringAttribute memoryUnitsAttr =
            moonrayArrasSettingsAttr.getChildByName("memory units");

    const kodachi::FloatAttribute framesPerSecondAttr =
            moonrayArrasSettingsAttr.getChildByName("frames per second");

    const kodachi::IntAttribute renderFromCwdAttr =
            moonrayArrasSettingsAttr.getChildByName("render from cwd");

    const kodachi::StringAttribute cwdOverrideAttr =
            moonrayArrasSettingsAttr.getChildByName("cwd override");

    const kodachi::IntAttribute useLocalMachineAttr =
            moonrayArrasSettingsAttr.getChildByName("use local machine");

    const kodachi::StringAttribute productionAttr =
            moonrayArrasSettingsAttr.getChildByName("production");

    const kodachi::StringAttribute sequenceAttr =
            moonrayArrasSettingsAttr.getChildByName("sequence");

    const kodachi::StringAttribute shotAttr =
            moonrayArrasSettingsAttr.getChildByName("shot");

    const kodachi::StringAttribute assetGroupAttr =
            moonrayArrasSettingsAttr.getChildByName("asset group");

    const kodachi::StringAttribute assetAttr =
            moonrayArrasSettingsAttr.getChildByName("asset");

    const kodachi::StringAttribute departmentAttr =
            moonrayArrasSettingsAttr.getChildByName("department");

    const kodachi::StringAttribute teamAttr =
            moonrayArrasSettingsAttr.getChildByName("team");

    const kodachi::GroupAttribute envVarsAttr =
            moonrayArrasSettingsAttr.getChildByName("environmentVariables");

    kodachi::GroupBuilder gb;
    gb.set("computationBackend", kodachi::StringAttribute("MoonrayRenderBackend"));

    // These settings can be forwarded as-is to the ArrasRenderBackend
    gb.set("datacenter", datacenterAttr)
      .set("environment", environmentAttr)
      .set("connectionTimeout", connectionTimeoutAttr)
      .set("connectionRetryCount", connectionRetryCountAttr)
      .set("connectionRetryDelay", connectionRetryDelayAttr)
      .set("loggingMode", loggingModeAttr)
      .set("numBackendComputations", renderComputationsAttr)
      .set("coresPerComputation", coresPerComputationAttr)
      .set("production", productionAttr)
      .set("sequence", sequenceAttr)
      .set("shot", shotAttr)
      .set("assetGroup", assetGroupAttr)
      .set("asset", assetAttr)
      .set("department", departmentAttr)
      .set("team", teamAttr)
      ;

    // Memory reservations for Arras computations are specified in MB, but
    // users prefer GB, so do the conversion if necessary
    if (memoryUnitsAttr == "GB" && memoryPerComputationAttr.isValid()) {
        gb.set("memoryPerComputation",
                kodachi::IntAttribute(memoryPerComputationAttr.getValue() * 1024));
    } else {
        gb.set("memoryPerComputation", memoryPerComputationAttr);
    }

    if (coresPerComputationModeAttr == "exact") {
        // pass the int value as it is
        gb.set("coresPerComputation", coresPerComputationAttr);
    } else {
        // specify a range
        gb.set("minCoresPerComputation", coresPerComputationAttr);
        gb.set("maxCoresPerComputation", kodachi::StringAttribute("*"));
    }

    // ArrasRenderBackend takes snapshotInterval instead of FPS
    if (framesPerSecondAttr.isValid()) {
        const int32_t framesPerSecond = framesPerSecondAttr.getValue();
        if (framesPerSecond <= 0 || framesPerSecond > 100) {
            KdLogError("Invalid 'frames per second' value, using default");
        } else {
            gb.set("framesPerSecond", framesPerSecondAttr);
        }
    }

    if (renderFromCwdAttr.getValue(false, false)) {
        char* cwd = ::getcwd(nullptr, 0);
        if (!cwd) {
            KdLogError("Unable to get Current Working Directory.");
        } else {
            gb.set("workingDirectory", kodachi::StringAttribute(cwd));
            free(cwd);
        }
    } else if (cwdOverrideAttr.isValid()) {
        gb.set("workingDirectory", cwdOverrideAttr);
    }

    if (!useLocalMachineAttr.getValue(false, false)) {
        gb.set("noLocal", kodachi::IntAttribute(true));
    }

    // TODO: We need to send the path to the rez-context file
    // instead of the contents since it is currently too large for the Arras
    // PROD database to handle.
    bool sendRezResolve = true;
    if (rezContextFileAttr.isValid()) {
        sendRezResolve = false;
        gb.set("rezContextFile", rezContextFileAttr);
    } else {
        // Assume the Katana session has been started with all of the packages
        // necessary to render the scene. Send the rez context to remove the
        // need for the KodachiRenderComputations to rez-env to the same environment
        const char* rezContextFileEnv = ::getenv("REZ_CONTEXT_FILE");
        if (rezContextFileEnv) {

            bool openFile = true;
           // Ensure the path is absolute
            if (!std::filesystem::path(rezContextFileEnv).is_absolute()) {
                openFile = false;
            }

            std::filesystem::path normalizedPath = std::filesystem::path(rezContextFileEnv).lexically_normal();
            // Check if the file exists and is a regular file
            if (!std::filesystem::is_regular_file(normalizedPath)) {
                openFile = false;
            }
            // Check for path traversal sequences
            if (normalizedPath.string().find("..") != std::string::npos) {
                openFile = false;
            }

            if (openFile) {
                std::ifstream rezContextFileStream(rezContextFileEnv);
                if (rezContextFileStream) {
                    std::stringstream ss;
                    ss << rezContextFileStream.rdbuf();
                    std::string rezContextFile = ss.str();
                    gb.set("rezContext",
                        kodachi::ZeroCopyStringAttribute::create({std::move(rezContextFile)}));
                    sendRezResolve = false;
                }
            }
        }
    }

    if (sendRezResolve) {
        //KdLogInfo("Error getting rez context, falling back to sending REZ_RESOLVE");

        // Couldn't get the rez context so send the package versions instead
        const char* rezResolveEnv = ::getenv("REZ_RESOLVE");
        const char* rezPackagesPathEnv = ::getenv("REZ_PACKAGES_PATH");
        if (!rezResolveEnv || !rezPackagesPathEnv) {
            KdLogError("Could not get 'REZ_RESOLVE' or 'REZ_PACKAGES_PATH' environment variables");
        } else {
            gb.set("rezPackages", kodachi::StringAttribute(rezResolveEnv));
            gb.set("rezPackagesPrepend", kodachi::StringAttribute(rezPackagesPathEnv));
        }
    }

    if (isRez2Environment()) {
        // packagingSystem defaults to rez1
        gb.set("packagingSystem", kodachi::StringAttribute("rez2"));
    }

    // TODO: allowing the ClientFbReceiver to flip the buffers for us
    // and sending them as one large message causes the Katana monitor to stop
    // updating.
    //gb.set("flipBuffersVertical", kodachi::IntAttribute(true));

    if (idPassEnabled) {
        gb.set("additionalGetData.idRegistration.scope",
                kodachi::GroupAttribute("machineId",
                        kodachi::IntAttribute(0), false));
        gb.set("additionalGetData.idRegistration.query",
                kodachi::GroupAttribute("type",
                        kodachi::StringAttribute("idRegistrations"), false));
        gb.set("additionalGetData.idPass.scope",
                kodachi::GroupAttribute("allMachines",
                        kodachi::IntAttribute(true), false));
        gb.set("additionalGetData.idPass.query",
                kodachi::GroupAttribute("type",
                        kodachi::StringAttribute("idPass"), false));
    }

    kodachi::GroupBuilder evgb;

    // handle environment variables outside of the rez context
    if (envVarsAttr.isValid()) {
        const kodachi::StringAttribute forwardsAttr =
                envVarsAttr.getChildByName("forwarding");

        if (forwardsAttr.isValid()) {
            const auto sample = forwardsAttr.getNearestSample(0.0f);

            for (const auto envVar : sample) {
                const char* value = std::getenv(envVar);
                if (value != nullptr) {
                    evgb.set(envVar, kodachi::StringAttribute(value));
                }
            }

        }

        // TODO: Allow for envrionment variables to be set
    }

    gb.set("environmentVariables.forwarding", evgb.build());

    return gb.build();
}


inline bool
dirExists(const std::string& dir)
{
    struct stat buffer;
    return (stat(dir.c_str(), &buffer) == 0);
}

bool
createOutputDirs(const std::string& path, __mode_t mode = ACCESSPERMS)
{
    // Nothing to do if directory already exists
    if (dirExists(path)) {
        return true;
    }

    std::vector<std::string> dirs;
    FnKat::Util::Path::GetLocationStack(dirs, path);
    for (const auto& dir : dirs) {
        // Create directory if current 'dir' doesn't exist
        if (!dirExists(dir)) {
            if (mkdir(dir.c_str(), mode) != 0) {
                // ERROR
                return false;
            }
        }
    }

    return true;
}

void
createExrMergeOutputDirectory(const kodachi::GroupAttribute& renderSettingsAttr)
{
    const FnAttribute::GroupAttribute outputsGroupAttr =
            renderSettingsAttr.getChildByName("outputs");
    if (!outputsGroupAttr.isValid()) {
        return;
    }

    for (const auto& output : outputsGroupAttr) {
        const FnAttribute::GroupAttribute outputAttr = output.attribute;
        if (!outputAttr.isValid()) {
            continue;
        }

        const FnAttribute::StringAttribute outputTypeAttr =
                outputAttr.getChildByName("type");

        // Only interested in outputs of type "merge"
        if (outputTypeAttr != "merge") {
            continue;
        }

        const FnAttribute::StringAttribute outputLocationAttr =
                outputAttr.getChildByName("locationSettings.renderLocation");
        if (!outputLocationAttr.isValid()) {
            continue;
        }

        std::string outputLocation = outputLocationAttr.getValue();

        // Remove the file name from path
        std::string::size_type slashLoc = outputLocation.rfind('/');
        if (slashLoc == std::string::npos) {
            // No '/' found, this string can't be a valid path.
            continue;
        }
        outputLocation = outputLocation.substr(0, slashLoc);

        // Skip unresolved paths (e.g., paths with ani variables)
        if (outputLocation.find("{") == std::string::npos) {
            // Create the complete path to the output location
            // (recursive mkdir)
            createOutputDirs(outputLocation);
        }
        else {
            KdLogError(
                    "Failed to create render output location [" <<
                    outputLocation << "]; path may contain an unresolved ANI variable.");
        }
    }
}

template<typename T>
std::string exrHeaderValueToString(const T& attr)
{
    // convert vectors to space-delimited string
    const auto valueVec = attr.getNearestSample(0.0f);
    std::stringstream ss;
    for (size_t i = 0; i < valueVec.size(); ++i) {
        if (i > 0) {
            ss << " ";
        }
        ss << valueVec.at(i);
    }
    return ss.str();
}


kodachi::GroupAttribute
buildExrHeaderAttributes(const kodachi::GroupAttribute exrHeaderAttr)
{
    kodachi::GroupAttribute headerAttrs;

    if (exrHeaderAttr.isValid()) {
        // Metadata takes in 3 separate string vectors of names, types, and
        // values. Parse exr_header_attributes and identify what its type
        // is, and then convert it to a string value.
        std::vector<std::string> names;
        std::vector<std::string> types;
        std::vector<std::string> values;

        for (const auto attrIter : exrHeaderAttr) {
            names.emplace_back(attrIter.name);

            switch (attrIter.attribute.getType()) {
            case kFnKatAttributeTypeInt: {
                static const std::map<int, std::string> kTypeMap = {
                        {1, "int"},
                        {2, "v2i"},
                        {3, "v3i"}
                };
                const FnAttribute::IntAttribute attr = attrIter.attribute;
                values.push_back(exrHeaderValueToString(attr));

                auto iter = kTypeMap.find(attr.getNumberOfValues());
                if (iter != kTypeMap.end()) {
                    types.push_back(iter->second);
                } else {
                    // If someone created bad data, just pass it through
                    // anyway as a raw string
                    types.push_back("string");
                }
                break;
            }
            case kFnKatAttributeTypeFloat: {
                static const std::map<int, std::string> kTypeMap = {
                        {1, "float"},
                        {2, "v2f"},
                        {3, "v3f"},
                        {9, "m33f"},
                        {16, "m44f"}
                };
                const FnAttribute::FloatAttribute attr = attrIter.attribute;
                values.push_back(exrHeaderValueToString(attr));

                auto iter = kTypeMap.find(attr.getNumberOfValues());
                if (iter != kTypeMap.end()) {
                    types.push_back(iter->second);
                } else {
                    types.push_back("string");
                }
                break;
            }
            case kFnKatAttributeTypeDouble: {
                static const std::map<int, std::string> kTypeMap = {
                        {1, "double"}
                };
                const FnAttribute::DoubleAttribute attr = attrIter.attribute;
                values.push_back(exrHeaderValueToString(attr));

                auto iter = kTypeMap.find(attr.getNumberOfValues());
                if (iter != kTypeMap.end()) {
                    types.push_back(iter->second);
                } else {
                    types.push_back("string");
                }
                break;
            }
            case kFnKatAttributeTypeString:
            default: {
                const FnAttribute::StringAttribute attr = attrIter.attribute;
                types.push_back("string");
                values.push_back(attr.getValue());
                break;
            }
            }
        }

        if (!names.empty() && names.size() == types.size() && names.size() == values.size()) {
            static const kodachi::StringAttribute kMetadataAttr("Metadata");

            // ExrHeaderMergeOp should have already merged outputs rendering to the
            // same exr, so we can assume that this hash is unique per output file,
            // and that all outputs using the same file have the same hash.
            const std::string metaName = exrHeaderAttr.getHash().str() + "_Metadata";

            kodachi::GroupAttribute attrsAttr(
                    "name", kodachi::ZeroCopyStringAttribute::create(std::move(names)),
                    "type", kodachi::ZeroCopyStringAttribute::create(std::move(types)),
                    "value", kodachi::ZeroCopyStringAttribute::create(std::move(values)),
                    false);

            headerAttrs = kodachi::GroupAttribute(
                    "sceneClass", kMetadataAttr,
                    "name", kodachi::StringAttribute(metaName),
                    "attrs", attrsAttr,
                    "disableAliasing", kodachi::IntAttribute(true),
                    false);
        }
    }

    return headerAttrs;
}

void
addRenderOutput(kodachi::StaticSceneCreateOpArgsBuilder& sscbRenderOutput,
                const std::string& locationPath,
                const kodachi::GroupAttribute& sceneObjectAttrs)
{
    static const kodachi::StringAttribute kRenderOutputAttr("RenderOutput");
    static const kodachi::StringAttribute kRdl2Attr("rdl2");

    const kodachi::GroupAttribute sceneObjectAttr(
            "sceneClass", kRenderOutputAttr,
            "name", kodachi::StringAttribute(locationPath),
            "attrs", sceneObjectAttrs,
            "disableAliasing", kodachi::IntAttribute(true),
            false);

    sscbRenderOutput.setAttrAtLocation(locationPath, "type", kRdl2Attr);
    sscbRenderOutput.setAttrAtLocation(locationPath, "rdl2.sceneObject", sceneObjectAttr);
}

} // anonymous namespace

namespace mfk {

mfk::KatanaRenderMethod
parseMethod(const std::string &mthdString)
{
    if (mthdString == FnKat::RendererInfo::PreviewRenderMethod::kDefaultName)
        return mfk::KatanaRenderMethod::PREVIEW;
    if (mthdString == FnKat::RendererInfo::LiveRenderMethod::kDefaultName)
        return mfk::KatanaRenderMethod::LIVE;
    if (mthdString == FnKat::RendererInfo::DiskRenderMethod::kDefaultName)
        return mfk::KatanaRenderMethod::DISK;

    throw std::runtime_error("Unsupported renderMethod type: " + mthdString);
}

const char*
toString(KatanaRenderMethod krm)
{
    switch (krm) {
    case KatanaRenderMethod::PREVIEW:
        return "PREVIEW";
    case KatanaRenderMethod::LIVE:
        return "LIVE";
    case KatanaRenderMethod::DISK:
        return "DISK";
    default:
        return "UNKNOWN";
    }
}

KodachiRenderMethod::KodachiRenderMethod(MoonrayRender* source,
                                         KatanaRenderMethod renderMethod,
                                         const kodachi::GroupAttribute& opTreeMessage,
                                         const kodachi::StringAttribute& debugFile)
    :   mSourceBase(source)
    ,   mKatanaRenderMethod(renderMethod)
    ,   mNumTbbThreads(calculateNumTbbThreads())
{
    assert(mSourceBase);

    mGlobalSettings = source->getRootIterator().getAttribute("moonrayGlobalStatements");

    mSnapshotInterval = getDefaultSnapshotInterval(mKatanaRenderMethod, mGlobalSettings);

    const kodachi::GroupAttribute renderSettingsAttr =
            source->getRootIterator().getAttribute("renderSettings");
    mRenderSettings.initialize(renderSettingsAttr);

    // For disk render initialization, we need to handle the case where there
    // is no optree, and only the root iterator was passed in.
    if (opTreeMessage.isValid()) {
        const kodachi::StringAttribute activeContextAttr =
                opTreeMessage.getChildByName("activeContext");
        if (!activeContextAttr.isValid()) {
            throw std::runtime_error("optree message missing 'activeContext'");
        }

        mActiveContextId = activeContextAttr.getValue();

        const kodachi::GroupAttribute contextAttr =
                opTreeMessage.getChildByName("contexts." + mActiveContextId);
        if (!contextAttr.isValid()) {
            throw std::runtime_error("optree message does not contain entry for :" + mActiveContextId);
        }

        KdLogDebug("Using active context " << mActiveContextId);

        mSkipRender = kodachi::IntAttribute(
                mGlobalSettings.getChildByName("skip render")).getValue(false, false);

        if (debugFile.isValid()) {
            mDebugOutputFile = debugFile;
            mSkipRender = true;
        }

        mRezContextFile = opTreeMessage.getChildByName("rezContextFile");

        // This will be true if Katana is in UI mode and '3D > Render ID Pass'
        // Is checked in the monitor.
        if (mSourceBase->useRenderPassID()) {
            mIdSender.reset(FnKat::Render::IdSenderFactory::getNewInstance(
                    mSourceBase->getKatanaHost(), mRenderSettings.getFrameId()));

            mRenderSettings.initializeIdPassChannel();
        }

        initializeRenderBackend(opTreeMessage);
    }

    //-----------------------------------------------------
    // If this is a disk render and outputs of type "merge" defined, create the entire
    // output path on disk before starting the render 
    if (isDiskRender()) {
        createExrMergeOutputDirectory(renderSettingsAttr);
    }
}

KodachiRenderMethod::~KodachiRenderMethod()
{
}

int
KodachiRenderMethod::start()
{
    if (!mRenderBackend.isValid()) {
        KdLogFatal("RenderBackend was not initialized");
        return -1;
    }

    // For disk renders, the katana monitor loads the final rendered image from
    // file, it doesn't use the KatanaPipe
    if (!mSkipRender && !isDiskRender()) {
        Foundry::Katana::KatanaPipe* pipe = mSourceBase->getImagePipe();
        if (!pipe) {
            KdLogError("Could not open KatanaPipe");
            return -1;
        }

        mCurrentFrame.reset(new mfk::MonitorFrame(pipe,
                                                  mSourceBase->getRenderTime(),
                                                  mRenderSettings.getFrameId(),
                                                  "Moonray"));
    }

    mRenderBackend.start();

    int result = 0;
    if (!mSkipRender) {
        result = onRenderStarted();
    }

    FnKat::RenderOutputUtils::flushProceduralDsoCaches();
    FnKat::RenderOutputUtils::emptyFlattenedMaterialCache();

    mRenderBackend.stop();

    return result;
}

void
KodachiRenderMethod::queueDataUpdates(const Foundry::Katana::GroupAttribute& updateAttribute)
{
    static const kodachi::StringAttribute kOptreeDeltaMessageType("OPTREE_DELTA");

    const kodachi::StringAttribute typeAttr = updateAttribute.getChildByName("type");
    if (typeAttr != kOptreeDeltaMessageType) {
        KdLogInfo("Skipping data update of type" << typeAttr.getValueCStr("", false))
        return;
    }

    {
    std::lock_guard<std::mutex> lock(mDataUpdateMutex);
    mDataUpdates.emplace_back(updateAttribute);
    }

    mDataUpdateCondition.notify_one();
}

void
KodachiRenderMethod::applyPendingDataUpdates()
{
    static const kodachi::StringAttribute kOpTreeDeltasAttr("opTreeDeltas");

    std::unique_lock<std::mutex> lock(mDataUpdateMutex);
    if (mDataUpdates.empty()) {
        return;
    }

    const std::vector<kodachi::GroupAttribute> updates(std::move(mDataUpdates));
    lock.unlock();

    kodachi::GroupBuilder deltaGb;

    for (auto& update : updates) {
        const kodachi::GroupAttribute optreesAttr = update.getChildByName("optrees");
        if (optreesAttr.isValid()) {
            if (mIsMultiContext) {
                for (const auto optreePair : optreesAttr) {
                    deltaGb.setWithUniqueName(
                            kodachi::concat(optreePair.name, ".d"), optreePair.attribute);
                }
            } else {
                const kodachi::GroupAttribute optreeAttr =
                        optreesAttr.getChildByName(mActiveContextId);
                if (optreeAttr.isValid()) {
                    deltaGb.setWithUniqueName("d", optreeAttr);
                }
            }
        }
    }

    const kodachi::GroupAttribute deltaAttr = deltaGb.build();
    if (deltaAttr.getNumberOfChildren() > 0) {
        kodachi::GroupAttribute deltaGroup("type", kOpTreeDeltasAttr,
                                           "deltas", deltaAttr, false);
        mRenderBackend.setData(deltaGroup);
    }

    sendIdRegistrations();
}

bool
KodachiRenderMethod::isPreviewRender() const
{
    return mKatanaRenderMethod == KatanaRenderMethod::PREVIEW;
}

bool
KodachiRenderMethod::isLiveRender() const
{
    return mKatanaRenderMethod == KatanaRenderMethod::LIVE;
}

bool
KodachiRenderMethod::isDiskRender() const
{
    return mKatanaRenderMethod == KatanaRenderMethod::DISK;
}

int
KodachiRenderMethod::calculateNumTbbThreads()
{
    // Katana uses a value of 0 to mean maximum number of threads,
    // and a negative value to mean (max_threads - value).
    int renderThreads = 0;

    // Check if threads were set in Preferences
    // If not then check if attribute was added to render settings
    if (!mSourceBase->applyRenderThreadsOverride(renderThreads)) {
        mRenderSettings.applyRenderThreads(renderThreads);
    }

    if (renderThreads < 0) {
        renderThreads += tbb::task_scheduler_init::default_num_threads();

        if (renderThreads <= 0) {
            KdLogWarn("Invalid negative value for render threads."
                            << " Lowest value can be: "
                            << -tbb::task_scheduler_init::default_num_threads() + 1
                            << ". Using 1 thread.");
            renderThreads = 1;
        }
    }

    // Let TBB decide
    if (renderThreads == 0) {
        renderThreads = tbb::task_scheduler_init::automatic;
    }

    return renderThreads;
}

void
KodachiRenderMethod::initializeRenderBackend(const kodachi::GroupAttribute& opTreeMessage)
{
    const auto runtime = kodachi::KodachiRuntime::createRuntime();

    const kodachi::GroupAttribute contextsAttr =
            opTreeMessage.getChildByName("contexts");

    const int32_t numContexts = contextsAttr.getNumberOfChildren();

    kodachi::GroupAttribute renderBackendOpTreeAttr;
    if (numContexts > 1) {
        KdLogDebug("Creating backends for " << numContexts << " contexts");

        mIsMultiContext = true;

        kodachi::GroupBuilder contextsGb;

        std::array<int, 2> dim;
        mRenderSettings.getDisplayWindowSize(dim.data());
        KdLogDebug("Width: " << dim[0] << ", height: " << dim[1]);

        const kodachi::GroupAttribute layoutAttr =
                mSourceBase->getRootIterator().getAttribute("moonrayArrasSettings.layout");

        const kodachi::IntAttribute rowsAttr = layoutAttr.getChildByName("rows");
        const kodachi::IntAttribute paddingAttr = layoutAttr.getChildByName("padding");

        const int32_t rows = std::min(numContexts, rowsAttr.getValue(2, false));
        const int32_t cols = std::ceil(static_cast<float>(numContexts) / static_cast<float>(rows));
        const int32_t padding = paddingAttr.getValue(1, false);

        std::array<int32_t, 4> regionViewport;
        regionViewport[0] = 0;
        regionViewport[1] = 0;
        regionViewport[2] = (dim[0] * cols) + ((cols - 1) * padding);
        regionViewport[3] = (dim[1] * rows) + ((rows - 1) * padding);

        for (const auto contextPair : contextsAttr) {
            const kodachi::GroupAttribute contextInfoAttr = contextPair.attribute;

            const kodachi::GroupAttribute opTreeAttr = contextInfoAttr.getChildByName("optree");
            const kodachi::IntAttribute indexAttr = contextInfoAttr.getChildByName("index");

            const auto divt = std::div(indexAttr.getValue(), cols);
            const int row = rows - 1 - divt.quot;
            const int col = divt.rem;

            std::array<int32_t, 2> offset { dim[0] * col + padding * col, dim[1] * row + padding * row };

            const auto client = kodachi::optree_util::loadOpTree(runtime, opTreeAttr);
            const kodachi::GroupAttribute rootAttrs =
                            client->cookLocation("/root", false).getAttrs();

            {
                int32_t xRes = 512, yRes = 512;

                const kodachi::IntAttribute xyResAttr =
                        rootAttrs.getChildByName("renderSettings.xyRes");
                if (xyResAttr.isValid()) {
                    const auto xyRes = xyResAttr.getNearestSample(0.f);
                    xRes = xyRes[0];
                    yRes = xyRes[1];
                }

                if (xRes != dim[0] || yRes != dim[1]) {
                    std::stringstream ss;
                    ss << "Context '" << contextPair.name <<
                          "' resolution does not match. Expected: (" << dim[0] << ", " <<
                          dim[1] << "), Actual: (" << xRes << ", " << yRes << ")";

                    throw std::runtime_error(ss.str());
                }
            }

            const kodachi::GroupAttribute contextOpTreeAttr =
                    buildRenderBackendOpTree(rootAttrs, opTreeAttr);

            const kodachi::GroupAttribute contextAttr("optree", contextOpTreeAttr,
                                                      "offset", kodachi::IntAttribute(offset.data(), offset.size(), 1),
                                                      false);

            contextsGb.set(contextPair.name, contextAttr);
        }

        kodachi::OpTreeBuilder multiContextOtb;
        auto op = multiContextOtb.createOp();

        kodachi::AttributeSetOpArgsBuilder rootAttributeSetBuilder;
        rootAttributeSetBuilder.setLocationPaths(kodachi::StringAttribute("/root"));
        rootAttributeSetBuilder.setAttr("kodachi.backendSettings.backend", kodachi::StringAttribute("MultiContextRenderBackend"));
        rootAttributeSetBuilder.setAttr("kodachi.backendSettings.contexts", contextsGb.build());
        rootAttributeSetBuilder.setAttr("kodachi.backendSettings.regionViewport", kodachi::IntAttribute(regionViewport.data(), regionViewport.size(), 1));

        multiContextOtb.setOpArgs(op, "AttributeSet", rootAttributeSetBuilder.build());
        renderBackendOpTreeAttr = multiContextOtb.build(op);
    } else {
        const kodachi::GroupAttribute contextAttr = contextsAttr.getChildByIndex(0);
        const kodachi::GroupAttribute opTreeAttr = contextAttr.getChildByName("optree");
        const auto client = kodachi::optree_util::loadOpTree(runtime, opTreeAttr);

        const kodachi::GroupAttribute rootAttrs =
                client->cookLocation("/root", false).getAttrs();

        renderBackendOpTreeAttr = buildRenderBackendOpTree(rootAttrs, opTreeAttr);
    }

    if (!mRenderBackend.initialize(renderBackendOpTreeAttr)) {
        throw std::runtime_error("Error initializing backend");
    }
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
    }

    return suffixedFilePath;
}

kodachi::StringAttribute
KodachiRenderMethod::resolveFileSequence(const kodachi::StringAttribute& fileSequence)
{
    const float frame = mSourceBase->getRenderTime();
    std::string filePath = fileSequence.getValue();
    filePath = FnKat::DefaultFileSequencePlugin::resolveFileSequence(filePath,
                                                          static_cast<int>(frame),
                                                          false);
    return kodachi::StringAttribute(filePath);
}


kodachi::GroupAttribute
KodachiRenderMethod::buildRenderBackendOpTree(const kodachi::GroupAttribute& rootAttrs,
                                              const kodachi::GroupAttribute& opTreeAttr)
{
    const std::string kAttributeSet("AttributeSet");
    const std::string kStaticSceneCreate("StaticSceneCreate");

    const std::string kSceneFileOutput("moonrayGlobalStatements.scene file output");
    const std::string kSkipRender("moonrayGlobalStatements.skip render");

    const std::string kRender("render");

    static const kodachi::StringAttribute kRdl2Attr("rdl2");

    kodachi::OpTreeBuilder opTreeBuilder;
    auto initialOps = opTreeBuilder.merge(opTreeAttr);
    kodachi::OpTreeBuilder::Op::Ptr op = initialOps.back();

    const kodachi::IntAttribute useArrasAttr =
            rootAttrs.getChildByName("moonrayArrasSettings.use arras");

    const bool useArras = useArrasAttr.getValue(false, false);

    kodachi::AttributeSetOpArgsBuilder rootAttributeSetBuilder;
    {
        rootAttributeSetBuilder.setLocationPaths(kodachi::StringAttribute("/root"));

        // override the scene file output if renderboot provided us with a debug file.
        if (mDebugOutputFile.isValid()) {
            rootAttributeSetBuilder.setAttr(
                    kSceneFileOutput, mDebugOutputFile);
            rootAttributeSetBuilder.setAttr(
                    kSkipRender, kodachi::IntAttribute(true));
        }

        kodachi::GroupBuilder backendSettingsGb;
        {
            const bool idPassEnabled = mIdSender != nullptr;

            if (useArras) {
                if (isDiskRender()) {
                    throw std::runtime_error("Cannot use Arras for disk renders");
                }

                kodachi::GroupBuilder arrasSettingsBuilder;

                const kodachi::GroupAttribute moonrayArrasSettingsAttr =
                        rootAttrs.getChildByName("moonrayArrasSettings");

                backendSettingsGb.set("arrasSettings",
                        buildArrasSettings(moonrayArrasSettingsAttr,
                                           mRezContextFile,
                                           idPassEnabled));

                backendSettingsGb.set("backend", kodachi::StringAttribute("ArrasRenderBackend"));
            } else {
                backendSettingsGb.set("backend", kodachi::StringAttribute("MoonrayRenderBackend"));
            }

            if (idPassEnabled) {
                const auto idPassChannel = mRenderSettings.getIdPassChannel();
                assert(idPassChannel);

                backendSettingsGb.set("idPass.enabled", kodachi::IntAttribute(true));
                backendSettingsGb.set("idPass.idAttrName", kodachi::StringAttribute("katanaID"));
                backendSettingsGb.set("idPass.bufferName", kodachi::StringAttribute(idPassChannel->getLocationPath()));
            }

            backendSettingsGb.set("isLiveRender",
                    kodachi::IntAttribute(isLiveRender()));

            // Moonray uses 0 for automatic number of threads, while TBB uses -1
            backendSettingsGb.set("numThreads",
                    kodachi::IntAttribute(std::max(mNumTbbThreads, 0)));

            if (isDiskRender()) {
                backendSettingsGb.set("writeToDisk", kodachi::IntAttribute(true));
            }


            backendSettingsGb.set("appendImplicitResolvers", kodachi::IntAttribute(false));
            backendSettingsGb.set("appendTerminalOps", kodachi::IntAttribute(false));

            rootAttributeSetBuilder.setAttr(
                    "kodachi.backendSettings", backendSettingsGb.build());
        }
    }

    // add RenderOutputs as 'renderer procedural' locations
    // they will be automatically added to the SceneContext during traversal
    kodachi::StaticSceneCreateOpArgsBuilder sscbRenderOutput(true);
    {
        const auto& chanInfos = isDiskRender() ?
                mRenderSettings.getEnabledChannels() : mRenderSettings.getInteractiveChannels();
        const kodachi::GroupAttribute outputChannels =
                                   mGlobalSettings.getChildByName("outputChannels");

        for (auto chanInfo : chanInfos) {
            kodachi::GroupBuilder sceneObjectAttrsGb;

            bool isBeauty = false;
            bool isCryptomatte = false;

            switch(chanInfo->getChannelType()) {
            case MChannelInfo::ChannelType::AOV:
            {
                // Get the attrs for the RenderOutput's output channel
                const kodachi::GroupAttribute argsAttr =
                              outputChannels.getChildByName(chanInfo->getMoonrayChannelName());
                if (!argsAttr.isValid()) {
                    KdLogWarn("Could not get outputChannel attrs for " << chanInfo->getMoonrayChannelName());
                } else {
                    kodachi::StringAttribute resultAttr = argsAttr.getChildByName("result");
                    if (resultAttr == "cryptomatte") {
                        isCryptomatte = true;
                    }

                    sceneObjectAttrsGb.deepUpdate(argsAttr);
                    sceneObjectAttrsGb.del("name");

                    // disabling until we have multiple types of cryptomatte
                    sceneObjectAttrsGb.del("cryptomatte_layer");
                }
                break;
            }
            case MChannelInfo::ChannelType::BEAUTY:
            {
                isBeauty = true;
                if (useArras) {
                    rootAttributeSetBuilder.setAttr("kodachi.backendSettings.arrasSettings.beautyBufferName",
                                            kodachi::StringAttribute(chanInfo->getLocationPath()));
                }
                break;
            }
            case MChannelInfo::ChannelType::ID:
            {
                // Katana IDs are unsigned 64-bit ints. We represent them in
                // Moonray as 2 floats. Katana requires us to send the IDs in a
                // 3-float buffer with the first float being 0 so it knows that
                // we are using the new ID system and not the deprecated one.
                // Use 'closest' math filter so that Moonray doesn't attempt
                // to average or modify the data when computing the render output.
                sceneObjectAttrsGb
                    .set("result", kodachi::StringAttribute("primitive attribute"))
                    .set("primitive_attribute", kodachi::StringAttribute("katanaID"))
                    .set("primitive_attribute_type", kodachi::StringAttribute("VEC3F"))
                    .set("math_filter", kodachi::StringAttribute("closest"));
                break;
            }
            }

            bool fileNameSet = false;

            if (isDiskRender()) {
                auto renderOutput = chanInfo->getRenderOutput();
                if (!renderOutput) {
                    KdLogWarn("RenderOutput not set for channel: " << chanInfo->getRenderOutputName());
                } else {

                    auto outputPath =
                            FnKat::RenderOutputUtils::buildTempRenderLocation(
                                            mSourceBase->getRootIterator(),
                                            chanInfo->getRenderOutputName(),
                                            kRender,
                                            renderOutput->fileExtension,
                                            mSourceBase->getRenderTime());

                    sceneObjectAttrsGb.set("file_name", kodachi::StringAttribute(outputPath));
                    fileNameSet = true;

                    // Copy relevant renderOutput.renderSettings attributes
                    const auto& outputRenderSettings = renderOutput->rendererSettings;
                    {
                        const auto outputTypeIter =
                                outputRenderSettings.find("output_type");
                        if (outputTypeIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set("output_type", outputTypeIter->second);
                        }
                    }

                    {
                        const auto parityIter =
                                outputRenderSettings.find("parity");
                        if (parityIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set(
                                    "parity", parityIter->second);
                        }
                    }

                    {
                        const auto compressionIter =
                                outputRenderSettings.find("compression");
                        if (compressionIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set(
                                    "compression", compressionIter->second);
                        }
                    }

                    {
                        const auto exrCompressionIter =
                                outputRenderSettings.find("exr_dwa_compression_level");
                        if (exrCompressionIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set(
                                    "exr_dwa_compression_level", exrCompressionIter->second);
                        }
                    }

                    // Checkpoint rendering
                    {
                        const auto checkpointFileNameIter =
                                outputRenderSettings.find("checkpoint_file_name");
                        if (checkpointFileNameIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set(
                                    "checkpoint_file_name", resolveFileSequence(kodachi::StringAttribute(checkpointFileNameIter->second)));
                        }
                    }

                    // Resume rendering
                    {
                        const auto resumeFileNameIter =
                                outputRenderSettings.find("resume_file_name");
                        if (resumeFileNameIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set(
                                    "resume_file_name", resolveFileSequence(kodachi::StringAttribute(resumeFileNameIter->second)));
                        }
                    }

                    // Checkpoint rendering
                    {
                        const auto filePartIter =
                                outputRenderSettings.find("file_part");
                        if (filePartIter != outputRenderSettings.end()) {
                            sceneObjectAttrsGb.set(
                                    "file_part", filePartIter->second);
                        }
                    }

                    {
                        const auto exrHeaderIter =
                                outputRenderSettings.find("exr_header_attributes");

                        if (exrHeaderIter != outputRenderSettings.end()) {
                            const auto exrHeaderAttr =
                                    buildExrHeaderAttributes(exrHeaderIter->second);
                            if (exrHeaderAttr.isValid()) {
                                const std::string metadataPath =
                                        chanInfo->getLocationPath() + "/__Metadata";
                                sscbRenderOutput.setAttrAtLocation(
                                        metadataPath, "type", kRdl2Attr);
                                sscbRenderOutput.setAttrAtLocation(
                                        metadataPath, "rdl2.sceneObject", exrHeaderAttr);

                                sceneObjectAttrsGb.set("exr_header_attributes",
                                        exrHeaderAttr.getChildByName("name"));
                            }
                        }
                    }

                    // Create a "weight" and "beauty aux" aov if resumable_output
                    // is true
                    {
                        const kodachi::IntAttribute resumableOutputAttr =
                                mGlobalSettings.getChildByName("resumable_output");
                        const bool resumableOutput =
                                resumableOutputAttr.getValue(false, false);

                        const kodachi::StringAttribute checkpointFileAttr =
                                mGlobalSettings.getChildByName("checkpoint_file");

                        // beauty is the one AOV that's guaranteed to be created,
                        // so use its creation as the opportunity to also create
                        // the necessary resume outputs
                        if (resumableOutput && isBeauty) {
                            kodachi::GroupBuilder weightSceneObjectAttrsGb;
                            kodachi::GroupBuilder beautyAuxSceneObjectAttrsGb;
                            const kodachi::GroupAttribute sceneObjectGroupAttr =
                                    sceneObjectAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain);
                            weightSceneObjectAttrsGb.update(sceneObjectGroupAttr);
                            beautyAuxSceneObjectAttrsGb.update(sceneObjectGroupAttr);

                            if (checkpointFileAttr.isValid()) {
                                weightSceneObjectAttrsGb.set("checkpoint_file_name",
                                        checkpointFileAttr);
                                beautyAuxSceneObjectAttrsGb.set("checkpoint_file_name",
                                        checkpointFileAttr);
                            }

                            weightSceneObjectAttrsGb.set("result", kodachi::IntAttribute(11));
                            weightSceneObjectAttrsGb.set("channel_name", kodachi::StringAttribute("weight"));

                            beautyAuxSceneObjectAttrsGb.set("result", kodachi::IntAttribute(12));
                            beautyAuxSceneObjectAttrsGb.set("channel_name", kodachi::StringAttribute("beauty aux"));

                            // Create new paths for these outputs, but add
                            // "resume" to the path in case the names might
                            // conflict with another terribly named output.
                            std::string weightLocationPath =
                                    "/root/__scenebuild/renderoutput/resume/weight";
                            std::string beautyAuxLocationPath  =
                                    "/root/__scenebuild/renderoutput/resume/beauty_aux";

                            addRenderOutput(sscbRenderOutput, weightLocationPath,
                                    weightSceneObjectAttrsGb.build());
                            addRenderOutput(sscbRenderOutput, beautyAuxLocationPath,
                                    beautyAuxSceneObjectAttrsGb.build());
                        }
                    }


                    // generate_denoiser_outputs (default value is "off")
                    {
                        const auto denoiseIter =
                                outputRenderSettings.find("generate_denoiser_outputs");
                        if (denoiseIter != outputRenderSettings.end() &&
                                kodachi::StringAttribute(denoiseIter->second) == "on") {
                            const std::string& origLocationPath = chanInfo->getLocationPath();


                            std::string channelName = "primary";
                            {
                                const kodachi::StringAttribute channelNameAttr =
                                        sceneObjectAttrsGb
                                            .build(kodachi::GroupBuilder::BuildAndRetain)
                                            .getChildByName("channel_name");
                                if (channelNameAttr.isValid()) {
                                    channelName = channelNameAttr.getValue();
                                }
                            }

                            // Keep the even and odd buffers separate
                            kodachi::GroupBuilder evenSceneObjectAttrsGb;
                            kodachi::GroupBuilder oddSceneObjectAttrsGb;
                            const kodachi::GroupAttribute sceneObjectGroupAttr =
                                    sceneObjectAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain);
                            evenSceneObjectAttrsGb.update(sceneObjectGroupAttr);
                            oddSceneObjectAttrsGb.update(sceneObjectGroupAttr);

                            // Create a different render file for the even data
                            auto evenOutputPath =
                                    insertDenoiserSuffixToFilePath(outputPath,
                                                                   ".1");
                            evenSceneObjectAttrsGb.set("file_name", kodachi::StringAttribute(evenOutputPath));

                            // Create a different render file for the odd data
                            auto oddOutputPath =
                                    insertDenoiserSuffixToFilePath(outputPath,
                                                                   ".0");
                            oddSceneObjectAttrsGb.set("file_name", kodachi::StringAttribute(oddOutputPath));

                            // even parity
                            const std::string evenLocationPath = origLocationPath + "_even";
                            evenSceneObjectAttrsGb.set("channel_name", kodachi::StringAttribute(channelName));
                            evenSceneObjectAttrsGb.set("channel_format", kodachi::StringAttribute("half"));
                            evenSceneObjectAttrsGb.set("parity", kodachi::StringAttribute("even"));
                            addRenderOutput(sscbRenderOutput, evenLocationPath,
                                    evenSceneObjectAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain));

                            // odd parity
                            const std::string oddLocationPath = origLocationPath + "_odd";
                            oddSceneObjectAttrsGb.set("channel_name", kodachi::StringAttribute(channelName));
                            oddSceneObjectAttrsGb.set("channel_format", kodachi::StringAttribute("half"));
                            oddSceneObjectAttrsGb.set("parity", kodachi::StringAttribute("odd"));
                            addRenderOutput(sscbRenderOutput, oddLocationPath,
                                    oddSceneObjectAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain));

                            // even variance
                            const std::string evenVarianceLocationPath = origLocationPath + "_even_variance";
                            evenSceneObjectAttrsGb.del("parity");
                            evenSceneObjectAttrsGb.set("channel_name", kodachi::StringAttribute(channelName + ".variance"));
                            evenSceneObjectAttrsGb.set("channel_format", kodachi::StringAttribute("float"));
                            evenSceneObjectAttrsGb.set("result", kodachi::StringAttribute("variance aov"));
                            evenSceneObjectAttrsGb.set("reference_render_output", kodachi::StringAttribute(evenLocationPath));
                            addRenderOutput(sscbRenderOutput, evenVarianceLocationPath,
                                    evenSceneObjectAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain));

                            // odd variance
                            const std::string oddVarianceLocationPath = origLocationPath + "_odd_variance";
                            oddSceneObjectAttrsGb.del("parity");
                            oddSceneObjectAttrsGb.set("channel_name", kodachi::StringAttribute(channelName + ".variance"));
                            oddSceneObjectAttrsGb.set("channel_format", kodachi::StringAttribute("float"));
                            oddSceneObjectAttrsGb.set("result", kodachi::StringAttribute("variance aov"));
                            oddSceneObjectAttrsGb.set("reference_render_output", kodachi::StringAttribute(oddLocationPath));
                            addRenderOutput(sscbRenderOutput, oddVarianceLocationPath,
                                    oddSceneObjectAttrsGb.build(kodachi::GroupBuilder::BuildAndRetain));
                        }
                    }

                    // Copy relevant cryptomatte info to the backendsettings
                    {
                        const auto cryptomatteManifestIter =
                                outputRenderSettings.find("cryptomatte_manifest");
                        if (cryptomatteManifestIter != outputRenderSettings.end() &&
                                kodachi::StringAttribute(cryptomatteManifestIter->second) != "") {
                            rootAttributeSetBuilder.setAttr("kodachi.backendSettings.cryptomatte.cryptomatte_manifest",
                                                            resolveFileSequence(kodachi::StringAttribute(cryptomatteManifestIter->second)));
                        }
                    }
                }
            }

            if (!fileNameSet) {
                sceneObjectAttrsGb.set("file_name", kodachi::StringAttribute("/tmp/scene.exr"));
            }

            if (isCryptomatte && !isDiskRender()) continue;
            addRenderOutput(sscbRenderOutput, chanInfo->getLocationPath(),
                            sceneObjectAttrsGb.build());
        }
    }

    // Add the ops to the optree
    {
        op = opTreeBuilder.appendOp(op, opTreeBuilder.createOp());
        opTreeBuilder.setOpArgs(op, kAttributeSet, rootAttributeSetBuilder.build());

        op = opTreeBuilder.appendOp(op, opTreeBuilder.createOp());

        opTreeBuilder.setOpArgs(op, kStaticSceneCreate, sscbRenderOutput.build());
    }

    // If the 'multi threaded' attribute has been set to false,
    // disable parallel traversal for the whole scene graph
    if (!kodachi::IntAttribute(mGlobalSettings.
            getChildByName("multi threaded")).getValue(true, false)) {
        kodachi::AttributeSetOpArgsBuilder parallelAttrSetBuilder;
        parallelAttrSetBuilder.setCEL(kodachi::StringAttribute("//*"));
        parallelAttrSetBuilder.setAttr(kodachi::Traversal::kParallelTraversal,
                                       kodachi::IntAttribute(false));

        op = opTreeBuilder.appendOp(op, opTreeBuilder.createOp());
        opTreeBuilder.setOpArgs(op, kAttributeSet, parallelAttrSetBuilder.build());
    }

    return opTreeBuilder.build(op);
}

int
KodachiRenderMethod::onRenderStarted()
{
    mSourceBase->resetProgress();

    switch (mKatanaRenderMethod) {
    case KatanaRenderMethod::DISK:
        return onDiskRenderStarted();
    case KatanaRenderMethod::PREVIEW:
        return onPreviewRenderStarted();
    case KatanaRenderMethod::LIVE:
        return onLiveRenderStarted();
    default:
        KdLogError("Unsupported KatanaRenderMethod: "
                   << toString(mKatanaRenderMethod));
    }

    return -1;
}

int
KodachiRenderMethod::onDiskRenderStarted()
{
    // just wait to finish
    bool frameComplete = false;

    while (!frameComplete) {
        frameComplete = isFrameComplete(mRenderBackend);

        // Poll for completion/image every 500 ms.
        mSourceBase->logProgress(getRenderProgress(mRenderBackend));
        if (!frameComplete) {
            std::this_thread::sleep_for(mSnapshotInterval);
        }
    }

    // backend will write out exr automatically

    return 0;
}

int
KodachiRenderMethod::onPreviewRenderStarted()
{
    while (!isFrameReadyForDisplay(mRenderBackend)) {
        std::this_thread::sleep_for(mSnapshotInterval);
    }

    bool frameComplete = false;

    while (!frameComplete) {
        sendIdRegistrations();

        // Determine when we next want to snapshot
        const auto nextSnapshotTime =
                std::chrono::system_clock::now() + mSnapshotInterval;

        frameComplete = onFrameReadyForSnapshot();

        if (!frameComplete) {
            // GCC 4.8.3 Bug
            // Have to check that sleep duration is not negative, otherwise
            // infinite sleep can occur
            // https://gcc.gnu.org/bugzilla/show_bug.cgi?id=58038
            const auto snapshotEnd = std::chrono::system_clock::now();
            if (nextSnapshotTime > snapshotEnd) {
                std::this_thread::sleep_for(nextSnapshotTime - snapshotEnd);
            }
        }
    }

    return 0;
}

int
KodachiRenderMethod::onLiveRenderStarted()
{
    bool continueLiveLoop = true;

    // Outter loop lets us stop sending frames once the frame is complete,
    // but then start up again if changes are made.
    while (continueLiveLoop) {

        // Image loop
        while (continueLiveLoop) {

            while (!isFrameReadyForDisplay(mRenderBackend)) {
                std::this_thread::sleep_for(mSnapshotInterval);
            }

            sendIdRegistrations();

            const auto nextSnapshotTime =
                    std::chrono::system_clock::now() + mSnapshotInterval;

            const bool frameComplete = onFrameReadyForSnapshot();

            // Apply updates until it is time to take the next snapshot
            std::cv_status cvStatus = std::cv_status::no_timeout;
            while (cvStatus == std::cv_status::no_timeout) {
                // always apply updates at least once
                applyPendingDataUpdates();

                std::unique_lock<std::mutex> lock(mDataUpdateMutex);
                if (std::chrono::system_clock::now() >= nextSnapshotTime) {
                    // time for the next snapshot
                    cvStatus = std::cv_status::timeout;
                } else if (!mDataUpdates.empty()) {
                    // more updates were received while applying the last set
                    continue;
                } else {
                    // wait until its time to take the next snapshot.
                    // If the dataUpdateCondition is notified, then apply updates again
                    cvStatus = mDataUpdateCondition.wait_until(lock,
                                                               nextSnapshotTime);
                }
            }

            // if the frame was complete before we send the data, and no updates
            // were applied, then stop the render loop
            if (frameComplete && isFrameComplete(mRenderBackend)) {
                break;
            }
        }

        // wait section
        {
            // block until we have something to bother with
            std::unique_lock<std::mutex> lock(mDataUpdateMutex);
            while (continueLiveLoop && mDataUpdates.empty() &&
                    (!isFrameRendering(mRenderBackend) ||
                           isFrameComplete(mRenderBackend))) {
                mDataUpdateCondition.wait(lock);
            }
        }
        applyPendingDataUpdates();
    }

    return 0;
}

bool
KodachiRenderMethod::onFrameReadyForSnapshot()
{
    static const kodachi::GroupAttribute kSnapshotQuery(
            "type", kodachi::StringAttribute("renderSnapshot"), false);

    static const kodachi::GroupAttribute kIdPassQuery(
            "type", kodachi::StringAttribute("idPass"), false);

    const kodachi::BackendClient::DataMessage snapshotData =
            mRenderBackend.getData(kSnapshotQuery);

    const kodachi::GroupAttribute snapshotAttr = snapshotData.getAttr();

    const kodachi::FloatAttribute progAttr = snapshotAttr.getChildByName("prog");

    // Get and log the render progress.
    mSourceBase->logProgress(progAttr.getValue());

    // send the frame ///////////
    mCurrentFrame->sendRenderSnapshot(mRenderSettings, snapshotAttr);

    if (mIdSender) {
        const kodachi::BackendClient::DataMessage idPassData =
                mRenderBackend.getData(kIdPassQuery);

        const kodachi::GroupAttribute idPassAttr = idPassData.getAttr();
        if (idPassAttr.isValid()) {
            mCurrentFrame->sendRenderSnapshot(mRenderSettings, idPassAttr);
        }
    }

    const kodachi::IntAttribute frameCompleteAttr =
            snapshotAttr.getChildByName("frameComplete");

    return frameCompleteAttr.getValue(false, false);
}

void
KodachiRenderMethod::sendIdRegistrations()
{
    if (mIdSender) {
        const kodachi::GroupAttribute idRegistrationAttr =
                getIDRegistrations(mRenderBackend);

        for (const auto idPairAttr : idRegistrationAttr) {
            const kodachi::IntAttribute idAttr = idPairAttr.attribute;

            union HashUnion {
                uint64_t u64;
                int32_t i32[2];
            } u;

            const auto sample = idAttr.getNearestSample(0.f);
            u.i32[0] = sample[0];
            u.i32[1] = sample[1];

            KdLogDebug("Sending ID: " << u.u64 << ", " << idPairAttr.name);
            mIdSender->send(u.u64, idPairAttr.name.data());
        }
    }
}

} /* namespace mfk */

