// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/op/Op.h>

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/ZeroCopyAttribute.h>
#include <kodachi/logging/KodachiLogging.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/PathUtil.h>

#include <functional>
#include <set>

namespace {
KdLogSetup("MoonrayCryptomatte");

uint32_t murmur3_32(const std::string& str, uint32_t seed)
{
    const uint8_t* key = reinterpret_cast<const uint8_t*>(str.data());
    const size_t len = str.size();

    // MurmurHash
    uint32_t h = seed;
    if (len > 3) {
        const uint32_t* key_x4 = (const uint32_t*) key;
        size_t i = len >> 2;
        do {
            uint32_t k = *key_x4++;
            k *= 0xcc9e2d51;
            k = (k << 15) | (k >> 17);
            k *= 0x1b873593;
            h ^= k;
            h = (h << 13) | (h >> 19);
            h = h * 5 + 0xe6546b64;
        } while (--i);
        key = (const uint8_t*) key_x4;
    }
    if (len & 3) {
        size_t i = len & 3;
        uint32_t k = 0;
        key = &key[i - 1];
        do {
            k <<= 8;
            k |= *key--;
        } while (--i);
        k *= 0xcc9e2d51;
        k = (k << 15) | (k >> 17);
        k *= 0x1b873593;
        h ^= k;
    }
    h ^= len;
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

union HashUnion
{
    uint32_t ui32;
    float f32;
};

void
normalizeUint(uint32_t& ui32)
{
    uint32_t e = ui32 & 0x7F800000;
    if (e == 0 || e == 0x7F800000) { // denormalized, NaN, and infinity
        // use the lower 8 bits as the exponent, but must avoid 0 or ff
        // by scaling it to the 1-fe range
        ui32 ^= (((ui32&0xFF)*254)/256 + 1) << 23;
    }
}


class MoonrayCryptomatteOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        if (interface.atRoot()) {
            const kodachi::GroupAttribute cryptoAttr = interface.getAttr("cryptomatte");
            if (cryptoAttr.isValid()) {
                interface.replaceChildTraversalOp("", cryptoAttr);
            } else {
                interface.stopChildTraversal();
            }
        }

        static const std::string kCryptoObjectId    { "cryptomatte_object_id" };
        static const std::string kCryptoMaterialId  { "cryptomatte_material_id" };

        static const kodachi::StringAttribute kCELMatchAttr(
                "/root/world//*{@type==\"rdl2\" and hasattr(\"rdl2.meta.isGeometry\")}");

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

        static const kodachi::StringAttribute kPrimitiveScopeAttr("primitive");
        static const kodachi::StringAttribute kPartScopeAttr("part");

        const kodachi::IntAttribute cryptoObjectAttr =
                interface.getOpArg("cryptomatte_object_id");

        if (cryptoObjectAttr.getValue(0, false) == 1) {
            // object-based cryptomatte id

            // use the hash of the input scene graph location
            // so it works with instances
            const std::string location = interface.getInputLocationPath();
            HashUnion hash;

            hash.ui32 = murmur3_32(location, 0);
            normalizeUint(hash.ui32);

            kodachi::FloatAttribute hashAttr(hash.f32);

            // Also set a top level attr so renderer procedurals can have
            // cryptomatte ids too
            interface.setAttr("kodachi.cryptomatte.cryptomatte_object_id",
                              hashAttr, false);

            kodachi::GroupAttribute idAttr;

            const kodachi::IntAttribute perPartIdsAttr =
                                interface.getAttr("rdl2.meta.perPartIDs");

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
                    kodachi::FloatVector perPartHashes;

                    // Use the geometry's ID for the "default" part
                    perPartHashes.reserve(partNames.size() + 1);

                    for (const auto& partName : partNames) {
                        const std::string partLocationPath =
                                kodachi::concat(location, "/", partName);

                        HashUnion partHash;
                        partHash.ui32 = murmur3_32(partLocationPath, 0);
                        normalizeUint(partHash.ui32);

                        // set the hash on the child location
                        {
                            static const kodachi::StringAttribute kAttributeSetCELAttr("//*");

                            kodachi::op_args_builder::AttributeSetOpArgsBuilder asb;
                            asb.setCEL(kAttributeSetCELAttr);
                            asb.setAttr("kodachi.cryptomatte.cryptomatte_object_id",
                                    kodachi::FloatAttribute(partHash.f32));
                            interface.createChild(partName, "AttributeSet", asb.build());
                        }

                        perPartHashes.push_back(partHash.f32);
                    }

                    perPartHashes.push_back(hash.f32);

                    const kodachi::FloatAttribute valueAttr =
                            kodachi::ZeroCopyFloatAttribute::create(std::move(perPartHashes));

                    idAttr = kodachi::GroupAttribute("scope", kPartScopeAttr,
                                                     "value", valueAttr,
                                                     false);
                }
            }

            if (!idAttr.isValid()) {
                idAttr = kodachi::GroupAttribute("scope", kPrimitiveScopeAttr,
                                                 "value", hashAttr,
                                                 false);
            }

            interface.setAttr("geometry.arbitrary.cryptomatte_object_id",
                              idAttr, false);
        }

        // TODO: Moonray does not currently support material cryptomatte
//        const kodachi::IntAttribute cryptoMaterialAttr =
//                interface.getOpArg("cryptomatte_material_id");
//
//        if (cryptoMaterialAttr.getValue(0, false) == 1) {
//            // material-based cryptomatte id
//
//            // use the hash of the de-duplicated rdl2.layerAssign.material
//            // which is generated in KPOPMaterial
//            const kodachi::StringAttribute materialAttr =
//                    interface.getAttr("rdl2.layerAssign.material");
//            if (materialAttr.isValid()) {
//                const std::uint64_t hash = materialAttr.getHash().uint64();
//                kodachi::FloatAttribute hashAttr(static_cast<uint32_t>(hash));
//
//                kodachi::GroupAttribute materialIdAttr("value", hashAttr,
//                                                       "scope", kScopeAttr,
//                                                       false);
//                interface.setAttr("kodachi.cryptomatte.cryptomatte_material_id", materialIdAttr, false);
//                interface.setAttr("geometry.arbitrary.cryptomatte_material_id", materialIdAttr, false);
//            }
//        }
    }

    static kodachi::GroupAttribute describe()
    {
        using namespace FnKat::FnOpDescription;

        FnOpDescriptionBuilder builder;

        builder.setSummary("");
        builder.setHelp("");
        builder.setNumInputs(0);

        return builder.build();
    }
};


DEFINE_GEOLIBOP_PLUGIN(MoonrayCryptomatteOp)

} // anonymous namespace


void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayCryptomatteOp, "MoonrayCryptomatte", 0, 1);
}

