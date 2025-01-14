// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "OpTreeUtil.h"

#include "internal/internal_utils.h"

// Kodachi
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/logging/KodachiLogging.h>

// C++
#include <fstream>
#include <queue>
#include <stack>
#include <unordered_set>
#include <unordered_map>

//------------------------------------------------------------------------------

namespace
{
KdLogSetup("OpTreeBuilder");

kodachi::GroupAttribute
mergeGroupAttributes(const std::vector<kodachi::GroupAttribute>& groupAttrs,
                     std::string baseName = "op")
{
    kodachi::GroupBuilder gb;
    for (const auto& group : groupAttrs) {
        if (!group.isValid()) {
            continue;
        }

        for (const auto child : group) {
            gb.setWithUniqueName(baseName, child.attribute);
        }
    }

    return gb.build();
}

const kodachi::StringAttribute& kNoOpAttr()
{
    static kodachi::StringAttribute kNoOp("no-op");
    return kNoOp;
}

} // anonymous namespace

//------------------------------------------------------------------------------

namespace kodachi {
namespace optree_util {

/**
 * Convert a Katana optree into a Kodachi optree
 */
kodachi::GroupAttribute
convertToKodachiOpTree(const kodachi::GroupAttribute& optree)
{
    if (!optree.isValid()) {
        return { };
    }

    // Use this map to update opInput attributes
    std::unordered_map<kodachi::string_view, std::string, kodachi::StringViewHash> oldIdToNewIdMap;

    // Generate new KodachiOpIds for each op
    for (const auto op : optree) {
        oldIdToNewIdMap.emplace(op.name, kodachi::KodachiOpId::generate().str());
    }

    // Create a new op tree using the above map
    kodachi::GroupBuilder optreeGB;
    for (const auto op : optree) {
        kodachi::GroupAttribute attrs = op.attribute;
        kodachi::StringAttribute opInputsAttr = attrs.getChildByName("opInputs");

        // Build the new opInputs list
        std::vector<const char*> newOpInputs;
        if (opInputsAttr.isValid()) {
            const kodachi::StringAttribute::array_type opInputsVect =
                    opInputsAttr.getNearestSample(0.0f);

            // Convert the old opInputs list to use the new IDs
            for (const kodachi::string_view input : opInputsVect) {
                const auto inputIter = oldIdToNewIdMap.find(input);
                if (inputIter == oldIdToNewIdMap.cend()) {
                    continue;
                }

                newOpInputs.push_back(inputIter->second.c_str());
            }
        }

        // Update the new op tree with new op info
        const kodachi::Attribute opTypeAttr = attrs.getChildByName("opType");
        const kodachi::Attribute opArgsAttr = attrs.getChildByName("opArgs");

        kodachi::GroupAttribute opAttr;
        if (newOpInputs.empty()) {
            opAttr = kodachi::GroupAttribute("opType", opTypeAttr,
                                             "opArgs", opArgsAttr, false);
        } else {
            const kodachi::StringAttribute opInputsAttr(newOpInputs.data(), newOpInputs.size(), 1);

            opAttr = kodachi::GroupAttribute("opType", opTypeAttr,
                                             "opArgs", opArgsAttr,
                                             "opInputs", opInputsAttr,
                                             false);
        }

        const std::string& newOpId = oldIdToNewIdMap[op.name];
        optreeGB.set(newOpId, opAttr);
    }

    return optreeGB.build();
}

KodachiRuntime::Client::Ptr
loadOpTree(const KodachiRuntime::Ptr& kodachiRuntime,
           const GroupAttribute& optreeAttr)
{
    auto txn = kodachiRuntime->createTransaction();

    const std::vector<KodachiRuntime::Op::Ptr> ops =
            txn->parseGraph(optreeAttr);

    if (ops.empty()) {
        return nullptr;
    }

    auto client = txn->createClient();

    txn->setClientOp(client, ops.back());

    kodachiRuntime->commit(txn);

    return client;
}

//-------------------------------------------------
// Parses one or more XMLs on disk to build a group attribute containing a collection of
// op descriptions; each entry is itself a GroupAttribute, and contains at least
// two attributes:
//      1) an opType (StringAttribute), and
//      2) an opArgs (GroupAttribute)
// Other attributes may be present, e.g. addSystemOpArgs (IntAttribute), etc.
//
// Input: no direct input. Reads the full path to the XML file(s) by reading
//        KODACHI_RESOLVERS_COLLECTION_XML environment variable.
//
// Output: a kodachi::GroupAttribute
//
kodachi::GroupAttribute
loadImplicitResolversOpCollection()
{
    //------------------------------------------
    // Read and merge XML files into a group attribute first

    const char* pathToXMLs = ::getenv("KODACHI_RESOLVERS_COLLECTION_XML");
    if (pathToXMLs == nullptr) {
        KdLogError("Failed to read collection of implicit resolvers from disk; "
                   "environment variable KODACHI_RESOLVERS_COLLECTION_XML not found.");
        return { };
    }

    const std::string envVarStr(pathToXMLs);

    // Set a maximum allowed length for the input string
    constexpr size_t MAX_INPUT_LENGTH = 10 * 1024 * 1024; // 10 MB
    if (envVarStr.length() > MAX_INPUT_LENGTH) {
        KdLogError("Failed to read collection of implicit resolvers from disk; "
                   "environment variable KODACHI_RESOLVERS_COLLECTION_XML list is too large.");
        return { };
    }

    // Limit the number of delimiters 
    constexpr size_t MAX_DELIMITERS = 10000; // arbitrary limit
    size_t delimiterCount = std::count(envVarStr.begin(), envVarStr.end(), ':');
    if (delimiterCount > MAX_DELIMITERS) {
        KdLogError("Failed to read collection of implicit resolvers from disk; "
                   "environment variable KODACHI_RESOLVERS_COLLECTION_XML list is too large.");
        return { };
    }

    std::vector<std::string> implicitResolverXMLPaths =
            internal::splitString(envVarStr, ':');
    if (implicitResolverXMLPaths.back().empty()) {
        implicitResolverXMLPaths.pop_back();
    }

    std::vector<kodachi::GroupAttribute> opCollections;
    for (const auto& xmlPath : implicitResolverXMLPaths) {
        if (internal::fileOrDirExists(xmlPath)) {
            std::ifstream fin(xmlPath);
            std::stringstream buffer;
            buffer << fin.rdbuf();

            opCollections.emplace_back(
                    kodachi::Attribute::parseXML(buffer.str().c_str()));
        }
    }

    // Merge op collections; use a unique name for each entry otherwise
    // resolvers from different packages will overwrite one another
    const kodachi::GroupAttribute mergedXMLs = mergeGroupAttributes(opCollections);

    //------------------------------------------
    // Sort implicit resolvers by stage priority:
    //
    //    0 -  99 : Before Preprocess resolvers
    //
    //  100 - 199 : Preprocess resolvers ( FIXED )
    //
    //  200 - 299 : Before Standard Resolvers
    //
    //  300 - 399 : Standard Resolvers ( FIXED )
    //
    //  400 - 499 : After Standard Resolvers
    //
    //  500 - 599 : Postprocess Resolvers ( FIXED )
    //
    //  600 - 699 : AfterPostProcessResolvers

    // To sort op descriptions based on priority
    std::multimap<int /* priority */, kodachi::GroupAttribute> resolverCollection;
    for (const GroupAttributeChild entry : mergedXMLs) {
        const kodachi::GroupAttribute resolverAttr = entry.attribute;
        const kodachi::IntAttribute priorityAttr = resolverAttr.getChildByName("priority");
        if (priorityAttr.isValid()) {
            resolverCollection.emplace(priorityAttr.getValue(), resolverAttr);
        }
        else {
            KdLogError("Missing attribute \"priority\"; loading implicit resolvers from XML failed.");
            return { };
        }
    }

    // From multimap to GroupBuilder
    kodachi::GroupBuilder gb;
    for (const auto& resolver : resolverCollection) {
        gb.setWithUniqueName("op", resolver.second);
    }

    return gb.build();
}

//-------------------------------------------------

kodachi::GroupAttribute
addSystemOpArgsToOpCollection(const kodachi::GroupAttribute& opCollection,
                              const kodachi::GroupAttribute& systemOpArgs)
{
    kodachi::GroupBuilder gb;
    gb.reserve(opCollection.getNumberOfChildren());

    for (const auto& opDesc : opCollection) {
        const kodachi::GroupAttribute opDescAttr(opDesc.attribute);
        const kodachi::IntAttribute addSysOpArgs =
                opDescAttr.getChildByName("addSystemOpArgs");

        if (!addSysOpArgs.isValid()) {
            // Nothing to do to this op
            gb.set(opDesc.name, opDescAttr);
            continue;
        }

        kodachi::GroupBuilder opBuilder;
        opBuilder.update(opDescAttr);

        if (addSysOpArgs.getValue() == 1 /* true */) {
            opBuilder.set("opArgs.system", systemOpArgs);
        }

        opBuilder.del("addSystemOpArgs");

        // Add to the new op description collection
        gb.set(opDesc.name, opBuilder.build());
    }

    return gb.build();
}

//-------------------------------------------------

} // namespace optree_util
} // namespace kodachi

