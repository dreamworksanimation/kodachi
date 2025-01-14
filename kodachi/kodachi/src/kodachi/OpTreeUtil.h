// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

#include <kodachi/KodachiRuntime.h>

namespace kodachi {
namespace optree_util {

kodachi::GroupAttribute convertToKodachiOpTree(const kodachi::GroupAttribute& optree);

/**
 * Uses the provided runtime to create a Transaction and load the optree.
 * Returns a Client with the ClientOp set to the last op in the optree.
 * Returns nullptr if optree is empty
 */
KodachiRuntime::Client::Ptr
loadOpTree(const KodachiRuntime::Ptr& kodachiRuntime,
           const GroupAttribute& optreeAttr);

// Parses XML files on disk to build a group attribute containing a collection of
// op descriptions; each entry is itself a GroupAttribute, and contains at least
// two attributes:
//      1) an opType (StringAttribute), and
//      2) an opArgs (GroupAttribute)
// Other attributes may be present, e.g. addSystemOpArgs (IntAttribute), etc.
//
// Input: no direct input. Reads the full path to the XML file(s) by reading
//        KODACHI_OP_DESC_COLLECTION_XML environment variable.
//
// Output: a kodachi::GroupAttribute
//
kodachi::GroupAttribute loadImplicitResolversOpCollection();

// Resolves an OpCollection into an OpChain
//
// Takes in a collection of op descriptions (a GroupAttribute
// containing opType, opArgs, etc) and a GroupAttribute containing
// system op args.
//
// Goes over all the op descriptions and checks if any of them has
// an "addSystemOpArgs", and whether or not it is set to 1 (true),
// if yes, then the opArgs is updated by adding a copy of systemOpArgs.
//
// Returns a new GroupAttribute containing the modified opDescrCollection.
//
// All "addSystemOpArgs" are removed to avoid overwriting the "system"
// attributes in subsequent calls of this function on a previously processed
// collection.
//
kodachi::GroupAttribute addSystemOpArgsToOpCollection(
        const kodachi::GroupAttribute& opDescrCollection,
        const kodachi::GroupAttribute& systemOpArgs);

} // namespace backend_util
} // namespace kodachi

