// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <FnAttribute/FnAttribute.h>
#include <FnAttribute/FnAttributeUtils.h>
#include <FnAttribute/FnGroupBuilder.h>
#include <FnPluginSystem/FnPlugin.h>
#include <FnAttributeFunction/plugin/FnAttributeFunctionPlugin.h>
#include <FnGeolib/util/Path.h>
#include <FnLogging/FnLogging.h>
#include <pystring/pystring.h>

#include <map>
#include <cstdio>
#include <memory>
#include <iostream>
#include <algorithm>
#include <mutex>

#include <kodachi_moonray/rdl_util/RDLObjectCache.h>

FnLogSetup("MoonrayAttributeAliasResolve");

namespace //anonymous
{

typedef std::map<std::string, std::string> GroupAttrMap;

static std::vector<std::string> katTypes({kFnRendererObjectTypeShader});

typedef std::pair<std::string, std::string> ClassAliasPair;

class AliasCache
{
public:
    std::string get(const ClassAliasPair& pair) const
    {
        auto iter = mCache.find(pair);
        if (iter != mCache.end()) {
            return iter->second;
        }
        return "";
    }

    void store(const ClassAliasPair& pair, std::string alias)
    {
        std::lock_guard<std::mutex> lock(mCacheMutex);
        mCache[pair] = alias;
    }
private:
    std::mutex mCacheMutex;
    std::map<ClassAliasPair, std::string> mCache;
};

static AliasCache aliasCache;

const std::string resolveAlias(const std::string& className,
                               const std::string& alias)
{
    // Lets memoize this function
    const auto& key = ClassAliasPair({className, alias});
    const auto& r = aliasCache.get(key);
    if (r.empty()) {
        auto cache = kodachi_moonray::RDLObjectCache::get();
        for (const auto& katType : katTypes) {
            const auto& objectMap = cache->getRenderObjects(katType);
            auto rdlObject = objectMap.find(className);
            if (rdlObject != objectMap.end()) {
                for (const auto& param : (*rdlObject).second->mParams) {
                    if (param.mName == alias) {
                        return alias;
                    }
                    if (std::find(param.mAliases.begin(),
                                  param.mAliases.end(),
                                  alias) != param.mAliases.end()) {
                        FnLogInfo("Replacing Moonray attr alias '" << alias <<
                                  "' with '" << param.mName << "' for SceneObject: " << className);
                        aliasCache.store(key, param.mName);
                        return param.mName;
                    }
                }
            }
        }
        return alias;
    } else {
        return r;
    }
}

std::pair<bool, FnAttribute::Attribute> recursivelyRename(const FnAttribute::Attribute& inputAttr,
                                                          const std::string& className)
{
    if (inputAttr.getType() == kFnKatAttributeTypeGroup) {
        FnAttribute::GroupAttribute grp = inputAttr;

        // we may need this
        std::unique_ptr<FnAttribute::GroupBuilder> gb;

        std::vector<std::string> finalAttrNames;
        finalAttrNames.resize(grp.getNumberOfChildren());

        for (int64_t i = 0, e = grp.getNumberOfChildren(); i < e; ++i) {
            FnAttribute::Attribute childAttr = grp.getChildByIndex(i);
            auto result = recursivelyRename(childAttr, className);
            FnAttribute::Attribute newAttr = result.second;
            const auto unresolvedAttrName = grp.getChildName(i);
            const auto resolvedAttrName = resolveAlias(className,
                                                       unresolvedAttrName);

            if (result.first || unresolvedAttrName != resolvedAttrName) {
                finalAttrNames[i] = resolvedAttrName;
                if (!gb) {
                    gb.reset(new FnAttribute::GroupBuilder(FnAttribute::GroupBuilder::BuilderModeStrict));

                    // set everything up to me
                    for (size_t j = 0; j < i; ++j) {
                        gb->set(finalAttrNames[j], grp.getChildByIndex(j));
                    }
                }
            } else {
                finalAttrNames[i] = unresolvedAttrName;
            }

            // if we're building a new group, always set it
            if (gb) {
                gb->set(finalAttrNames[i], newAttr);
            }
        }

        if (gb) {
            return {true, gb->build()};
        } else {
            return {false, inputAttr};
        }
    } else {
        return {false, inputAttr};
    }
}

GroupAttrMap groupAttrToMap(const FnAttribute::GroupAttribute& inputAttr)
{
    GroupAttrMap attrMap;
    for (auto i = 0; i < inputAttr.getNumberOfChildren(); ++i) {
        auto childAttr = inputAttr.getChildByIndex(i);
        if (childAttr.getType()
                == FnAttribute::StringAttribute::getKatAttributeType()) {
            attrMap[inputAttr.getChildName(i)] =
                    FnAttribute::StringAttribute(childAttr).getValue("", false);
        }
    }
    return attrMap;
}

} //anonymous

//------------------------------------------------
//
//------------------------------------------------

class MoonrayAttributeAliasResolveFunc : public Foundry::Katana::AttributeFunction
{
public:
    static FnAttribute::Attribute run(FnAttribute::Attribute attribute)
    {
        // Are we a group attr?
        FnAttribute::GroupAttribute rootAttr = attribute;
        if (rootAttr.isValid()) {
            // First look for any variables to search for
            auto classAttr =
                    FnAttribute::StringAttribute(rootAttr.getChildByName("class"));
            auto inputAttr =
                    FnAttribute::GroupAttribute(rootAttr.getChildByName("input"));
            if (classAttr.isValid()) {
                if (inputAttr.isValid()) {
                    return recursivelyRename(inputAttr, classAttr.getValue()).second;
                } else {
                    FnLogWarn("Invalid 'input' child attribute!");
                }
            } else {
                FnLogWarn("Invalid 'class' child attribute!");
            }
        } else {
            FnLogWarn("Invalid input attribute!");
        }
        return attribute;
    }

    static FnPlugStatus setHost(FnPluginHost *host)
    {
        FnKat::FnLogging::setHost(host);
        return FNATTRIBUTEFUNCTION_NAMESPACE::AttributeFunction::setHost(host);
    }
};

//------------------------------------------------
//
//------------------------------------------------

DEFINE_ATTRIBUTEFUNCTION_PLUGIN(MoonrayAttributeAliasResolveFunc)

//------------------------------------------------
//
//------------------------------------------------

void registerPlugins()
{
    REGISTER_PLUGIN(MoonrayAttributeAliasResolveFunc,
                    "MoonrayAttributeAliasResolve",
                    0,
                    1);
}

