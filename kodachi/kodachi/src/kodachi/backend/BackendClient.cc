// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


// self
#include <kodachi/backend/BackendClient.h>

#include <kodachi/KodachiRuntime.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/plugin_system/PluginManager.h>

// system
#include <map>

namespace {

KdLogSetup("BackendClient");

const KodachiBackendSuite_v1*
getSuite(const std::string& pluginName)
{
    static std::map<std::string, const KodachiBackendSuite_v1*> sRenderSuites;

    auto iter = sRenderSuites.find(pluginName);
    if (iter != sRenderSuites.end()) {
        return iter->second;
    }

    auto pluginHandle =
            kodachi::PluginManager::getPlugin(pluginName, "KodachiBackend", 1);
    if (!pluginHandle) {
        KdLogError("Could not get plugin: '" + pluginName + "'");
        return nullptr;
    }

    auto suite = reinterpret_cast<const KodachiBackendSuite_v1*>(
                          kodachi::PluginManager::getPluginSuite(pluginHandle));
    sRenderSuites.emplace(pluginName, suite);
    return suite;
}

kodachi::BackendClient::HandleUniquePtr
createBackendHandle(const KodachiBackendSuite_v1* suite)
{
    // release the backend handle back to the suite instead of calling delete
    return kodachi::BackendClient::HandleUniquePtr(
                 suite->createBackend(),
                [suite](KdBackendHandle h) { suite->releaseBackend(h); });
}

const std::string kRoot("/root");

} // anonymous namespace

namespace kodachi {

BackendClient::DataMessage::DataMessage(const KodachiBackendSuite_v1* suite,
                                        KdBackendDataMessageHandle h)
: mSuite(suite)
, mHandle(h, [suite](KdBackendDataMessageHandle h) { suite->releaseData(h); })
{
}

kodachi::Attribute
BackendClient::DataMessage::getAttr() const
{
    if (isValid()) {
        return kodachi::Attribute::CreateAndSteal(mSuite->getDataAttr(mHandle.get()));
    }

    return {};
}

std::shared_ptr<void>
BackendClient::DataMessage::getPayload(std::size_t idx) const
{
    if (isValid()) {
        void* rawData = mSuite->getDataPayload(mHandle.get(), idx);
        if (rawData) {
            // use aliasing constructor to keep the DataMessage alive for as long as the
            // payload is in use.
            return std::shared_ptr<void>(mHandle, rawData);
        }
    }

    return {};
}

bool
BackendClient::DataMessage::isValid() const
{
    return mHandle != nullptr;
}

kodachi::GroupAttribute
BackendClient::getStaticData(const std::string& pluginName,
                             const kodachi::GroupAttribute& configAttrs)
{
    auto suite = getSuite(pluginName);

    if (!suite) {
        return {};
    }

    return kodachi::Attribute::CreateAndSteal(
                                suite->getStaticData(configAttrs.getHandle()));
}

BackendClient::BackendClient() {}

BackendClient::BackendClient(BackendClient&& other)
: mSuite(other.mSuite)
, mHandle(std::move(other.mHandle))
{
    other.mSuite = nullptr;
}

BackendClient&
BackendClient::operator =(BackendClient&& other)
{
    mSuite = other.mSuite;
    mHandle = std::move(other.mHandle);
    other.mSuite = nullptr;

    return *this;
}

bool
BackendClient::isValid() const
{
    return static_cast<bool>(mHandle);
}

bool
BackendClient::initialize(const kodachi::GroupAttribute& opTree)
{
    if (mHandle) {
        KdLogError("BackendClient has already been initialized");
        return false;
    }

    // create a client so that we can cook root and get the backendSettings
    const auto runtime = KodachiRuntime::createRuntime();

    const auto txn = runtime->createTransaction();

    const auto client = txn->createClient();

    const std::vector<KodachiRuntime::Op::Ptr> ops = txn->parseGraph(opTree);
    if (ops.empty()) {
        KdLogError("OpTree is empty");
        return false;
    }

    txn->setClientOp(client, ops.back());
    runtime->commit(txn);

    KdLogDebug("Set ClientOp to op of type: " << ops.back()->getOpArgs().first)

    const auto rootData = client->cookLocation(kRoot, false);
    if (!rootData.doesLocationExist()) {
        KdLogError("could not cook '/root'");
        return false;
    }

    const GroupAttribute backendSettingsAttrs =
            rootData.getAttrs().getChildByName("kodachi.backendSettings");

    if (!backendSettingsAttrs.isValid()) {
        KdLogError("'kodachi.backendSettings' attr not found on '/root'");
        return false;
    }

    const StringAttribute backendAttr =
            backendSettingsAttrs.getChildByName("backend");
    if (!backendAttr.isValid()) {
        KdLogError("'backend' attr not set on backendSettings");
        return false;
    }

    const std::string backendName = backendAttr.getValue();
    mSuite = getSuite(backendName);
    if (!mSuite) {
        return false;
    }

    mHandle = createBackendHandle(mSuite);
    return static_cast<bool>(mSuite->initialize(mHandle.get(), opTree.getHandle()));
}

void
BackendClient::start()
{
    if (isValid()) {
        mSuite->start(mHandle.get());
    }
}

void
BackendClient::stop()
{
    if (isValid()) {
        mSuite->stop(mHandle.get());
    }
}

void
BackendClient::setData(const kodachi::GroupAttribute& data)
{
    if (isValid()) {
        mSuite->setData(mHandle.get(), data.getHandle());
    }
}

BackendClient::DataMessage
BackendClient::getData(const kodachi::GroupAttribute& query) const
{
    if (isValid()) {
        KdBackendDataMessageHandle msgHandle = mSuite->getData(mHandle.get(), query.getHandle());
        if (msgHandle) {
            return BackendClient::DataMessage(mSuite, msgHandle);
        }
    }

    return {};
}

KdPluginStatus
BackendClient::setHost(KdPluginHost* host)
{
    kodachi::KodachiRuntime::setHost(host);
    kodachi::PluginManager::setHost(host);
    kodachi::GroupBuilder::setHost(host);
    kodachi::KodachiLogging::setHost(host);
    return kodachi::Attribute::setHost(host);
}

} // namespace kodachi

