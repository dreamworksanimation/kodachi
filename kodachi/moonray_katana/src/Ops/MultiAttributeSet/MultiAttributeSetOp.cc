// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include <kodachi/attribute/ZeroCopyAttribute.h>

#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi/op/CookInterfaceUtils.h>
#include <kodachi/op/Op.h>
#include <kodachi/op/XFormUtil.h>

#include <kodachi/logging/KodachiLogging.h>

// std
#include <unordered_map>

// third-party includes
#include <json/reader.h>
#include <json/value.h>

namespace
{

KdLogSetup("MultiAttributeSetOp");

template<typename ATTR_T>
typename ATTR_T::value_type
extractValue(const Json::Value& value)
{
    // only allow the kodachi::DataAttribute types
    // we don't want to default to something since trying to
    // get a Json value from an inconvertible type will generate
    // an assertion
    ATTR_T::type_not_allowed;
}

template<>
kodachi::FloatAttribute::value_type
extractValue<kodachi::FloatAttribute>(const Json::Value& value)
{
    return value.asFloat();
}

template<>
kodachi::IntAttribute::value_type
extractValue<kodachi::IntAttribute>(const Json::Value& value)
{
    return value.asInt();
}

template<>
kodachi::DoubleAttribute::value_type
extractValue<kodachi::DoubleAttribute>(const Json::Value& value)
{
    return value.asDouble();
}

template<>
kodachi::StringAttribute::value_type
extractValue<kodachi::StringAttribute>(const Json::Value& value)
{
    return value.asCString();
}

template<class ATTR_T>
ATTR_T
getAttrFromJson(const Json::Value& value, Json::ValueType type)
{
    using value_t = typename ATTR_T::value_type;
    if (value.isArray()) {
        std::vector<value_t> out;
        for (Json::ArrayIndex i = 0; i < value.size(); ++i) {
            if (value[i].isConvertibleTo(type)) {
                out.emplace_back(extractValue<ATTR_T>(value[i]));
            }
        }
        return kodachi::ZeroCopyAttribute<ATTR_T>::create(out, 1);
    }

    if (value.isConvertibleTo(type)) {
        return ATTR_T(extractValue<ATTR_T>(value));
    }

    return ATTR_T();
}

const std::string sEnable("enable");
const std::string sType  ("type");
const std::string sValue ("value");

const std::string sFloat ("FloatAttr");
const std::string sInt   ("IntAttr");
const std::string sDouble("DoubleAttr");
const std::string sString("StringAttr");

struct XformData {
public:
    std::unordered_map<std::string, std::array<double, 3>> mXformMap;

    // eg. for xform.interactive.translate.x, strips out
    // the x and inserts into the array based on x,y,z
    bool
    insert(const kodachi::string_view attr,
           const Json::Value& value)
    {
        // first validate the value
        if (!value.isConvertibleTo(Json::ValueType::realValue)) {
            KdLogDebug("Attempting to set non numeric xform data: " << attr);
            return false;
        }

        const auto pos = attr.rfind(".");
        if (pos != attr.npos) {
            // name of the attribute
            const std::string name(attr.substr(0, pos));
            if (mXformMap.find(name) == mXformMap.end()) {
                insertInit(name);
            }

            std::array<double, 3>& array = mXformMap[name];

            // expected to be x, y, or z
            const kodachi::string_view index = attr.substr(pos + 1);
            if (index == "x") {
                array[0] = value.asDouble();
            } else if (index == "y") {
                array[1] = value.asDouble();
            } else if (index == "z") {
                array[2] = value.asDouble();
            } else {
                KdLogDebug("Unexpected param while setting xform data: " << attr << ", " << index);
                return false;
            }
            return true;
        }

        KdLogDebug("Unexpected xform attribute name: " << attr);
        return false;
    }

    // builds an xform group attribute out of the current xform data
    kodachi::GroupAttribute
    build() const
    {
        static const std::string kRotate("rotate");

        kodachi::GroupBuilder gb;
        for (const auto& pair : mXformMap) {
            const auto& name = pair.first;

            if (name.find(kRotate) != name.npos) {
                // this is a rotate
                const auto& data = pair.second;

                // eg. for an attribute like xform.interactive.rotate
                // strip out rotate and replace with rotateZ, rotateY, and rotateX
                const std::string subStr = name.substr(0, name.rfind("."));

                static const std::string kRotateZ(".rotateZ");
                static const std::string kRotateY(".rotateY");
                static const std::string kRotateX(".rotateX");

                // rotate attributes are split into rotateX, rotateY, or rotateZ with
                // 4 values each, in the format
                // rotateZ: z 0.0 0.0 1.0
                // rotateY: y 0.0 1.0 0.0
                // rotateX: x 1.0 0.0 0.0
                std::array<double, 4> zData { data[2], 0.0, 0.0, 1.0 };
                gb.set(kodachi::concat(subStr, kRotateZ),
                       kodachi::DoubleAttribute(zData.data(), 4, 1));

                std::array<double, 4> yData { data[1], 0.0, 1.0, 0.0 };
                gb.set(kodachi::concat(subStr, kRotateY),
                       kodachi::DoubleAttribute(yData.data(), 4, 1));

                std::array<double, 4> xData { data[0], 1.0, 0.0, 0.0 };
                gb.set(kodachi::concat(subStr, kRotateX),
                       kodachi::DoubleAttribute(xData.data(), 4, 1));

            } else {
                gb.set(name, kodachi::DoubleAttribute(pair.second.data(), 3, 1));
            }
        }

        return gb.build();
    }

private:
    void
    insertInit(const kodachi::string_view name)
    {
        static const std::string kScale("scale");

        // initialize scale to 1.0, everything else (translate and rotate) to 0.0
        if (name.find(kScale) != name.npos) {
            mXformMap.insert({name.data(), {1.0, 1.0, 1.0}});
        } else {
            mXformMap.insert({name.data(), {0.0, 0.0, 0.0}});
        }
    }
};

kodachi::GroupAttribute
parseXformInputAttrs(const Json::Value& location)
{
    kodachi::GroupBuilder gb;

    // stores all the incoming xform attributes in array format
    XformData xformData;

    Json::ValueIterator attrsIt = location.begin();
    // attrs to set
    for (; attrsIt != location.end(); ++attrsIt) {

        const Json::Value attr = (*attrsIt);
        if (!attrsIt.key().isString()) {
            // attr names definitely need to be string
            KdLogWarn("Skipping invalid attribute " << attrsIt.key().toStyledString());
            continue;
        }

        if (attr.isObject()) {
            // enabled?
            const Json::Value& enabled = attr[sEnable];
            Json::Int enableValue = 1;
            if (enabled.isNull() ||
                    !enabled.isConvertibleTo(Json::ValueType::intValue)) {
                KdLogWarn("Invalid 'enabled' key for attribute " << attrsIt.key().toStyledString()
                        << ", defaulting to true.");
            } else {
                enableValue = attr[sEnable].asInt();
            }

            // if not enabled, don't bother setting the attribute
            if (enableValue) {
                // value
                const Json::Value& value = attr[sValue];
                if (value.isNull()) {
                    KdLogWarn("Invalid 'value' key for attribute " << attrsIt.key().toStyledString()
                            << ", skipping.");
                    continue;
                }

                static const std::string kXform("xform");
                const std::string attrName = attrsIt.key().asString();
                if (attrName.find(kXform) != 0) {
                    // only care about attributes beginning with xform
                    KdLogDebug("Skipping non xform attribute: " << attrName);
                    continue;
                }

                // ready to set this attribute
                if (!xformData.insert(attrName, value)) {
                    KdLogDebug("Skipping invalid xform attribute: " << attrName);
                }
            } else {
                KdLogDebug("Skipping disabled attr: " << attrsIt.key().toStyledString());
            }
        } else {
            KdLogWarn("Skipping invalid attribute " << attrsIt.key().toStyledString());
        }
    } // attrs loop

    return xformData.build();
}

kodachi::GroupAttribute
parseInputAttrs(const Json::Value& location)
{
    kodachi::GroupBuilder gb;

    Json::ValueIterator attrsIt = location.begin();
    // attrs to set
    for (; attrsIt != location.end(); ++attrsIt) {

        const Json::Value attr = (*attrsIt);
        if (!attrsIt.key().isString()) {
            // attr names definitely need to be string
            KdLogWarn("Skipping invalid attribute " << attrsIt.key().toStyledString());
            continue;
        }

        if (attr.isObject()) {
            // enabled?
            const Json::Value& enabled = attr[sEnable];
            Json::Int enableValue = 1;
            if (enabled.isNull() ||
                    !enabled.isConvertibleTo(Json::ValueType::intValue)) {
                KdLogWarn("Invalid 'enabled' key for attribute " << attrsIt.key().toStyledString()
                        << ", defaulting to true.");
            } else {
                enableValue = attr[sEnable].asInt();
            }

            // if not enabled, don't bother setting the attribute
            if (enableValue) {
                // type
                const Json::Value& type = attr[sType];
                if (type.isNull() ||
                        !type.isConvertibleTo(Json::ValueType::stringValue)) {
                    KdLogWarn("Invalid 'type' key for attribute " << attrsIt.key().toStyledString()
                            << ", skipping.");
                    continue;
                }
                const std::string typeStr = type.asString();

                // value
                const Json::Value& value = attr[sValue];
                if (value.isNull()) {
                    KdLogWarn("Invalid 'value' key for attribute " << attrsIt.key().toStyledString()
                            << ", skipping.");
                    continue;
                }

                if (type == sFloat) {
                    gb.set(attrsIt.key().asString(),
                           getAttrFromJson<kodachi::FloatAttribute>(value, Json::ValueType::realValue));
                } else if (type == sInt) {
                    gb.set(attrsIt.key().asString(),
                           getAttrFromJson<kodachi::IntAttribute>(value, Json::ValueType::intValue));
                } else if (type == sDouble) {
                    gb.set(attrsIt.key().asString(),
                           getAttrFromJson<kodachi::DoubleAttribute>(value, Json::ValueType::realValue));
                } else if (type == sString) {
                    gb.set(attrsIt.key().asString(),
                           getAttrFromJson<kodachi::StringAttribute>(value, Json::ValueType::stringValue));
                }

            } else {
                KdLogDebug("Skipping disabled attr: " << attrsIt.key().toStyledString());
            }
        } else {
            KdLogWarn("Skipping invalid attribute " << attrsIt.key().toStyledString());
        }
    } // attrs loop

    return gb.build();
}

// function defining how to retrieve attribute values from an Json input location object
// returns a group attribute of all the attributes and values
using ParseAttrsFunc = std::function<kodachi::GroupAttribute(const Json::Value&)>;

bool
parseInput(const std::string& inputStr,
           kodachi::GroupBuilder& argsGb,
           const ParseAttrsFunc& parseAttrsFunc)
{
    Json::Value root;
    Json::Reader reader;
    if (reader.parse(inputStr, root)) {

        Json::ValueIterator childIt = root.begin();
        // locations to set attrs on
        for (; childIt != root.end(); ++childIt) {

            const Json::Value child = (*childIt);
            if (!childIt.key().isString()) {
                // paths definitely need to be string
                KdLogWarn("Skipping invalid child " << childIt.key().toStyledString());
                continue;
            }

            if (child.isObject()) {
                // attrs for this location
                const kodachi::GroupAttribute attrs = parseAttrsFunc(child);
                if (attrs.isValid()) {
                    argsGb.set(childIt.key().asString(), attrs);
                }
            } else {
                KdLogWarn("Skipping invalid child " << childIt.key().toStyledString());
            }
        } // child locations loop

        return true;
    } // parse

    KdLogWarn("Error parsing Json from input attributes: " <<
                                reader.getFormattedErrorMessages());
    return false;
}

// MultiAttributeSet will run on a location and sets attributes for its children
// takes in a string in a dictionary format:
// { "child0" : { "attr0": {"value":[1, 0, 0], "type":"FloatAttr", "enable":True}, "attr1": ... },
//   "child1" : { ... },
//   ...
// }
class MultiAttributeSetOp : public kodachi::Op
{
public:
    static void setup(kodachi::OpSetupInterface &interface)
    {
        interface.setThreading(kodachi::OpSetupInterface::ThreadModeConcurrent);
    }

    static void cook(kodachi::OpCookInterface &interface)
    {
        const kodachi::StringAttribute topLocationAttr =
                interface.getOpArg("topLocation");
        if (topLocationAttr.isValid()) {
            kodachi::CookInterfaceUtils::MatchesCELInfo celInfo;
            kodachi::CookInterfaceUtils::matchesCEL(celInfo, interface, topLocationAttr);

            // first time we're running the op; need to evaluate 'input' and 'xformInput'
            if (celInfo.matches) {
                kodachi::GroupBuilder attrsGb;

                // Input attributes ------------------------------------------------------------------
                const kodachi::StringAttribute inputAttr = interface.getOpArg("input");
                std::string inputStr = inputAttr.getValue("", false);
                if (!inputStr.empty()) {

                    // replace single quotes with double quotes for Json formatting
                    std::replace(inputStr.begin(), inputStr.end(), '\'', '"');

                    kodachi::GroupBuilder inputGb;
                    if (parseInput(inputStr, inputGb, parseInputAttrs)) {
                        attrsGb.deepUpdate(inputGb.build());
                    } else {
                        kodachi::ReportWarning(interface, "Error parsing 'input'.");
                    }
                } // input attr

                // Xform attributes ------------------------------------------------------------------
                const kodachi::StringAttribute xformAttr = interface.getOpArg("xformInput");
                std::string xformStr = xformAttr.getValue("", false);
                if (!xformStr.empty()) {

                    // replace single quotes with double quotes for Json formatting
                    std::replace(xformStr.begin(), xformStr.end(), '\'', '"');

                    kodachi::GroupBuilder inputGb;
                    if (parseInput(xformStr, inputGb, parseXformInputAttrs)) {
                        attrsGb.deepUpdate(inputGb.build());
                    } else {
                        kodachi::ReportWarning(interface, "Error parsing 'xformInput'.");
                    }
                } // xform attr

                // new op args:
                // __attrs parsed from input Json format text
                interface.replaceChildTraversalOp("",
                        kodachi::GroupAttribute("__attrs", attrsGb.build(),
                                                false));

                // Marks this location as the new root location in the Op’s traversal.
                // Subsequent calls to GetRelativeInputLocationPath() at child locations
                // will return paths relative to this one.
                interface.resetRoot();
            }

            return;
        }

        // attrs from evaluating 'input' and 'xformInput'
        // children of topLocation should have this op arg
        const kodachi::GroupAttribute attrsGroup = interface.getOpArg("__attrs");
        if (!attrsGroup.isValid()) {
            return;
        }

        // set attrs for this location
        // path names are relative to the root location where this op was first ran
        // ie.
        //
        // ""
        //  |--child1
        //       |--child1/grandchild1
        //  |--child2
        //       |--child2/grandchild2

        const std::string relativePath = interface.getRelativeInputLocationPath();

        const kodachi::GroupAttribute newAttrs = attrsGroup.getChildByName(relativePath);
        if (newAttrs.isValid()) {
            for (const auto attr : newAttrs) {
                kodachi::GroupBuilder gb;
                gb.update(interface.getAttr(attr.name));
                gb.deepUpdate(attr.attribute);
                interface.setAttr(attr.name, gb.build());
            }
        }
    }

    static kodachi::GroupAttribute describe()
    {
        kodachi::OpDescriptionBuilder builder;

        const std::string opHelp    = "";
        const std::string opSummary = "";

        builder.setHelp(opHelp);
        builder.setSummary(opSummary);
        builder.setNumInputs(0);

        return builder.build();
    }

    static void flush()
    {

    }
};

DEFINE_KODACHIOP_PLUGIN(MultiAttributeSetOp)

}   // anonymous

void registerPlugins()
{
    REGISTER_PLUGIN(MultiAttributeSetOp, "MultiAttributeSetOp", 0, 1);
}


