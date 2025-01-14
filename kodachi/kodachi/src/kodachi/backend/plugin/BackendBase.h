// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/attribute/Attribute.h>
#include <kodachi/backend/suite/BackendSuite.h>

#include <kodachi/plugin_system/PluginManager.h>

#include <memory>

namespace kodachi {

/**
 * Follows a similar pattern to Katana's RenderBase. Backend Plugins inherit this
 * class and implement the virtual functions. The BackendClient can then
 * instantiate and interact with Backend plugin instances.
 */
class BackendBase
{
public:

    struct DataMessage
    {
        using PayloadPtr = std::shared_ptr<void>;
        using PayloadVec = std::vector<PayloadPtr>;

        DataMessage() = default;
        DataMessage(kodachi::Attribute attr)
        :mAttr(std::move(attr))
        {}

        std::size_t addPayload(PayloadPtr payload)
        {
            if (!mPayloads) {
                mPayloads.reset(new PayloadVec);
            }

            mPayloads->emplace_back(std::move(payload));
            return mPayloads->size() - 1;
        }

        PayloadPtr getPayload(const std::size_t idx) const
        {
            if (mPayloads && idx < mPayloads->size()) {
                return mPayloads->at(idx);
            }

            return nullptr;
        }

        kodachi::Attribute mAttr;
        std::unique_ptr<PayloadVec> mPayloads;
    };

    BackendBase();
    virtual ~BackendBase();

    virtual bool initialize(const GroupAttribute& opTreeAttr) = 0;

    virtual void start() = 0;
    virtual void stop() = 0;

    virtual void setData(const GroupAttribute& data) = 0;

    virtual DataMessage getData(const GroupAttribute& query) = 0;

    static kodachi::GroupAttribute getStaticData(
            const kodachi::GroupAttribute& configAttr);

    static void flush();
    static KdPluginStatus setHost(KdPluginHost* host);
    static KdPluginHost getHost();

    static KodachiBackendSuite_v1 createSuite(KdBackendHandle (*create)(),
                                              KdAttributeHandle (*getStaticData)(KdAttributeHandle));
    static KdBackendHandle newBackendHandle(kodachi::BackendBase* backend);

    static constexpr unsigned int _apiVersion = 1;
    static constexpr const char*  _apiName = "KodachiBackend";

private:
    static KdPluginHost* sHost;
};

} // namespace kodachi

// Plugin Registering Macro.
#define DEFINE_KODACHI_BACKEND_PLUGIN(BACKEND_CLASS)                               \
                                                                                   \
    kodachi::KdPlugin BACKEND_CLASS##_plugin;                                      \
                                                                                   \
    KdBackendHandle BACKEND_CLASS##_create()                                       \
    {                                                                              \
        return kodachi::BackendBase::newBackendHandle(                             \
                BACKEND_CLASS::create());                                          \
    }                                                                              \
                                                                                   \
    KdAttributeHandle BACKEND_CLASS##_getStaticData(                               \
                                            KdAttributeHandle configHandle)        \
    {                                                                              \
        const auto configAttr = kodachi::Attribute::CreateAndRetain(configHandle); \
        return BACKEND_CLASS::getStaticData(configAttr).getRetainedHandle();       \
    }                                                                              \
                                                                                   \
    KodachiBackendSuite_v1 BACKEND_CLASS##_suite =                                 \
            kodachi::BackendBase::createSuite(                                     \
                    BACKEND_CLASS##_create,                                        \
                    BACKEND_CLASS##_getStaticData);                                \
                                                                                   \
    const void* BACKEND_CLASS##_getSuite()                                         \
    {                                                                              \
        return &BACKEND_CLASS##_suite;                                             \
    }

