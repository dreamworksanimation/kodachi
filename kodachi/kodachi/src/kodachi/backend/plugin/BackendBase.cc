// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include <kodachi/Kodachi.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/backend/plugin/BackendBase.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/KodachiRuntime.h>

#include <memory>
#include <vector>

struct KodachiBackendStruct
{
    KodachiBackendStruct(kodachi::BackendBase* backend)
    :   mBackend(backend)
    {}

    kodachi::BackendBase& getBackend() { return *mBackend; }

private:
    std::unique_ptr<kodachi::BackendBase> mBackend;
};

struct KodachiBackendDataMessageStruct
{
    KodachiBackendDataMessageStruct(kodachi::BackendBase::DataMessage&& dataMessage)
    : mDataMessage(std::move(dataMessage))
    {}

    const kodachi::BackendBase::DataMessage& getDataMessage() const { return mDataMessage; }

private:
    const kodachi::BackendBase::DataMessage mDataMessage;
};


////////////////////////////////////////////////////
// C callbacks implementations for the plugin suite
////////////////////////////////////////////////////

void
_releaseBackend(KdBackendHandle handle)
{
    delete handle;
}

uint8_t
_initialize(KdBackendHandle handle, KdAttributeHandle opTreeHandle)
{
    const kodachi::GroupAttribute opTreeAttr(kodachi::Attribute::CreateAndRetain(opTreeHandle));

    return static_cast<uint8_t>(handle->getBackend().initialize(opTreeAttr));
}

void
_start(KdBackendHandle handle)
{
    handle->getBackend().start();
}

void
_stop(KdBackendHandle handle)
{
    handle->getBackend().stop();
}

void
_setData(KdBackendHandle handle, KdAttributeHandle dataHandle)
{
    handle->getBackend().setData(kodachi::Attribute::CreateAndRetain(dataHandle));
}

KdBackendDataMessageHandle
_getData(KdBackendHandle handle, KdAttributeHandle queryHandle)
{
    auto dataMsg = handle->getBackend().getData(kodachi::Attribute::CreateAndRetain(queryHandle));
    if (!dataMsg.mAttr.isValid()) {
        return nullptr;
    }

    KdBackendDataMessageHandle msgHandle = new KodachiBackendDataMessageStruct(std::move(dataMsg));
    return msgHandle;
}

void
_releaseData(KdBackendDataMessageHandle handle)
{
    delete handle;
}

KdAttributeHandle
_getDataAttr(KdBackendDataMessageHandle handle)
{
    return handle->getDataMessage().mAttr.getRetainedHandle();
}

void*
_getDataPayload(KdBackendDataMessageHandle handle, uint64_t idx)
{
    return handle->getDataMessage().getPayload(idx).get();
}

KodachiBackendSuite_v1
_createSuite(KdBackendHandle (*create)(),
             KdAttributeHandle (*getStaticData)(KdAttributeHandle))
{
    KodachiBackendSuite_v1 suite =
    {
            create,
            _releaseBackend,
            getStaticData,
            _initialize,
            _start,
            _stop,
            _setData,
            _getData,
            _releaseData,
            _getDataAttr,
            _getDataPayload
    };

    return suite;
}

///////////////////////////
// BackendBase implementation
///////////////////////////

namespace kodachi {

kodachi::GroupAttribute
BackendBase::getStaticData(const kodachi::GroupAttribute& configAttr)
{
    return {};
}

void
BackendBase::flush()
{
}

KdPluginStatus
BackendBase::setHost(KdPluginHost* host)
{
    sHost = host;
    kodachi::setHost(host);
    kodachi::PluginManager::setHost(host);
    kodachi::GroupBuilder::setHost(host);
    kodachi::KodachiLogging::setHost(host);
    kodachi::Attribute::setHost(host);

    return kodachi::KodachiRuntime::setHost(host);
}

KdBackendHandle
BackendBase::newBackendHandle(kodachi::BackendBase* backend)
{
    if (backend) {
        return new KodachiBackendStruct(backend);
    }

    return nullptr;
}

KodachiBackendSuite_v1
BackendBase::createSuite(KdBackendHandle (*create)(),
                         KdAttributeHandle (*getStaticData)(KdAttributeHandle))
{
    return _createSuite(create, getStaticData);
}

BackendBase::BackendBase() {}
BackendBase::~BackendBase() {}

KdPluginHost* BackendBase::sHost = nullptr;

} // namespace kodachi

