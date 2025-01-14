// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <scene_rdl2/scene/rdl2/Geometry.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>

// tbb
#include <unordered_map>
#include <functional>
#include <string>

namespace kodachi_moonray {

class IdPassManager
{
public:

    void enable(const kodachi::StringAttribute& idAttrNameAttr,
                const kodachi::StringAttribute& passNameAttr);
    bool isEnabled() const { return mEnabled; }

    const std::string& getIdAttrName() const { return mIdAttrName; }
    const std::string& getBufferName() const { return mBufferName; }

    /**
     * Creates an ID for the geo/part pair and sends over the IdSender.
     * Does nothing if IdPassManager is not initialized
     */
    void registerGeometry(const kodachi::StringAttribute& locationAttr,
                          const kodachi::IntAttribute& idAttr,
                          const arras::rdl2::Geometry* geo,
                          const std::string& partName);

    /**
     * Returns the registered ID for the geo/part pair.
     * Returns 0 if not found or not initialized.
     */
    uint64_t getGeometryId(const arras::rdl2::Geometry* geo,
                           const std::string& part) const;

    kodachi::GroupAttribute getIdRegistrations();

private:
    std::string mIdAttrName;
    std::string mBufferName;
    bool mEnabled = false;

    using GeometryPartPair = std::pair<const arras::rdl2::Geometry*, std::string>;
    struct GeometryPartHash
    {
        std::size_t operator()(const GeometryPartPair& p) const
        {
            return std::hash<GeometryPartPair::first_type>()(p.first) ^
                   std::hash<GeometryPartPair::second_type>()(p.second);
        }
    };

    using IDHashMap = std::unordered_map<GeometryPartPair, uint64_t, GeometryPartHash>;
    IDHashMap mIdMap;
    kodachi::GroupBuilder mIdRegistrationBuilder;
};

} // namespace kodachi_moonray

