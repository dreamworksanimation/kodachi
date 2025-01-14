// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

// self
#include "IdPassManager.h"

// kodachi
#include <kodachi/logging/KodachiLogging.h>

namespace {
KdLogSetup("IdPassManager");
}

namespace kodachi_moonray {

void
IdPassManager::enable(const kodachi::StringAttribute& idAttrNameAttr,
                      const kodachi::StringAttribute& bufferNameAttr)
{
    mIdAttrName = idAttrNameAttr.getValue();
    mBufferName = bufferNameAttr.getValue();
    mEnabled = true;
}

void
IdPassManager::registerGeometry(const kodachi::StringAttribute& locationAttr,
                                const kodachi::IntAttribute& idAttr,
                                const arras::rdl2::Geometry* geo,
                                const std::string& partName)
{
    const kodachi::string_view location = locationAttr.getValueCStr();

    if (!idAttr.isValid()) {
        KdLogWarn(location << ": Invalid IDAttr");
        return;
    }

    if (idAttr.getNumberOfValues() != 2) {
        KdLogWarn(location << ": Expect IDAttr to have 2 int values");
        return;
    }

    KdLogDebug("RegisterGeometry: " << location);

    union HashUnion {
        uint64_t u64;
        int32_t i32[2];
    } u;

    const auto sample = idAttr.getNearestSample(0.f);
    u.i32[0] = sample[0];
    u.i32[1] = sample[1];

    const auto emplacePair = mIdMap.emplace(std::make_pair(geo, partName), u.u64);
    if (emplacePair.second) {
        mIdRegistrationBuilder.set(location, idAttr);
    } else {
        const uint64_t currentId = emplacePair.first->second;
        if (currentId != u.u64) {
            emplacePair.first->second = u.u64;
            mIdRegistrationBuilder.set(location, idAttr);
        }
    }
}

uint64_t
IdPassManager::getGeometryId(const arras::rdl2::Geometry* geo,
                             const std::string& part) const
{
    if (mEnabled) {
        const auto iter = mIdMap.find({geo, part});
        if (iter != mIdMap.end()) {
            return iter->second;
        }
    }

    return 0ul;
}

kodachi::GroupAttribute
IdPassManager::getIdRegistrations()
{
    kodachi::GroupAttribute idRegistrationsAttr = mIdRegistrationBuilder.build();
    if (idRegistrationsAttr.getNumberOfChildren() > 0) {
        return idRegistrationsAttr;
    }

    return kodachi::GroupAttribute{};
}

} // namespace kodachi_moonray

