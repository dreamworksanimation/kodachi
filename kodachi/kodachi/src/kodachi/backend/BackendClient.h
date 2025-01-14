// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/backend/suite/BackendSuite.h>
#include <kodachi/plugin_system/PluginManager.h>

// system
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kodachi {

/**
 * Used to create instances of Kodachi Backends and intKatanaerface with them. The
 * Type of backend to create and its settings are stored on /root of the optree
 * passed to the initialize function. To allow backends to be as flexible as
 * possible in their functionality, interacting with a backend is primarily
 * done by passing GroupAttributes back and forth using the getData() and setData()
 * methods.
 *
 * Backends can implement getStaticData() to return information about themselves
 * without requiring an optree to be constructed.
 */
class BackendClient
{
public:

    /**
     * Returned by calls to getData(). A DataMessage can contain a GroupAttribute
     * and then optionally one or more payloads. Payloads are returned as void*
     * to provide maximum flexibility, the payload's properties can be described
     * in the GroupAttribute.
     */
    class DataMessage
    {
    public:
        using HandleSharedPtr = std::shared_ptr<
                std::remove_pointer<KdBackendDataMessageHandle>::type>;

        DataMessage(DataMessage&&) = default;

        kodachi::Attribute getAttr() const;

        // Returns an empty payload if none exists for the provided index
        std::shared_ptr<void> getPayload(std::size_t idx) const;

        template <class T>
        std::shared_ptr<T> getPayloadAs(std::size_t idx) const
        {
            std::shared_ptr<void> payload = getPayload(idx);
            return std::shared_ptr<T>(payload, reinterpret_cast<T*>(payload.get()));
        }

        bool isValid() const;

    private:
        DataMessage() = default;
        DataMessage(const KodachiBackendSuite_v1* suite,
                    KdBackendDataMessageHandle h);

        const KodachiBackendSuite_v1* mSuite = nullptr;
        HandleSharedPtr mHandle;

        friend class BackendClient;
    };

    /**
     * Get static data from a Backend Plugin. e.g. 'terminalOps'
     */
    static GroupAttribute getStaticData(const std::string& pluginName,
                                        const kodachi::GroupAttribute& configAttr);

    BackendClient();
    BackendClient(BackendClient&&);
    BackendClient& operator=(BackendClient&&);
    BackendClient(const BackendClient&) = delete;
    BackendClient& operator=(const BackendClient&) = delete;

    bool isValid() const;

    /**
     * Loads the provided optree into a KodachiRuntime and sets the last
     * on in the GroupAttribute as the clientOp. Cooks root and
     * Finds the 'backendSettings' attribute and uses it to
     * create the new BackendClient.
     */
    bool initialize(const kodachi::GroupAttribute& opTree);

    void start();
    void stop();

    void setData(const kodachi::GroupAttribute& data);

    DataMessage getData(const kodachi::GroupAttribute& query) const;

    static KdPluginStatus setHost(KdPluginHost* host);

    using HandleUniquePtr = std::unique_ptr<std::remove_pointer<KdBackendHandle>::type,
                                            std::function<void(KdBackendHandle)>>;
private:
    const KodachiBackendSuite_v1* mSuite = nullptr;

    HandleUniquePtr mHandle;
};

} // namespace kodachi

