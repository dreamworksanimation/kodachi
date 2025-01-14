// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>

#include <kodachi/attribute/Attribute.h>

namespace {
KdLogSetup("GenerateKatanaId");

// To indicate to Katana that you are using the new ID pass system and not the
// deprecated one, the ID pass requires 3 ints per pixel, where the first value
// is 0, and the second 2 contain the data for the uint64_t id.


class HashArray {
private:
    static constexpr uint32_t s_max_uint = 0x3FFFFFFF; // 0011,1111,1111,1111,1111,1111,1111,1111

public:
    HashArray(uint32_t x, uint32_t y, uint32_t z)
    {
        x &= s_max_uint; // reset two left-most bits
        std::memcpy(&m_x, &x, sizeof(uint32_t));

        y &= s_max_uint; // reset two left-most bits
        std::memcpy(&m_y, &y, sizeof(uint32_t));

        z &= s_max_uint; // reset two left-most bits
        std::memcpy(&m_z, &z, sizeof(uint32_t));
    }

    const float* data() const { return &m_x; }

    float m_x = 0.0f;
    float m_y = 0.0f;
    float m_z = 0.0f;
};

class HashUnion
{
private:
    // 1111,1111,1111,1111,1111,1111,1111,1111,1111,1111,1111,1111,1111,1111,1111,1111 = 0xFFFFFFFFFFFFFFFF
    // 0000,1111,1111,1111,1111,1111,1111,1111,1100,0000,0000,0000,0000,0000,0000,0000 = 0x0FFFFFFFC0000000
    // 0000,0000,0000,0000,0000,0000,0000,0000,0011,1111,1111,1111,1111,1111,1111,1111 = 0x000000003FFFFFFF
    static constexpr uint64_t s_x_mask = 0x0FFFFFFFC0000000;
    static constexpr uint64_t s_y_mask = 0x000000003FFFFFFF;

public:
    HashUnion(uint64_t i)
        : m_x(static_cast<uint32_t>((i & s_x_mask) >> 30))
        , m_y(static_cast<uint32_t>(i & s_y_mask))
    {
    }

    uint32_t m_x = 0;
    uint32_t m_y = 0;

    const int32_t * data() const { return reinterpret_cast<const int32_t*>(&m_x); }
};

using HashVector = std::vector<HashArray>;

static_assert(sizeof(int32_t) == sizeof(float), "Moonray only supports float AOVs so we are disguising ints as floats");
static_assert(sizeof(std::size_t) == sizeof(uint64_t), "We require std::hash to produce a 64-bit value");

kodachi::IntAttribute
hashUnionToIntAttr(const HashUnion& u)
{
    return kodachi::IntAttribute(u.data(), 2, 2);
}

void
HashVectorDeleter(void* hashArray)
{
    std::default_delete<HashVector>()(reinterpret_cast<HashVector*>(hashArray));
}

const std::string kKatanaId("katanaID");

class GenerateKatanaIdOp : public kodachi::Op
{
public:
    static void setup(Foundry::Katana::GeolibSetupInterface &interface)
    {
        interface.setThreading(Foundry::Katana::GeolibSetupInterface::ThreadModeConcurrent);
    }

    static void cook(Foundry::Katana::GeolibCookInterface &interface)
    {
        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world/geo//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isGeometry\")}");

        kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
        kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, kCELMatchAttr);

        if (!celInfo.canMatchChildren) {
            interface.stopChildTraversal();
        }

        if (!celInfo.matches) {
            return;
        }

        const kodachi::IntAttribute skipIdGenerationAttr =
                interface.getAttr("rdl2.meta.skipIDGeneration");
        if (!skipIdGenerationAttr.getValue(true, false)) {
            return;
        }

        const std::string inputLocationPath = interface.getInputLocationPath();

        const std::size_t geometryHash = std::hash<std::string>()(inputLocationPath);
        const HashUnion geometryUnion { geometryHash };
        const HashArray geometryHashArray { 0, geometryUnion.m_x, geometryUnion.m_y };

        interface.setAttr(kKatanaId, hashUnionToIntAttr(geometryUnion));

        kodachi::GroupAttribute idAttr;

        const kodachi::IntAttribute perPartIdsAttr =
                            interface.getAttr("rdl2.meta.perPartIDs");

        static const kodachi::StringAttribute kPrimitiveScopeAttr("primitive");
        static const kodachi::StringAttribute kPartScopeAttr("part");
        static const kodachi::StringAttribute kVector3InputTypeAttr("vector3");

        if (perPartIdsAttr.getValue(false, false)) {
            const kodachi::StringAttribute potentialChildrenAttr =
                    interface.getPotentialChildren();

            std::vector<kodachi::string_view> partNames;
            for (const kodachi::string_view childName : potentialChildrenAttr.getNearestSample(0.f)) {
                const kodachi::IntAttribute isPartAttr =
                        interface.getAttr("rdl2.meta.isPart", childName);
                if (isPartAttr.isValid()) {
                    partNames.push_back(childName);
                }
            }

            if (!partNames.empty()) {
                // Katana doesn't officially support part-scope primitive attributes,
                // but Moonray does and it's more memory efficient that
                // creating a face-scope attribute with the IDs repeated
                // many times.
                std::unique_ptr<HashVector> perPartHashes(new HashVector);

                // Use the geometry's ID for the "default" part
                perPartHashes->reserve(partNames.size() + 1);

                for (const auto& partName : partNames) {
                    const std::string partLocationPath =
                            kodachi::concat(inputLocationPath, "/", partName);

                    const std::size_t partHash =
                            std::hash<std::string>()(partLocationPath);

                    const HashUnion partUnion = { partHash };

                    // set the hash on the child location
                    {
                        static const kodachi::StringAttribute kAttributeSetCELAttr("//*");

                        kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
                        asb.setCEL(kAttributeSetCELAttr);
                        asb.setAttr(kKatanaId, hashUnionToIntAttr(partUnion));
                        interface.createChild(partName, "AttributeSet", asb.build());
                    }

                    perPartHashes->emplace_back(0, partUnion.m_x, partUnion.m_y);
                }

                perPartHashes->emplace_back(geometryHashArray);

                const float* data = reinterpret_cast<const float*>(perPartHashes->data());
                const kodachi::FloatAttribute valueAttr(
                        data, perPartHashes->size() * 3, 3,
                        perPartHashes.release(), HashVectorDeleter);

                idAttr = kodachi::GroupAttribute("scope", kPartScopeAttr,
                                                 "inputType", kVector3InputTypeAttr,
                                                 "value", valueAttr,
                                                 false);
            }
        }

        if (!idAttr.isValid()) {
        idAttr = kodachi::GroupAttribute("scope", kPrimitiveScopeAttr,
                                         "inputType", kVector3InputTypeAttr,
                                         "value", kodachi::FloatAttribute(geometryHashArray.data(), 3, 3),
                                         false);
        }

        interface.setAttr("geometry.arbitrary.katanaID", idAttr, false);
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_GEOLIBOP_PLUGIN(GenerateKatanaIdOp)

} // anonymous namespace

void registerPlugins()
{
    REGISTER_PLUGIN(GenerateKatanaIdOp, "GenerateKatanaId", 0, 1);
}

