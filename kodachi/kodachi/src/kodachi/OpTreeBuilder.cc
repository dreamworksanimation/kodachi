// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#include "OpTreeBuilder.h"

#include "internal/internal_utils.h"

// Kodachi
#include <kodachi/logging/KodachiLogging.h>

// C++
#include <fstream>
#include <queue>
#include <stack>
#include <unordered_set>

//------------------------------------------------------------------------------

namespace {
KdLogSetup("OpTreeBuilder");
} // anonymous namespace

//------------------------------------------------------------------------------

namespace kodachi
{
struct OpTreeBuilder::op_constructor_key {};

OpTreeBuilder::Op::Op(const op_constructor_key&, const KodachiOpId& id)
: mId(id)
{ }

std::vector<KodachiOpId>
OpTreeBuilder::findTerminalOps(const kodachi::GroupAttribute& optree)
{
    if (!optree.isValid()) {
        return { };
    }

    const std::int64_t opCount = optree.getNumberOfChildren();
    std::unordered_set<kodachi::string_view, kodachi::StringViewHash> opsUsedAsInput(opCount);
    std::vector<kodachi::string_view> opList;
    opList.reserve(opCount);

    for (const auto op : optree) {

        opList.push_back(op.name);

        const kodachi::GroupAttribute attrs = op.attribute;
        const kodachi::StringAttribute opInputsAttr = attrs.getChildByName("opInputs");
        if (!opInputsAttr.isValid()) {
            continue;
        }

        const kodachi::StringAttribute::array_type opInputsVect =
                opInputsAttr.getNearestSample(0.0f);

        for (const char* opId : opInputsVect) {
            opsUsedAsInput.emplace(opId);
        }
    }

    // If an op does not exist in opsUsedAsInput set, then it is not
    // used as an input to any other ops, therefore it is a leaf.
    std::vector<KodachiOpId> optreeLeaves;
    for (const auto& opId : opList) {
        if (opsUsedAsInput.count(opId) == 0) {
            optreeLeaves.emplace_back(opId.data());
        }
    }

    return optreeLeaves;
}

OpTreeBuilder::Op::Ptr
OpTreeBuilder::createOp()
{
    std::lock_guard<std::mutex> lock(mMutex);

    return lockFreeCreateOp(KodachiOpId::generate());
}

OpTreeBuilder::Op::Ptr
OpTreeBuilder::lockFreeCreateOp(const KodachiOpId& id)
{
    static const kodachi::StringAttribute kNoOpAttr("no-op");

    Op::Ptr opPtr = std::make_shared<Op>(op_constructor_key{}, id);
    mKodachiOpIdToOpPtr.emplace(id, opPtr);
    lockFreeSetOpArgs(opPtr, kNoOpAttr, kodachi::GroupAttribute());

    return opPtr;
}

OpTreeBuilder::Op::Ptr
OpTreeBuilder::getOpFromOpId(const KodachiOpId& opId) const
{
    std::lock_guard<std::mutex> lock(mMutex);

    auto iter = mKodachiOpIdToOpPtr.find(opId);
    if (iter == mKodachiOpIdToOpPtr.cend()) {
        return nullptr;
    }

    return iter->second;
}

OpTreeBuilder&
OpTreeBuilder::setOpArgs(const OpTreeBuilder::Op::Ptr& op,
                         const std::string& opType,
                         const kodachi::Attribute& opArgs)
{
    std::lock_guard<std::mutex> lock(mMutex);

    return lockFreeSetOpArgs(op, kodachi::StringAttribute(opType), opArgs);
}

OpTreeBuilder&
OpTreeBuilder::lockFreeSetOpArgs(const OpTreeBuilder::Op::Ptr& op,
                          const kodachi::StringAttribute& opType,
                          const kodachi::Attribute& opArgs)
{
    const auto opIdIter = mKodachiOpIdToOpPtr.find(op->mId);
    if (opIdIter == mKodachiOpIdToOpPtr.cend()) {
        KdLogError("Failed to set op args. Op was not created using this OpTreeBuilder.");
        return *this;
    }

    mDeltaGB.set(op->mId.str() + ".opType", opType);
    mDeltaGB.set(op->mId.str() + ".opArgs", opArgs);

    return *this;
}

OpTreeBuilder&
OpTreeBuilder::setOpInputs(const OpTreeBuilder::Op::Ptr& op,
                           const std::vector<Op::Ptr>& opInputs)
{
    std::lock_guard<std::mutex> lock(mMutex);

    return lockFreeSetOpInputs(op, opInputs);
}

OpTreeBuilder&
OpTreeBuilder::lockFreeSetOpInputs(const OpTreeBuilder::Op::Ptr& op,
                                   const std::vector<Op::Ptr>& opInputs)
{
    const auto opIdIter = mKodachiOpIdToOpPtr.find(op->mId);
    if (opIdIter == mKodachiOpIdToOpPtr.cend()) {
        KdLogError("Failed to set op inputs. Op was not created using this OpTreeBuilder.");
        return *this;
    }

    std::vector<std::string> opInputVect;
    opInputVect.reserve(opInputs.size());

    // Only add ops made using this OpTreeBuilder
    for (const auto& inputOp : opInputs) {
        const auto iter = mKodachiOpIdToOpPtr.find(inputOp->mId);
        if (iter != mKodachiOpIdToOpPtr.cend()) {
            opInputVect.emplace_back(iter->first.str());
        }
        else {
            KdLogError("Skipped adding op to list of op inputs. Op was not "
                       "created using this OpTreeBuilder.");
        }
    }

    mDeltaGB.set(op->mId.str() + ".opInputs", kodachi::StringAttribute(opInputVect));

    return *this;
}

// Merges the input op-tree into the graph internally held by this OpTreeBuilder.
//
// Input must be a valid op-tree with each op already assigned a valid and
// unique ID (KodachiOpId).
//
// Goes over each entry and makes sure it is registered with this OpTreeBuilder;
// if an unregistered op is found, creates a new op and registers it with this
// OpTreeBuilder.
//
// Returns a vector<Op::Ptr> containing the pointers to all the new/modified
// ops; the last element of the returned vector is a terminal op.
//
std::vector<OpTreeBuilder::Op::Ptr>
OpTreeBuilder::merge(const kodachi::GroupAttribute& optree)
{
    if (!optree.isValid()) {
        return { };
    }

    std::lock_guard<std::mutex> lock(mMutex);

    const std::int64_t opCount = optree.getNumberOfChildren();
    std::unordered_set<KodachiOpId> opsUsedAsInput(opCount);
    std::vector<Op::Ptr> opPtrList;
    opPtrList.reserve(opCount);

    for (const auto op : optree) {
        const KodachiOpId opId(op.name.data());
        if (!opId.is_valid()) {
            KdLogError("Failed to merge; invalid op ID found in the input.");
            return { };
        }

        const auto iter = mKodachiOpIdToOpPtr.find(opId);

        // If not found, create a new op
        if (iter == mKodachiOpIdToOpPtr.cend()) {
            opPtrList.push_back(lockFreeCreateOp(opId));
        } else {
            opPtrList.push_back(iter->second);
        }

        const kodachi::GroupAttribute attrs = op.attribute;
        const kodachi::StringAttribute opInputsAttr =
                attrs.getChildByName("opInputs");
        if (!opInputsAttr.isValid()) {
            continue;
        }

        const kodachi::StringAttribute::array_type opInputsVect =
                opInputsAttr.getNearestSample(0.0f);

        for (const char* inputOpId : opInputsVect) {
            opsUsedAsInput.emplace(inputOpId);
        }
    }

    // If an op does not exist in opsUsedAsInput set, then it is not
    // used as an input to any other op and therefore it is a leaf.
    Op::Ptr terminalOpPtr;
    for (const auto& opPtr : opPtrList) {
        if (opsUsedAsInput.count(opPtr->mId) == 0) {
            terminalOpPtr = opPtr;
            break;
        }
    }

    // We must find at least one terminal op, otherwise the input is not
    // a tree and has a cycle.
    if (terminalOpPtr == nullptr) {
        KdLogError("Input is not a valid tree (possibly a graph with a "
                   "directed cycle sub-graph).");
        return { };
    }

    // Move the terminal op to the end of opList
    if (opPtrList.back() != terminalOpPtr) {
        opPtrList.erase(std::find(opPtrList.begin(), opPtrList.end(), terminalOpPtr));
        opPtrList.push_back(terminalOpPtr);
    }

    // Merge!
    mDeltaGB.update(optree);

    return opPtrList;
}

// Appends op2 to op1, returns a pointer to op2
OpTreeBuilder::Op::Ptr
OpTreeBuilder::appendOp(const OpTreeBuilder::Op::Ptr& op1, const OpTreeBuilder::Op::Ptr& op2)
{
    if (op1 == nullptr || op2 == nullptr) {
        KdLogError("NULL passed to appendOp(const Op::Ptr&, const Op::Ptr&).");
        return { };
    }

    std::lock_guard<std::mutex> lock(mMutex);

    // Add current terminal op as an input to the new op
    lockFreeSetOpInputs(op2, std::vector<Op::Ptr>{ op1 });
    return op2;
}

// Creates new ops from the op chain and appends then to the specified op
std::vector<OpTreeBuilder::Op::Ptr>
OpTreeBuilder::appendOpChain(const OpTreeBuilder::Op::Ptr& op,
                             const kodachi::GroupAttribute& opChain)
{
    if (!opChain.isValid()) {
        KdLogError("Invalid op chain.");
        return { };
    }

    if (op == nullptr) {
        KdLogError("NULL passed to appendOpChain(const Op::Ptr&, const kodachi::GroupAttribute&).");
        return { };
    }

    std::lock_guard<std::mutex> lock(mMutex);

    std::vector<Op::Ptr> opPtrList;
    opPtrList.reserve(opChain.getNumberOfChildren() + 1); // ops in the opChain + current terminal op

    Op::Ptr currentTerminalOpPtr = op;
    opPtrList.push_back(currentTerminalOpPtr);

    for (const auto op : opChain) {
        const kodachi::GroupAttribute attrs = op.attribute;
        const kodachi::StringAttribute opTypeAttr = attrs.getChildByName("opType");
        const kodachi::Attribute opArgsAttr = attrs.getChildByName("opArgs");

        Op::Ptr currentOpPtr = lockFreeCreateOp(KodachiOpId::generate());

        lockFreeSetOpArgs(currentOpPtr, opTypeAttr, opArgsAttr);
        lockFreeSetOpInputs(currentOpPtr, { currentTerminalOpPtr });

        currentTerminalOpPtr = currentOpPtr;

        opPtrList.push_back(currentTerminalOpPtr);
    }

    return opPtrList;
}

kodachi::GroupAttribute
OpTreeBuilder::buildDelta(BuildMode mode)
{
    std::lock_guard<std::mutex> lock(mMutex);

    switch(mode)
    {
        case BuildMode::FLUSH:
        {
            mMergedGB.reset();
            return mDeltaGB.build(kodachi::GroupBuilder::BuildAndFlush);
        }
        case BuildMode::RETAIN:
        {
            kodachi::GroupAttribute delta =
                    mDeltaGB.build(kodachi::GroupBuilder::BuildAndFlush);
            mMergedGB.deepUpdate(delta);
            return delta;
        }
        default:
        {
            KdLogError("Invalid build mode " << static_cast<int>(mode) << ".");
            return { };
        }
    }

    return { };
}

kodachi::GroupAttribute
OpTreeBuilder::build(const OpTreeBuilder::Op::Ptr& terminalOp, BuildMode mode)
{
    std::lock_guard<std::mutex> lock(mMutex);

    if (terminalOp == nullptr) {
        KdLogError("Failed to build the op tree; a valid terminal op needed.");
        return { };
    }

    const auto iter = mKodachiOpIdToOpPtr.find(terminalOp->mId);
    if (iter == mKodachiOpIdToOpPtr.cend()) {
        KdLogError("Failed to build the op tree; this terminal op was not built "
                   "with this op tree builder.");
        return { };
    }

    kodachi::GroupAttribute graph;
    switch(mode)
    {
        case BuildMode::FLUSH:
        {
            mMergedGB.deepUpdate(mDeltaGB.build(kodachi::GroupBuilder::BuildAndFlush));
            graph = mMergedGB.build(kodachi::GroupBuilder::BuildAndFlush);
            break;
        }
        case BuildMode::RETAIN:
        {
            mMergedGB.deepUpdate(mDeltaGB.build(kodachi::GroupBuilder::BuildAndFlush));
            graph = mMergedGB.build(kodachi::GroupBuilder::BuildAndRetain);
            break;
        }
        default:
        {
            KdLogError("Invalid build mode " << static_cast<int>(mode) << ".");
            return { };
        }
    }

    // Extract the sub-tree rooted at the terminal op (traverse through opInputs)
    const KodachiOpId& terminalOpId = terminalOp->mId;
    kodachi::GroupAttribute terminalOpAttrs = graph.getChildByName(terminalOpId.str());
    if (!terminalOpAttrs.isValid()) {
        KdLogError("Failed to build the op tree; terminal op not found in the graph.");
        return { };
    }

    std::stack<const char*> opStack;
    std::queue<const char*> opInputQueue;
    opInputQueue.push(terminalOpId.str().c_str());
    while (!opInputQueue.empty()) {
        const char* opName = opInputQueue.front();

        kodachi::GroupAttribute opAttr = graph.getChildByName(opName);
        if (!opAttr.isValid()) {
            KdLogError("Failed to build the op tree; op not found in the graph.");
            return { };
        }

        // Add to stack
        opStack.push(opName);

        // Done with opName, safe to remove from the queue
        opInputQueue.pop();

        // Read op inputs, add to the queue
        const kodachi::StringAttribute opInputsAttr = opAttr.getChildByName("opInputs");
        if (!opInputsAttr.isValid()) {
            continue;
        }

        const kodachi::StringAttribute::array_type opInputsArray =
                opInputsAttr.getNearestSample(0.0f);
        for (const char* input : opInputsArray) {
            opInputQueue.push(input);
        }
    }

    kodachi::GroupBuilder treeGB;
    while (!opStack.empty()) {
        const char* opName = opStack.top();
        opStack.pop();

        kodachi::Attribute opAttr = graph.getChildByName(opName);
        if (!opAttr.isValid()) {
            KdLogError("Failed to build the op tree; op not found in the graph.");
            return { };
        }

        treeGB.set(opName, opAttr);
    }

    return treeGB.build();
}

KdPluginStatus
OpTreeBuilder::setHost(KdPluginHost* host)
{
    kodachi::KodachiLogging::setHost(host);
    kodachi::Attribute::setHost(host);

    return kodachi::GroupBuilder::setHost(host);
}

//-------------------------------------------------

} // namespace kodachi

