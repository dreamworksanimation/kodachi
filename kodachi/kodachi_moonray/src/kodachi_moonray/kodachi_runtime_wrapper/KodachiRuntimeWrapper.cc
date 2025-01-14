// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "KodachiRuntimeWrapper.h"

#include <boost/scoped_array.hpp>
#include <boost/archive/iterators/base64_from_binary.hpp>
#include <boost/archive/iterators/binary_from_base64.hpp>
#include <boost/archive/iterators/transform_width.hpp>
#include <boost/archive/iterators/remove_whitespace.hpp>
#include <boost/archive/iterators/dataflow_exception.hpp>

#include <kodachi/Kodachi.h>
#include <kodachi/OpTreeUtil.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

#include <scene_rdl2/scene/rdl2/Attribute.h>

#include <fstream>

namespace {
bool sHostSet = false;

// taken from PamHelpers
namespace bai = boost::archive::iterators;

typedef std::vector<char> CharArray;
typedef CharArray::const_iterator ConstCharIterator;

// Each char in base64 is 6 bits (2^6 = 64) so we need transforms
// that convert from 8 bit bytes to 6 bit ints, and vice versa.

typedef bai::transform_width<ConstCharIterator,6,8> BitsTransform;
typedef bai::base64_from_binary<BitsTransform> EncodeIterator;

typedef bai::binary_from_base64<ConstCharIterator> UnpackTransform;
typedef bai::transform_width<UnpackTransform,8,6> DecodeIterator;

std::string toBase64
    (const char* aBytes,
     size_t      aLength)
{
    if (aBytes == nullptr || aLength == 0) {return std::string();}

    // Base64 uses a "safe" 64 entry character set to represent binary
    // data. Because 2^6 = 64, Base64 only needs 6 bits to represent
    // each character. The trick is reinterpreting blocks of 8 bit
    // bytes as 6 bit characters instead. This can be done by treating
    // each sequence of 3 8bit bytes as 4 6bit characters (24 bits
    // in either case).
    //
    // You have to be extra careful at the end though, if the input
    // bytes length is not evenly divisble by three. In such cases you
    // have to supply enough 0 bits and the end to complete the next 6 bit
    // character. You then have to add padding to tell the decoder how
    // it should handle things. Refer to RFC 4648 for details.

    size_t thePadCount = (3 - (aLength % 3)) % 3;

    // There are more 6bit chars than there are 8bit chars, plus there
    // can be a few bytes of padding.
    std::string theEncoded;
    theEncoded.reserve(static_cast<std::string::size_type>
        (4.0 * std::ceil(static_cast<double>(aLength) / 3.0)));

    if (thePadCount == 0)
    {
        // The bytes will evenly encode into 6bit chars.
        theEncoded.assign(EncodeIterator(aBytes),
                          EncodeIterator(aBytes + aLength));
    }
    else if (thePadCount == 1)
    {
        // Two extra bytes have useful bits but we need one extra empty byte
        // to complete the last 6bit char.
        theEncoded.assign(EncodeIterator(aBytes),
                          EncodeIterator(aBytes + aLength - 2));

        char theTemp[3];
        theTemp[0] = aBytes[aLength - 2];
        theTemp[1] = aBytes[aLength - 1];
        theTemp[2] = 0;
        theEncoded.append(EncodeIterator(theTemp),
                          EncodeIterator(theTemp + 2));
    }
    else // thePadCount == 2
    {
        // One extra byte has useful bits but we need one extra empty byte
        // to complete the last 6bit char.
        theEncoded.assign(EncodeIterator(aBytes),
                          EncodeIterator(aBytes + aLength - 1));

        char theTemp[2];
        theTemp[0] = aBytes[aLength - 1];
        theTemp[1] = 0;
        theEncoded.append(EncodeIterator(theTemp),
                          EncodeIterator(theTemp + 1));
    }

    theEncoded.append(thePadCount, '=');

    return theEncoded;
}

std::string fromBase64
    (const char* aBytes,
     size_t      aLength)
{
    // base64 data is always at least 4 bytes long (its length is
    // actually always an even multiple of 4 also).
    if (aBytes == nullptr || aLength < 4) {return std::string();}

    try
    {
        // Need to know how many pad bytes were used during encryption.
        size_t thePadCount = 0;
        if (aBytes[aLength - 2] == '=') {
            thePadCount = 2;
        } else if (aBytes[aLength - 1] == '=') {
            thePadCount = 1;
        }

        // There are fewer 8bit chars than there are 6bit chars, plus we
        // can drop the padding.
        std::string theUnencoded;
        theUnencoded.reserve(static_cast<std::string::size_type>
                    ((3.0 * std::floor(static_cast<double>(aLength) / 4.0))
                             - static_cast<double>(thePadCount)));

        if (thePadCount == 0)
        {
            // The encoded bytes will evenly fit into 8bit bytes.
            theUnencoded.assign(DecodeIterator(aBytes),
                                DecodeIterator(aBytes+aLength));
        }
        else
        {
            // The last 4byte block has padding, so only decode up to that.
            theUnencoded.assign(DecodeIterator(aBytes),
                                DecodeIterator(aBytes + aLength - 4));

            // Treat the last block separately. Replace the padding
            // chars with 0 bytes and decode (remembering '\0' is
            // encoded as 'A' in Base64).
            char theTemp[4];
            memcpy(theTemp, aBytes + aLength - 4, 4);
            if (thePadCount == 1) {
                theTemp[3] = 'A';
            } else {
                theTemp[2] = 'A';
                theTemp[3] = 'A';
            }

            theUnencoded.append(DecodeIterator(theTemp),
                                DecodeIterator(theTemp + 4));

            // Don't forget to erase the zero padding bytes.
            theUnencoded.erase(theUnencoded.end() - thePadCount,
                               theUnencoded.end());
        }

        return theUnencoded;
    }
    catch (boost::archive::iterators::dataflow_exception& e)
    {
        std::ostringstream theMsg;
        theMsg << e.what() << "; failed to decode Base64 block";
        throw std::runtime_error(theMsg.str());
    }
}

std::string
encodeOpTree(const kodachi::GroupAttribute& optreeAttr)
{
    std::vector<char> binaryVec;
    optreeAttr.getBinary(&binaryVec);

    return toBase64(binaryVec.data(), binaryVec.size());
}

kodachi::GroupAttribute
decodeOpTree(const arras::rdl2::String& encodedOpTree)
{
    const std::string decodedOpTreeBinary =
            fromBase64(encodedOpTree.data(), encodedOpTree.size());

    return kodachi::Attribute::parseBinary(
            decodedOpTreeBinary.data(), decodedOpTreeBinary.size());
}

kodachi::GroupAttribute
loadOpTreeFromFile(const arras::rdl2::String& filePath)
{
    std::ifstream inputStream(filePath, std::ios::binary );
    if (!inputStream) {
        throw std::runtime_error("KodachiRuntime: could not load file: " + filePath);
    }

    std::vector<char> buffer(std::istreambuf_iterator<char>(inputStream), {});
    return kodachi::Attribute::parseBinary(buffer.data(), buffer.size());
}

} // anonymous namespace

namespace kodachi_moonray {

kodachi::KdPluginStatus
KodachiRuntimeWrapper::setHost(kodachi::KdPluginHost* host)
{
    if (sHostSet) {
        return kodachi::KdPluginStatus::FnPluginStatusOK;
    }

    sHostSet = true;
    kodachi::Attribute::setHost(host);
    kodachi::GroupBuilder::setHost(host);
    kodachi::PluginManager::setHost(host);
    return kodachi::KodachiRuntime::setHost(host);
}

void
KodachiRuntimeWrapper::setOpTree(const kodachi::GroupAttribute& opTreeAttr)
{
    arras::rdl2::SceneObject::UpdateGuard guard(this);
    set("optree", encodeOpTree(opTreeAttr));
    set("optree_mode", arras::rdl2::Int(0));

    const char* rezResolveEnv = ::getenv("REZ_RESOLVE");
    if (rezResolveEnv) {
        set("rez_resolve", arras::rdl2::String(rezResolveEnv));
    }

    char* cwd = ::getcwd(nullptr, 0);
    if (cwd) {
        set("working_directory", arras::rdl2::String(cwd));
        ::free(cwd);
    }
}

KodachiRuntimeWrapper::ClientWrapper::~ClientWrapper()
{
    if (mFlushPluginCaches) {
        std::cerr << "KodachiRuntimeWrapper: flushing plugin caches\n";
        kodachi::PluginManager::flushPluginCaches();
    }
}

kodachi::GroupAttribute
KodachiRuntimeWrapper::ClientWrapper::cookLocation(const std::string& location)
{
    if (!mKodachiClient) {
        return {};
    }

    kodachi::GroupAttribute locationAttrs;
    mArena.execute([&]() {
        const auto locationData = mKodachiClient->cookLocation(location, true);

        if (locationData.doesLocationExist()) {
            locationAttrs = locationData.getAttrs();
        }
    });

    return locationAttrs;
}

KodachiRuntimeWrapper::ClientWrapperPtr
KodachiRuntimeWrapper::getClientWrapper() const
{
    KodachiRuntimeWrapper::ClientWrapperPtr clientWrapper = mClientWeakPtr.lock();

    if (!clientWrapper) {
        std::lock_guard<std::mutex> lock(mClientCreationMutex);
        clientWrapper = mClientWeakPtr.lock();

        if (!clientWrapper) {
            if (!sHostSet) {
                if (kodachi::bootstrap()) {
                    setHost(kodachi::getHost());
                } else {
                    throw std::runtime_error("Failed to bootstrap kodachi");
                }
            }

            if (!mKodachiRuntime) {
                mKodachiRuntime = kodachi::KodachiRuntime::createRuntime();
            }

            const std::string& optree = get<arras::rdl2::String>("optree");
            if (optree.empty()) {
                throw std::runtime_error("KodachiRuntimeWrapper: optree attribute not specified");
            }

            const arras::rdl2::Int optreeMode = get<arras::rdl2::Int>("optree_mode");

            kodachi::GroupAttribute optreeAttr;
            if (optreeMode == 0) {
                optreeAttr = decodeOpTree(optree);
            } else {
                optreeAttr = loadOpTreeFromFile(optree);
            }

            const arras::rdl2::Bool flushPluginCaches =
                    get<arras::rdl2::Bool>("flush_plugin_caches");

            mKodachiRuntime = kodachi::KodachiRuntime::createRuntime();

            auto client = kodachi::optree_util::loadOpTree(
                    mKodachiRuntime, optreeAttr);

            if (!client) {
                throw std::runtime_error("KodachiRuntimeWrapper: failed to load optree");
            }

            {
                kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
                asb.setAttr("moonrayGlobalStatements.reuse cached materials",
                            kodachi::IntAttribute(false));

                auto txn = mKodachiRuntime->createTransaction();

                auto op = txn->createOp();

                txn->setOpArgs(op, "AttributeSet", asb.build());

                txn->setOpInputs(op, {client->getOp()});

                txn->setClientOp(client, op);

                mKodachiRuntime->commit(txn);
            }

            // The AdjustScreenWindowResolve implicit resolver will error if we
            // start cooking locations on multiple threads at the same time
            // but cooking root once seems to fix it. Possibly some static cache
            // without thread-safe initialization.
            const auto rootLd = client->cookLocation("/root", false);

            if (!rootLd.doesLocationExist()) {
                throw std::runtime_error("KodachiRuntimeWrapper: failed to cook /root");
            }

            const kodachi::StringAttribute typeAttr =
                    rootLd.getAttrs().getChildByName("type");

            if (typeAttr == "error") {
                const kodachi::StringAttribute errorMessageAttr =
                        rootLd.getAttrs().getChildByName("errorMessage");
                throw std::runtime_error(
                        "KodachiRuntimeWrapper: error on /root: " + errorMessageAttr.getValue("", false));
            }

            clientWrapper = std::make_shared<ClientWrapper>(client, flushPluginCaches);

            mClientWeakPtr = clientWrapper;
        }
    }

    return clientWrapper;
}

}
