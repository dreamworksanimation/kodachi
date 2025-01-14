// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <kodachi/attribute/Attribute.h>

#include <zmq.hpp>

#include <functional>
#include <memory>
#include <thread>

namespace mfk {

class SubscriberSocket
{
public:
    SubscriberSocket(std::string transport);
    ~SubscriberSocket();

    bool checkForMessages(long timeout);

    kodachi::Attribute getMessage();

    using MsgCallback = std::function<void(const kodachi::GroupAttribute&)>;

    void startCallbackLoop(MsgCallback&& callback, long timeout);
    void stopCallbackLoop();

private:
    zmq::socket_t mSubSocket;

    bool mCallbackLoopRunning = false;
    std::thread mCallbackThread;
};

} // namespace mfk

