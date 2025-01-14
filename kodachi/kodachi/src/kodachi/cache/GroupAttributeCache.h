// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0


#pragma once

// Kodachi
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/binary_conversion/BinaryConversion.h>
#include <kodachi/cache/KodachiCache.h>
#include <kodachi/cache/CacheUtils.h>
#include <kodachi/op/Op.h>

// C++
#include <stack>

namespace kodachi
{
    namespace GroupAttributeCacheUtils
    {
        inline std::size_t
        approximateSize(const kodachi::GroupAttribute& group)
        {
            if (!group.isValid()) {
                return 0;
            }

            std::size_t size = 0;

            std::stack <kodachi::GroupAttribute> s;
            s.push(group);

            while (!s.empty()) {
                kodachi::GroupAttribute current = s.top();
                s.pop();

                for (std::int64_t idx = 0; idx < current.getNumberOfChildren(); ++idx) {
                    kodachi::Attribute child = current.getChildByIndex(idx);
                    const FnKatAttributeType childType = child.getType();
                    if (childType == kFnKatAttributeTypeGroup) {
                        s.push(child);
                    }
                    else {
                        const kodachi::DataAttribute childAttr(child);
                        if (!childAttr.isValid()) {
                            continue;
                        }

                        const std::size_t sampleCount = childAttr.getNumberOfTimeSamples();
                        size += sampleCount * sizeof(float);

                        const std::size_t valuesPerSample = childAttr.getNumberOfValues();
                        const std::size_t totalValueCount = sampleCount * valuesPerSample;

                        if (childType == kFnKatAttributeTypeFloat) {
                            size += totalValueCount * sizeof(kodachi::FloatAttribute::value_type);
                        }
                        else if (childType == kFnKatAttributeTypeDouble) {
                            size += totalValueCount * sizeof(kodachi::DoubleAttribute::value_type);
                        }
                        else if (childType == kFnKatAttributeTypeInt) {
                            size += totalValueCount * sizeof(kodachi::IntAttribute::value_type);
                        }
                        else if (childType == kFnKatAttributeTypeString) {
                            size += totalValueCount
                                    *
                                    // approximate character count per std::string
                                    (sizeof(kodachi::StringAttribute::value_type) + 32UL);
                        }
                    }
                }
            }

            return size;
        }

        inline std::uint64_t KeyHash(const kodachi::GroupAttribute& key)
        {
            return key.getHash().uint64();
        }

        inline bool isValid(const kodachi::GroupAttribute& val)
        {
            return val.isValid();
        }
    } // namespace GroupAttributeCacheUtils

    template <kodachi::GroupAttribute (* CreateValueFunc) (const kodachi::GroupAttribute&, kodachi::GroupAttribute*)>
    using GroupAttributeCache =
            kodachi::KodachiCache<kodachi::GroupAttribute,
                                  kodachi::GroupAttribute,
                                  kodachi::GroupAttribute,
                                  GroupAttributeCacheUtils::KeyHash,
                                  CreateValueFunc,
                                  GroupAttributeCacheUtils::isValid,
                                  kodachi::readFromBinary_directDiskRead,
                                  kodachi::convertToBinary_directDiskWrite,
                                  GroupAttributeCacheUtils::approximateSize>;

    template <kodachi::GroupAttribute (* CreateValueFunc) (const kodachi::GroupAttribute&, kodachi::GroupAttribute*)>
    inline typename GroupAttributeCache<CreateValueFunc>::Ptr_t
    getGroupAttributeCacheInstance(const kodachi::GroupAttribute& settings, const std::string& scope)
    {
        // NOTE: this is thread-safe as of C++11 through compiler support
        // for "Dynamic Initialization and Destruction with Concurrency" (N2660):
        // http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2008/n2660.htm
        // (implemented in ICC 14.0+ and GCC 4.3+)
        //
        // "If control enters the declaration concurrently while
        //  the variable is being initialized, the concurrent
        //  execution shall wait for completion of the initialization."
        //
        static auto instance =
                kodachi::GroupAttributeCache<CreateValueFunc>::createCache(settings, scope);

        return instance;
    }

} // namespace kodachi

