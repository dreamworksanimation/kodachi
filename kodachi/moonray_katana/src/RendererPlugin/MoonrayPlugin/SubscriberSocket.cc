// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "SubscriberSocket.h"

#include <kodachi/logging/KodachiLogging.h>

namespace {

KdLogSetup("mfk::SubscriberSocket");

zmq::context_t&
getZmqContext()
{
    static zmq::context_t sZmqContext(1);

    return sZmqContext;
}

} // anonymous namespace

namespace mfk {

inline bool
isIPCProtocol(const std::string& socketAddress)
{
    if (socketAddress.size() < 6) {
        return false;
    }

    return (socketAddress[0] == 'i' &&
            socketAddress[1] == 'p' &&
            socketAddress[2] == 'c' &&
            socketAddress[3] == ':' &&
            socketAddress[4] == '/' &&
            socketAddress[5] == '/');
}

SubscriberSocket::SubscriberSocket(std::string transport)
: mSubSocket(getZmqContext(), ZMQ_SUB)
{
    std::string socketAddress =
            (isIPCProtocol(transport) ? transport.substr(6) : transport);

    const std::uint64_t socketAddressHash = std::hash<std::string>{}(socketAddress);

    std::string actualSocketAddress = "ipc:///tmp/" + std::to_string(socketAddressHash);

    KdLogInfo("Connecting ZMQ SUB Socket to \"" << actualSocketAddress << "\"");

    try {
        mSubSocket.connect(actualSocketAddress.c_str());
    }
    catch (zmq::error_t & e) {
        KdLogError("ZMQ SUB socket failed to connect..." << e.what());
    }

    KdLogInfo("ZMQ SUB socket connected successfully.");

    mSubSocket.setsockopt(ZMQ_SUBSCRIBE, "", 0);

    actualSocketAddress.append("_sync");
    transport.append("_sync");
    zmq::socket_t syncSocket(getZmqContext(), ZMQ_REQ);
    KdLogInfo("Connecting ZMQ REQ Socket to \"" << actualSocketAddress << "\"");

    try {
        syncSocket.connect(actualSocketAddress.c_str()); // NEW
    }
    catch (zmq::error_t & e) {
        KdLogError("ZMQ REQ socket failed to connect..." << e.what());
    }

    KdLogInfo("ZMQ REQ socket connected successfully.");

    KdLogDebug("Sending Sync Request");
    zmq::message_t msg(0);
    syncSocket.send(msg);

    KdLogDebug("Waiting for Sync Reply");
    zmq::message_t recvMsg;
    syncSocket.recv(&recvMsg);
}

SubscriberSocket::~SubscriberSocket()
{
    stopCallbackLoop();
}

bool
SubscriberSocket::checkForMessages(long timeout)
{
    zmq::pollitem_t pollitem{mSubSocket, 0, ZMQ_POLLIN, 0};

    return zmq::poll(&pollitem, 1, timeout) > 0;
}

kodachi::Attribute
SubscriberSocket::getMessage()
{
    zmq::message_t msg;

    mSubSocket.recv(&msg);

    return kodachi::Attribute::parseBinary(
            static_cast<const char*>(msg.data()), msg.size());
}

void
SubscriberSocket::startCallbackLoop(MsgCallback&& callback, long timeout)
{
    if (mCallbackThread.joinable()) {
        KdLogWarn("Socket is already running a callback loop")
        return;
    }

    mCallbackLoopRunning = true;

    mCallbackThread = std::thread([this, callback, timeout]()
            {
                while (mCallbackLoopRunning) {
                    if (checkForMessages(timeout)) {
                        callback(getMessage());
                    }
                }
            });
}

void
SubscriberSocket::stopCallbackLoop()
{
    if (mCallbackThread.joinable()) {
        mCallbackLoopRunning = false;
        mCallbackThread.join();
    }
}

} // namespace mfk

