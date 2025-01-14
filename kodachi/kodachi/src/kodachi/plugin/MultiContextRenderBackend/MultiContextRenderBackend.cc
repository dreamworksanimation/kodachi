// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// self
#include "MultiContextRenderBackend.h"

#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/KodachiRuntime.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/OpTreeUtil.h>
#include <kodachi/plugin_system/KdPlugin.h>

#include <tbb/parallel_for.h>

namespace {
KdLogSetup("MultiContextRenderBackend");

kodachi::IntAttribute
offsetViewport(const kodachi::IntAttribute& viewportAttr, int32_t xOffset, int32_t yOffset)
{
    if (xOffset == 0 && yOffset == 0) {
        return viewportAttr;
    }

    const auto viewport = viewportAttr.getNearestSample(0.f);
    std::array<int32_t, 4> offsetViewport { viewport[0] + xOffset,
                                            viewport[1] + yOffset,
                                            viewport[2] + xOffset,
                                            viewport[3] + yOffset };

    return kodachi::IntAttribute(offsetViewport.data(), offsetViewport.size(), 1);
}
}

namespace kodachi {

MultiContextRenderBackend::MultiContextRenderBackend()
{}

bool
MultiContextRenderBackend::initialize(const kodachi::GroupAttribute& opTree)
{
    const auto runtime = kodachi::KodachiRuntime::createRuntime();

    const auto client = kodachi::optree_util::loadOpTree(runtime, opTree);

    if (!client) {
        KdLogError("Could not load optree");
        return false;
    }

    const kodachi::GroupAttribute rootAttr =
            client->cookLocation("/root", false).getAttrs();

    const kodachi::GroupAttribute backendSettingsAttr =
            rootAttr.getChildByName("kodachi.backendSettings");

    const kodachi::IntAttribute regionViewportAttr =
            backendSettingsAttr.getChildByName("regionViewport");

    if (!regionViewportAttr.isValid()) {
        KdLogError("Missing 'regionViewport' backend setting");
        return false;
    }

    mRegionViewport = regionViewportAttr;

    const kodachi::GroupAttribute contextsAttr =
            backendSettingsAttr.getChildByName("contexts");

    if (!contextsAttr.isValid()) {
        KdLogError("Missing 'contexts' backend setting");
        return false;
    }

    const int64_t numContexts = contextsAttr.getNumberOfChildren();

    if (numContexts == 1) {
        KdLogWarn("backendSettings only contains 1 context");
    }

    // This is the only time that we are modifying the contexts map
    // so the mutex doesn't need to be a member variable
    std::mutex contextsMutex;

    // initialize each context in parallel, since in the case of ArrasRenderBackend
    // it will block until the session is ready
    tbb::parallel_for(int64_t(0), numContexts, int64_t(1), [&](int64_t i)
    {
        std::string contextName = contextsAttr.getChildName(i);

        const kodachi::GroupAttribute contextAttr = contextsAttr.getChildByIndex(i);
        const kodachi::GroupAttribute contextOpTreeAttr =
                contextAttr.getChildByName("optree");

        if (!contextOpTreeAttr.isValid()) {
            KdLogWarn("Missing optree for context '" << contextName << "'");
            return;
        }

        const kodachi::IntAttribute offsetAttr =
                contextAttr.getChildByName("offset");

        if (!contextOpTreeAttr.isValid()) {
            KdLogWarn("Missing offset for context '" << contextName << "'");
            return;
        }

        BackendClient backendClient;
        if (backendClient.initialize(contextOpTreeAttr)) {
            Context context;
            context.mContextName = std::move(contextName);
            context.mRenderBackend = std::move(backendClient);

            const auto offset = offsetAttr.getNearestSample(0.f);
            context.mXOffset = offset[0];
            context.mYOffset = offset[1];

            std::lock_guard<std::mutex> guard(contextsMutex);
            mContexts[context.mContextName] = std::move(context);
        } else {
            KdLogError("Failed to initialize context " << contextName);
        }
    });

    return !mContexts.empty();
}

void
MultiContextRenderBackend::start()
{
    for (auto& context : mContexts) {
        context.second.mRenderBackend.start();
    }
}

void
MultiContextRenderBackend::stop()
{
    for (auto& context : mContexts) {
        context.second.mRenderBackend.stop();
    }
}

void
MultiContextRenderBackend::setData(const kodachi::GroupAttribute& dataAttr)
{
    static const kodachi::StringAttribute kOpTreeDeltasAttr("opTreeDeltas");

    const kodachi::StringAttribute typeAttr(dataAttr.getChildByName("type"));

    if (typeAttr == kOpTreeDeltasAttr) {
        const kodachi::GroupAttribute deltasAttr(dataAttr.getChildByName("deltas"));
        for (const auto deltaPair : deltasAttr) {
            const auto contextIter = mContexts.find(deltaPair.name);
            if (contextIter != mContexts.end()) {
                kodachi::GroupAttribute deltaGroup("type", kOpTreeDeltasAttr,
                                                   "deltas", deltaPair.attribute, false);
                contextIter->second.mRenderBackend.setData(deltaGroup);
            }
        }
    } else {
        KdLogDebug("setData - Unsupported data type: " << dataAttr.getXML());
    }
}

MultiContextRenderBackend::DataMessage
MultiContextRenderBackend::getData(const kodachi::GroupAttribute& queryAttr)
{
    enum class QueryType {
        RENDER_SNAPSHOT,
        ID_REGISTRATIONS,
        ID_PASS,
        IS_FRAME_RENDERING,
        IS_FRAME_READY_FOR_DISPLAY,
        IS_FRAME_COMPLETE
    };

    static const std::unordered_map<kodachi::StringAttribute, QueryType, kodachi::AttributeHash> kQueryTypes
    {
        { "renderSnapshot"        , QueryType::RENDER_SNAPSHOT    },
        { "idRegistrations"       , QueryType::ID_REGISTRATIONS   },
        { "idPass"                , QueryType::ID_PASS   },
        { "isFrameRendering"      , QueryType::IS_FRAME_RENDERING },
        { "isFrameReadyForDisplay", QueryType::IS_FRAME_READY_FOR_DISPLAY },
        { "isFrameComplete"       , QueryType::IS_FRAME_COMPLETE  },
    };

    const kodachi::StringAttribute typeAttr(queryAttr.getChildByName("type"));

    const auto iter = kQueryTypes.find(typeAttr);
    if (iter != kQueryTypes.end()) {
        switch (iter->second) {
        case QueryType::RENDER_SNAPSHOT:
            return snapshotBuffers();
        case QueryType::ID_REGISTRATIONS:
            return getIdRegistrations();
        case QueryType::ID_PASS:
            return getIdPass();
        case QueryType::IS_FRAME_RENDERING:
            return DataMessage(kodachi::IntAttribute(isFrameRendering()));
        case QueryType::IS_FRAME_READY_FOR_DISPLAY:
            return DataMessage(kodachi::IntAttribute(isFrameReadyForDisplay()));
        case QueryType::IS_FRAME_COMPLETE:
            return DataMessage(kodachi::IntAttribute(isFrameComplete()));
        }
    }

    KdLogDebug("Unsupported query type: " << typeAttr.getValueCStr());

    return {};
}

MultiContextRenderBackend*
MultiContextRenderBackend::create()
{
    return new MultiContextRenderBackend;
}

kodachi::KdPluginStatus
MultiContextRenderBackend::setHost(FnPluginHost* host)
{
    return BackendBase::setHost(host);
}

bool
MultiContextRenderBackend::isFrameReadyForDisplay() const
{
    static const kodachi::GroupAttribute kQueryAttr("type",
                kodachi::StringAttribute("isFrameReadyForDisplay"), false);

    bool readyForDisplay = false;

    for (auto& context : mContexts) {
        const auto data = context.second.mRenderBackend.getData(kQueryAttr);

        const kodachi::IntAttribute isFrameReadyForDisplay(data.getAttr());

        readyForDisplay |= static_cast<bool>(isFrameReadyForDisplay.getValue(false, false));
    }

    return readyForDisplay;
}

bool
MultiContextRenderBackend::isFrameRendering() const
{
    static const kodachi::GroupAttribute kQueryAttr("type",
                kodachi::StringAttribute("isFrameRendering"), false);

    bool frameRendering = false;

    for (auto& context : mContexts) {
        const auto data = context.second.mRenderBackend.getData(kQueryAttr);

        const kodachi::IntAttribute isFrameRenderingAttr(data.getAttr());

        frameRendering |= static_cast<bool>(isFrameRenderingAttr.getValue(false, false));
    }

    return frameRendering;
}

bool
MultiContextRenderBackend::isFrameComplete() const
{
    static const kodachi::GroupAttribute kQueryAttr("type",
                kodachi::StringAttribute("isFrameComplete"), false);

    bool frameComplete = true;

    for (auto& context : mContexts) {
        const auto data = context.second.mRenderBackend.getData(kQueryAttr);

        const kodachi::IntAttribute isFrameCompleteAttr(data.getAttr());

        frameComplete &= static_cast<bool>(isFrameCompleteAttr.getValue(false, false));
    }

    return frameComplete;
}

MultiContextRenderBackend::DataMessage
MultiContextRenderBackend::snapshotBuffers()
{
    static const kodachi::GroupAttribute kSnapshotQueryAttr(
            "type", kodachi::StringAttribute("renderSnapshot"), false);

    // Send back tiles in the format of
    // -avp
    // -rvp
    // -tiles
    // ---contextName
    // -----vp
    // -----bufs
    // --------enc
    // --------data

    bool frameComplete = true;
    float totalProgress = 0.f;

    kodachi::GroupBuilder tilesGb;
    for (auto& contextPair : mContexts) {
        auto& context = contextPair.second;

        const auto snapshotData =
                context.mRenderBackend.getData(kSnapshotQueryAttr);
        if (!snapshotData.isValid()) {
            frameComplete &= context.mFrameComplete;
            totalProgress += context.mLastProgress;
            continue;
        }

        const kodachi::GroupAttribute snapshotAttr = snapshotData.getAttr();

        const kodachi::IntAttribute svpAttr = snapshotAttr.getChildByName("svp");
        const kodachi::GroupAttribute bufsAttr = snapshotAttr.getChildByName("bufs");
        const kodachi::FloatAttribute progAttr = snapshotAttr.getChildByName("prog");
        const kodachi::IntAttribute frameCompleteAttr = snapshotAttr.getChildByName("frameComplete");
        const kodachi::IntAttribute flippedVAttr = snapshotAttr.getChildByName("flippedV");

        context.mFrameComplete = frameCompleteAttr.getValue(false, false);
        frameComplete &= context.mFrameComplete;

        context.mLastProgress = progAttr.getValue();
        totalProgress += context.mLastProgress;

        const kodachi::GroupAttribute tileAttr("vp", offsetViewport(svpAttr, context.mXOffset, context.mYOffset),
                                               "bufs", bufsAttr,
                                               "flippedV", flippedVAttr,
                                               false);

        tilesGb.set(contextPair.first, tileAttr);
    }

    kodachi::GroupBuilder snapshotGb;
    snapshotGb
        .set("avp", mRegionViewport)
        .set("rvp", mRegionViewport)
        .set("tiles", tilesGb.build())
        .set("prog", kodachi::FloatAttribute(totalProgress / mContexts.size()));

    if (frameComplete) {
        snapshotGb.set("frameComplete", kodachi::IntAttribute(true));
    }

    return DataMessage(snapshotGb.build());
}

MultiContextRenderBackend::DataMessage
MultiContextRenderBackend::getIdRegistrations()
{
    static const kodachi::GroupAttribute kQueryAttr("type",
                kodachi::StringAttribute("idRegistrations"), false);

    kodachi::GroupBuilder gb;

    for (auto& context : mContexts) {
        const auto idRegistrationsData =
                context.second.mRenderBackend.getData(kQueryAttr);

        if (idRegistrationsData.isValid()) {
            gb.update(idRegistrationsData.getAttr());
        }
    }

    if (gb.isValid()) {
        return DataMessage(gb.build());
    }

    return {};
}

MultiContextRenderBackend::DataMessage
MultiContextRenderBackend::getIdPass()
{
    static const kodachi::GroupAttribute kIdPassQueryAttr(
            "type", kodachi::StringAttribute("idPass"), false);

    int64_t numTiles = 0;
    std::vector<std::tuple<kodachi::GroupAttribute, int32_t, int32_t>> idPassAttrs;
    for (auto& context : mContexts) {
        const auto idPassData =
                context.second.mRenderBackend.getData(kIdPassQueryAttr);

        if (idPassData.isValid()) {
            const kodachi::GroupAttribute idPassAttr = idPassData.getAttr();
            const kodachi::GroupAttribute tilesAttr = idPassAttr.getChildByName("tiles");
            numTiles += tilesAttr.getNumberOfChildren();

            idPassAttrs.emplace_back(idPassAttr.getChildByName("tiles"),
                    context.second.mXOffset, context.second.mYOffset);
        }
    }

    if (idPassAttrs.empty()) {
        return {};
    }

    int64_t count = 0;
    kodachi::GroupBuilder tilesGb;
    tilesGb.reserve(numTiles);
    for (const auto& idPassAttr : idPassAttrs) {
        const kodachi::GroupAttribute tilesAttr = std::get<0>(idPassAttr);
        const int32_t xOffset = std::get<1>(idPassAttr);
        const int32_t yOffset = std::get<2>(idPassAttr);

        for (int64_t i = 0; i < tilesAttr.getNumberOfChildren(); ++i) {
            const kodachi::GroupAttribute tileAttr = tilesAttr.getChildByIndex(i);

            const kodachi::GroupAttribute bufsAttr = tileAttr.getChildByName("bufs");
            const kodachi::IntAttribute vpAttr = tileAttr.getChildByName("vp");

            tilesGb.set(std::to_string(count++),
                    kodachi::GroupAttribute("vp", offsetViewport(vpAttr, xOffset, yOffset),
                                            "bufs", bufsAttr,
                                            false));
        }
    }

    return kodachi::GroupAttribute("avp", mRegionViewport,
                                   "rvp", mRegionViewport,
                                   "tiles", tilesGb.build(),
                                   false);
}

} // namespace kodachi

// define and register the plugin
namespace {
using namespace kodachi;

DEFINE_KODACHI_BACKEND_PLUGIN(MultiContextRenderBackend);
} // anonymous namespace

void registerPlugins() {
    REGISTER_PLUGIN(MultiContextRenderBackend, "MultiContextRenderBackend", 0, 1);
}

