// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/attribute_function/AttributeFunctionPlugin.h>

#include <boost/thread/shared_mutex.hpp>
#include <unordered_map>

// Used to change state of a KPOP-based render without modifying the optree
// Use of stateKey allows for multiple kinds of state per KPOP render, or multiple
// renders.
namespace {

std::unordered_map<kodachi::Attribute, kodachi::GroupAttribute, kodachi::AttributeHash> sKPOPStateMap;
boost::shared_mutex sMutex;

class GetKPOPStateAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute key)
    {
        boost::shared_lock<boost::shared_mutex> lock(sMutex);
        const auto iter = sKPOPStateMap.find(key);
        if (iter != sKPOPStateMap.end()) {
            return iter->second;
        }

        return {};
    }
};

class SetKPOPStateAttrFunc : public kodachi::AttributeFunction
{
public:
    static kodachi::Attribute
    run(kodachi::Attribute attribute)
    {
        const kodachi::GroupAttribute keyValueAttr(attribute);

        const kodachi::Attribute keyAttr = keyValueAttr.getChildByName("key");
        const kodachi::GroupAttribute valuesAttr = keyValueAttr.getChildByName("values");

        if (keyAttr.isValid() && valuesAttr.isValid()) {
            boost::lock_guard<boost::shared_mutex> lock(sMutex);
            auto emplacePair = sKPOPStateMap.emplace(keyAttr, valuesAttr);
            if (!emplacePair.second) {
                kodachi::GroupBuilder gb;
                gb.update(emplacePair.first->second);
                gb.deepUpdate(valuesAttr);
                emplacePair.first->second = gb.build();
            }
        }

        return {};
    }

    static void
    flush()
    {
        boost::lock_guard<boost::shared_mutex> lock(sMutex);
        sKPOPStateMap.clear();
    }
};

DEFINE_ATTRIBUTEFUNCTION_PLUGIN(GetKPOPStateAttrFunc)
DEFINE_ATTRIBUTEFUNCTION_PLUGIN(SetKPOPStateAttrFunc)

}

void registerPlugins()
{
    REGISTER_PLUGIN(GetKPOPStateAttrFunc, "GetKPOPState", 0, 1);
    REGISTER_PLUGIN(SetKPOPStateAttrFunc, "SetKPOPState", 0, 1);
}

