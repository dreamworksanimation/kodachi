// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Kodachi
#include <kodachi/KodachiOpId.h>
#include <kodachi/StringView.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/plugin_system/PluginManager.h>

// C++
#include <map>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace kodachi
{

//------------------------------------------------------------------------------

class OpTreeBuilder
{
private:
    struct op_constructor_key;

public:
    using Ptr = std::shared_ptr<OpTreeBuilder>;

    enum class BuildMode : int { FLUSH, RETAIN };

    class Op
    {
    public:
        using Ptr = std::shared_ptr<Op>;

        Op(const op_constructor_key&,
           const KodachiOpId& id);

        Op(const Op&) = default;
        Op& operator=(const Op&) = default;

        bool operator==(const Op& rhs) const { return mId == rhs.mId; }
        bool operator!=(const Op& rhs) const { return mId != rhs.mId; }
        bool operator<(const Op& rhs) const { return mId < rhs.mId; }

        const KodachiOpId mId;

        friend class OpTreeBuilder;
    };

    OpTreeBuilder() = default;

    static std::vector<KodachiOpId> findTerminalOps(const kodachi::GroupAttribute& optree);

    Op::Ptr createOp();

    bool contains(const Op::Ptr& op) const
    {
        std::lock_guard<std::mutex> lock(mMutex);
        return mKodachiOpIdToOpPtr.count(op->mId) > 0;
    }

    Op::Ptr getOpFromOpId(const KodachiOpId& opId) const;

    OpTreeBuilder& setOpArgs(const Op::Ptr& op,
                             const std::string& opType,
                             const kodachi::Attribute& opArgs);

    OpTreeBuilder& setOpInputs(const Op::Ptr& op, const std::vector<Op::Ptr>& opInputs);

    std::vector<Op::Ptr> merge(const kodachi::GroupAttribute& optree);

    Op::Ptr appendOp(const Op::Ptr& op1, const Op::Ptr& op2);
    std::vector<Op::Ptr> appendOpChain(const Op::Ptr& op, const kodachi::GroupAttribute& opChain);

    kodachi::GroupAttribute buildDelta(BuildMode mode = BuildMode::FLUSH);
    kodachi::GroupAttribute build(const Op::Ptr& terminalOp, BuildMode mode = BuildMode::FLUSH);

    static KdPluginStatus setHost(KdPluginHost* host);

private:
    OpTreeBuilder& lockFreeSetOpArgs(const Op::Ptr& op,
                                     const kodachi::StringAttribute& opType,
                                     const kodachi::Attribute& opArgs);

    OpTreeBuilder& lockFreeSetOpInputs(const Op::Ptr& op,
                                       const std::vector<Op::Ptr>& opInputs);

    Op::Ptr lockFreeCreateOp(const KodachiOpId& id);

    mutable std::mutex mMutex;

    kodachi::GroupBuilder mMergedGB;
    kodachi::GroupBuilder mDeltaGB;

    // Map KodachiOpId (string) to Op objects (shared_ptr<Op>)
    std::unordered_map<KodachiOpId, Op::Ptr> mKodachiOpIdToOpPtr;
};

} // namespace kodachi

