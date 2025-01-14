// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#include "MoonrayRendererInfo.h"

#include <FnLogging/FnLogging.h>
#include <FnRendererInfo/plugin/RenderMethod.h>

#include <kodachi/OpTreeUtil.h>
#include <kodachi/attribute/Attribute.h>
#include <kodachi/attribute/GroupBuilder.h>
#include <kodachi/backend/BackendClient.h>
#include <kodachi/op/BuiltInOpArgsUtil.h>
#include <kodachi_moonray/rdl_util/RDLObjectCache.h>

// system includes
#include <algorithm>

using RDLObjectCache = kodachi_moonray::RDLObjectCache;

namespace
{

const std::map<std::string, std::string> kCustomArrayTypes
{
    {"LightFilterArray", "lightfilter"}
};

const int kNumArrayInputs = 100;

// This can be used to populate a dropdown menu on output channels for the user
// to add new attributes that aren't provided in the UI by default.
// You must also update buildOutputChannel() to provide the appropriate default
// values and hints for the custom channel (isCustom flag will be true).
const std::vector<std::string> kCustomChannelParams = {};

template <typename T>
bool find(const std::vector<T>& values, const T& value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

FnLogSetup("MoonrayRendererInfo");

} // anonymous namespace

void 
MoonrayRendererInfo::configureBatchRenderMethod(
    FnKat::RendererInfo::DiskRenderMethod& batchRenderMethod) const
{
    batchRenderMethod.setDebugOutputSupported(true);
    batchRenderMethod.setDebugOutputFileType("rdla");
}

void
MoonrayRendererInfo::fillRenderMethods(RenderMethods& methods) const
{
    auto diskRenderMethod = new FnKat::RendererInfo::DiskRenderMethod();
    auto previewRenderMethod = new FnKat::RendererInfo::PreviewRenderMethod();
    auto liveRenderMethod = new FnKat::RendererInfo::LiveRenderMethod();

    previewRenderMethod->setDebugOutputSupported(true);
    previewRenderMethod->setDebugOutputFileType("rdla");
    previewRenderMethod->setSceneGraphDebugOutputSupported(true);

    const char* allowConcurrentRenders =
            ::getenv("MOONRAY_KATANA_ALLOW_CONCURRENT_RENDERS");
    if (allowConcurrentRenders && allowConcurrentRenders[0] == '1') {
        previewRenderMethod->setAllowConcurrentRenders(true);
    }

    liveRenderMethod->setDebugOutputSupported(false);

    methods.reserve(3);
    methods.push_back(diskRenderMethod);
    methods.push_back(previewRenderMethod);
    methods.push_back(liveRenderMethod);
}

void
MoonrayRendererInfo::fillRendererObjectTypes(
    std::vector<std::string>& renderObjectTypes,
    const std::string& type) const
{
    renderObjectTypes.clear();

    if (type == kFnRendererObjectTypeShader) {
        renderObjectTypes.reserve(4);
        renderObjectTypes.push_back(RDLObjectCache::sMaterial);
        renderObjectTypes.push_back(RDLObjectCache::sDisplacement);
        renderObjectTypes.push_back(RDLObjectCache::sLight);
        renderObjectTypes.push_back(RDLObjectCache::sLightFilter);
        renderObjectTypes.push_back(RDLObjectCache::sVolume);
    } else if (type == kFnRendererObjectTypeRenderOutput) {
        renderObjectTypes.push_back(kFnRendererOutputTypeColor);
        renderObjectTypes.push_back(kFnRendererOutputTypeRaw);
        renderObjectTypes.push_back(kFnRendererOutputTypeMerge);
        renderObjectTypes.push_back(kFnRendererOutputTypeScript);
    } else if (type == kFnRendererObjectTypeOutputChannelCustomParam) {
        renderObjectTypes.insert(renderObjectTypes.end(),
                kCustomChannelParams.begin(), kCustomChannelParams.end());
    }
}

void
MoonrayRendererInfo::fillRendererObjectNames(
    std::vector<std::string>& rendererObjectNames,
    const std::string& type,
    const std::vector<std::string>& typeTags) const
{
    // The Material node's "Add Shader" pulldown lists the shader types supplied
    // by "fillRendererObjectTypes". When a shader type is selected, and the
    // dropdown to select a specific shader is opened, this function is called
    // with the typeTags vector containing only 1 entry, which is the matching
    // shader type (e.g. "material")
    //
    // In the case of the MoonrayShadingNode, the typeTags vector is empty,
    // and all shaders will be returned.
    if (type == kFnRendererObjectTypeShader) {
        // Get the cache of render objects
        auto cache = RDLObjectCache::get();
        const auto& objectMap = cache->getRenderObjects(type);

        for (const auto& keyValue : objectMap) {
            const std::string& shaderName = keyValue.first;
            const kodachi_moonray::RenderObject* shader = keyValue.second.get();

            // If we have typeTags, filter on that
            bool matches = false;
            for (const std::string& tag : typeTags) {
                matches |= shader->isA(tag);
            }

            if (matches || typeTags.empty()) {
                rendererObjectNames.push_back(shaderName);
            }
        }

        // Add all defined rdl2 array types
        for (const auto& arrayType : kCustomArrayTypes) {
            if (typeTags.empty() ||
                    std::find(typeTags.begin(), typeTags.end(), arrayType.second) != typeTags.end()) {
                rendererObjectNames.push_back(arrayType.first);
            }
        }
    }
}

std::string
MoonrayRendererInfo::getRendererObjectDefaultType(const std::string& type) const
{
    return kFnRendererOutputTypeRaw;
}

std::string
MoonrayRendererInfo::getRegisteredRendererName() const
{
    return "moonray";
}

std::string
MoonrayRendererInfo::getRegisteredRendererVersion() const
{
    return getenv("REZ_MOONRAY_VERSION");
}

bool
MoonrayRendererInfo::isNodeTypeSupported(const std::string& nodeType) const
{
    return nodeType == "ShadingNode" || nodeType == "OutputChannelDefine";
}

void
MoonrayRendererInfo::fillShaderInputNames(std::vector<std::string>& names,
    const std::string& shaderName) const
{
    if (kCustomArrayTypes.count(shaderName) == 1) {
        for (int i = 0; i < kNumArrayInputs; ++i) {
            names.push_back("i" + std::to_string(i));
        }
    } else {
        auto cache = RDLObjectCache::get();
        const auto& objectMap =
            cache->getRenderObjects(kFnRendererObjectTypeShader);

        auto i = objectMap.find(shaderName);
        if (i != objectMap.end()) {
            // Find all of the bindable parameters on this shader and add them
            // as possible inputs.
            const kodachi_moonray::RenderObject* shader = i->second.get();
            for (const auto& param : shader->mParams) {
                if (!(param.mBindType.empty())) {
                    names.push_back(param.mName);
                }
            }
            if (shader->isA(RDLObjectCache::ObjectType::LIGHT)) {
                names.push_back("light_filters");
            }
        }
    }
}

void
MoonrayRendererInfo::fillShaderInputTags(std::vector<std::string>& tags,
    const std::string& shaderName,
    const std::string& inputName) const
{
    auto arrayIter = kCustomArrayTypes.find(shaderName);
    if (arrayIter != kCustomArrayTypes.end()) {
        // All array inputs are the same type regardless of inputName
        tags.push_back(arrayIter->second);
        return;
    } else {
        // Find the shader in question
        auto cache = RDLObjectCache::get();
        const auto& objectMap =
            cache->getRenderObjects(kFnRendererObjectTypeShader);

        auto i = objectMap.find(shaderName);
        if (i != objectMap.end()) {
            // Expose "light_filters" attr as a bindable shader input. This
            // will get processed in rdl2SceneContext::createNetworkTerminal
            // This only accepts a single input, so LightFilterArray should
            // be used to connect multiple filters.
            if (i->second->isA(RDLObjectCache::ObjectType::LIGHT) &&
                    inputName == "light_filters") {
                tags.push_back(RDLObjectCache::sLightFilter);
                return;
            }
            for (const kodachi_moonray::Param& param : i->second->mParams) {
                if (param.mName == inputName) {
                    tags.push_back(param.mBindType);
                    return;
                }
            }
        }
    }
}

void
MoonrayRendererInfo::fillShaderOutputNames(std::vector<std::string>& names,
    const std::string& shaderName) const
{
    // Shaders only have a single output. Unlike other renderers, Moonray
    // doesn't allow a bind or connection to a single component (e.g. green)
    // of the output.
    names.push_back("out");
}

void 
MoonrayRendererInfo::fillShaderOutputTags(
    std::vector<std::string>& shaderOutputTags,
    const std::string& shaderName,
    const std::string& outputName) const
{
    auto arrayIter = kCustomArrayTypes.find(shaderName);
    if (arrayIter != kCustomArrayTypes.end()) {
        shaderOutputTags.push_back(arrayIter->second);
        return;
    } else {
        // Find the shader in question
        auto cache = RDLObjectCache::get();
        const auto& objectMap =
            cache->getRenderObjects(kFnRendererObjectTypeShader);

        auto i = objectMap.find(shaderName);
        if (i != objectMap.end()) {
            // The output type of the shader is the same as the shader type itself.
            // This allows us to limit inputs to the accepted shader type. For
            // example, bindable inputs can only be bound to a map.
            const kodachi_moonray::RenderObject* shader = i->second.get();
            shader->fillShaderOutputTags(shaderOutputTags);
        }
    }
}

void 
MoonrayRendererInfo::fillRendererShaderTypeTags(
    std::vector<std::string>& shaderTypeTags,
    const std::string& shaderType) const
{
    shaderTypeTags.clear();
}

void MoonrayRendererInfo::_configureBasicRenderObjectInfo(FnAttribute::GroupBuilder &gb,
                                                         const std::string &name,
                                                         const std::string &type) const
{
    configureBasicRenderObjectInfo(gb,
                                   type,                                /* type      */
                                   { },                                 /* type tags */
                                   name,                                /* location  */
                                   { },                                 /* full path */
                                   kFnRendererObjectValueTypeUnknown,   /* outputType (unsupported) */
                                   FnAttribute::Attribute());
}

void
MoonrayRendererInfo::fillRenderTerminalOps(
            FnKat::RendererInfo::RendererInfoBase::OpDefinitionQueue& terminalOps,
            const FnAttribute::GroupAttribute& stateArgs) const
{
    // "liveRender", "previewRender", or "diskRender"
    const kodachi::StringAttribute renderMethodAttr =
            stateArgs.getChildByName(kFnTerminalOpStateArgRenderMethodType);

    if (renderMethodAttr == kFnRenderMethodTypeDiskRender) {
        // The MoonrayRenderManager doesn't handle disk renders, so these state
        // args won't be of any use to us. We'll manually add them in the render
        // plugin instead.
        return;
    }

    kodachi::GroupBuilder terminalOpsConfigBuilder;
    terminalOpsConfigBuilder.set("type", kodachi::StringAttribute("terminalOps"));

    if (renderMethodAttr.isValid()) {
        terminalOpsConfigBuilder.set("renderType", renderMethodAttr);
    }

    const kodachi::GroupAttribute systemOpArgsAttr =
            stateArgs.getChildByName(kTerminalOpStateArgSystem);

    // The RendererPlugin will disable this or add relevant op-args
    //terminalOps.emplace_back("GenerateKatanaId", kodachi::GroupAttribute(true));

    kodachi::GroupAttribute terminalOpsAttr =
            kodachi::BackendClient::getStaticData("MoonrayRenderBackend",
                                                   terminalOpsConfigBuilder.build());

    terminalOpsAttr = kodachi::optree_util::addSystemOpArgsToOpCollection(terminalOpsAttr, systemOpArgsAttr);

    for (const auto opDesc : terminalOpsAttr) {
        const kodachi::GroupAttribute opAttr(opDesc.attribute);
        const kodachi::StringAttribute opTypeAttr(opAttr.getChildByName("opType"));
        const kodachi::GroupAttribute opArgsAttr(opAttr.getChildByName("opArgs"));
        terminalOps.emplace_back(opTypeAttr.getValue(), std::move(opArgsAttr));
    }
}

bool
MoonrayRendererInfo::buildOutputChannel(FnAttribute::GroupBuilder &gb,
                                        const std::string &name,
                                        bool isCustom) const
{
    _configureBasicRenderObjectInfo(gb, name,
            isCustom ? kFnRendererObjectTypeOutputChannelCustomParam: kFnRendererObjectTypeOutputChannel);
    auto cache = RDLObjectCache::get();
    const auto& objectMap = 
        cache->getRenderObjects(kFnRendererObjectTypeOutputChannel);
    if (objectMap.empty()) return false;

    const kodachi_moonray::RenderObject* renderObject = objectMap.begin()->second.get();

    // dummies
    static const EnumPairVector enums;

    static const std::set<std::string> kSkippedParams { "active",
                                                        "file_name",
                                                        "file_part",
                                                        "compression",
                                                        "exr_header_attributes",
                                                        "output_type",
                                                        "exr_dwa_compression_level",
                                                        // denoiser attributes
                                                        "parity",
                                                        "denoiser_input",
                                                        "denoise",
                                                        "reference_render_output",
                                                        // checkpointing
                                                        "checkpoint_file_name" };

//    // type
//    std::string attrName = "type";
//    FnAttribute::GroupBuilder paramGb;
//    int paramType = kFnRendererObjectValueTypeString;
//    const char *typeOptions[] =
//    {
//        //"BYTE", "INT", "UINT", "BOOL", "FLOAT",
//        "RGB", "RGBA"//, "VECTOR", "VECTOR2",
//        //"STRING", "POINTER", "NODE", "ARRAY", "MATRIX"
//    };
//    paramGb.set("options", FnAttribute::StringAttribute(typeOptions, 2, 1)); //14
//    paramGb.set("widget", FnAttribute::StringAttribute("popup"));
//    addRenderObjectParam(gb, attrName, paramType, arraySize,
//            FnAttribute::StringAttribute("RGB"),
//            paramGb.build(), enums);

    // Add parameters
    for (const auto& param : renderObject->mParams) {
        if (kSkippedParams.count(param.mName) > 0 ||
                isCustom != find(kCustomChannelParams, param.mName) ||
                (isCustom && param.mName != name)) continue;

        FnAttribute::GroupBuilder hints;

        // Group
        if (!param.mGroup.empty()) {
            hints.set("page", FnAttribute::StringAttribute(param.mGroup));
        }

        // Help
        if (!param.mHelp.empty()) {
            hints.set("help", FnAttribute::StringAttribute(param.mHelp));
        }

        hints.set("label", FnAttribute::StringAttribute(param.mName));

        if (!param.mOptions.empty()) {
            hints.set("widget", FnAttribute::StringAttribute("popup"));
            hints.set("options", FnAttribute::StringAttribute(param.mOptions));
        }

        if (strncmp(param.mName.c_str(), "light ", 5) == 0) {
            hints.set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"));
            hints.set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../result"));
            hints.set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("light aov"));
        }

        if (strncmp(param.mName.c_str(), "material ", 8) == 0) {
            hints.set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"));
            hints.set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../result"));
            hints.set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("material aov"));
        }

        if (strncmp(param.mName.c_str(), "primitive ", 9) == 0) {
            hints.set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"));
            hints.set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../result"));
            hints.set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("primitive attribute"));
        }

        if (strncmp(param.mName.c_str(), "state ", 5) == 0) {
            hints.set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"));
            hints.set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../result"));
            hints.set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("state variable"));
        }

        if (param.mName == "visibility_aov") {
            hints.set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"));
            hints.set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../result"));
            hints.set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("visibility aov"));
        }

        if (param.mName == "cryptomatte_depth") {
            hints.set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"));
            hints.set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../result"));
            hints.set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("cryptomatte"));
        }

        addRenderObjectParam(gb,
            param.mName,
            param.mValueType,
            0,
            param.mDefaultValue,
            hints.build(),
            enums);
    }

    {
        // Cryptomatte Parameters
        FnAttribute::GroupBuilder hints;
        hints
            .set("help", kodachi::StringAttribute("The type of cryptomatte layer"))
            .set("label", kodachi::StringAttribute("cryptomatte layer"))
            .set("widget", kodachi::StringAttribute("mapper"))
            .set("options", kodachi::GroupAttribute("object", kodachi::StringAttribute("cryptomatte_object_id"),
                                                    false))
            .set("conditionalVisOps.conditionalVisOp", kodachi::StringAttribute("equalTo"))
            .set("conditionalVisOps.conditionalVisPath", kodachi::StringAttribute("../result"))
            .set("conditionalVisOps.conditionalVisValue", kodachi::StringAttribute("cryptomatte"));

        addRenderObjectParam(gb, "cryptomatte_layer", kFnRendererObjectValueTypeString,
                             0, kodachi::StringAttribute("object"), hints.build(), enums);
    }

    return true;
}

bool
MoonrayRendererInfo::buildRenderOutput(
                            FnAttribute::GroupBuilder& rendererObjectInfo,
                            const std::string& name,
                            const FnAttribute::GroupAttribute inputAttr) const
{
    FnAttribute::GroupAttribute globalStatementsAttr =
            inputAttr.getChildByName("moonrayGlobalStatements");
    std::string attrName;
    std::string helpStr;
    int arraySize = 0;
    int paramType;
    EnumPairVector enums;

    static const std::string defaultChanName = "beauty";

    if (name == kFnRendererOutputTypeColor || name == kFnRendererOutputTypeRaw) {
        //|| name == kFnRendererOutputTypeDeep

        if (name == kFnRendererOutputTypeRaw) {
            attrName = "tempRenderLocation";
            paramType = kFnRendererObjectValueTypeString;
            FnAttribute::GroupBuilder tempRenderLocationGroupBuilder;
            helpStr = "<p><b>For normal usage, this field should be left blank.</b></p>"
                    "<p>In the rare occasion that you need to specify the temp file "
                    "path that the renderer will initially write, enter it here. "
                    "This file path is only relevant during the render process. "
                    "When the render is complete, Katana converts or copies the temp "
                    "file to its final location and removes the temp file.</p>"
                    "<p>Use <code>$KATANA_TMPDIR</code> for a session-specific local "
                    "directory.</p>"
                    "<p>Example: <code>$KATANA_TMPDIR/my_unique_filename.#.exr</code></p>";
            tempRenderLocationGroupBuilder.set("help", FnAttribute::StringAttribute(helpStr));
            addRenderObjectParam(rendererObjectInfo, attrName, paramType, arraySize, FnAttribute::StringAttribute(""),
                    tempRenderLocationGroupBuilder.build(), enums);
        }

        // Start with default channel
        std::vector<std::string> outputChannels { defaultChanName };

        const FnAttribute::GroupAttribute outputGroup =
                globalStatementsAttr.getChildByName("outputChannels");
        if (outputGroup.isValid()) {
            for (auto child : outputGroup)
            {
                // We want to get the name from the nested StringAttibute
                // "name", this can correctly contain symbols and punctuation.
                const FnAttribute::GroupAttribute childGroup(child.attribute);

                // If the "name" attribute is not there we can still try to
                // use the groupAttr name.
                const FnAttribute::StringAttribute channelNameAttr = childGroup.getChildByName("name");

                // Get the chan, and make sure it's not the one we already added.
                // Could possibly eliminate all dupes here if we care.
                std::string chanName(child.name);
                if (channelNameAttr.isValid()) {
                    chanName = channelNameAttr.getValue();
                }

                if (chanName != defaultChanName) {
                    outputChannels.emplace_back(std::move(chanName));
                }
            }
        }

        attrName = "channel";
        paramType = kFnRendererObjectValueTypeString;
        FnAttribute::GroupBuilder channelGroupBuilder;
        channelGroupBuilder.set("widget", FnAttribute::StringAttribute("popup"));
        channelGroupBuilder.set("options", FnAttribute::StringAttribute(outputChannels));

        addRenderObjectParam(rendererObjectInfo, attrName, paramType, arraySize,
                             FnAttribute::StringAttribute(defaultChanName),
                             channelGroupBuilder.build(), enums);

        // Add specific channel params
        auto cache = RDLObjectCache::get();
        const auto& objectMap =
            cache->getRenderObjects(kFnRendererObjectTypeOutputChannel);
        if (!objectMap.empty()) {
            const kodachi_moonray::RenderObject* renderObject = objectMap.begin()->second.get();
            for (const auto& param : renderObject->mParams) {
                static const std::set<std::string> kParamWhitelist
                { "output_type", "exr_dwa_compression_level", "compression", "parity" };

                FnAttribute::GroupBuilder hints;
                if (kParamWhitelist.count(param.mName) > 0) {
                    if (!param.mHelp.empty()) {
                        hints.set("help", FnAttribute::StringAttribute(param.mHelp));
                    }
                    hints.set("label", FnAttribute::StringAttribute(param.mName));
                    if (!param.mOptions.empty()) {
                        hints.set("widget", FnAttribute::StringAttribute(param.mWidget));
                        hints.set("options", FnAttribute::StringAttribute(param.mOptions));
                    } else if (param.mName == "output_type") {
                        hints.set("widget", FnAttribute::StringAttribute("popup"));
                        hints.set("options", FnAttribute::StringAttribute(std::vector<std::string>{"flat", "deep"}));
                    }

                    addRenderObjectParam(rendererObjectInfo, param.mName, param.mValueType, 0,
                                         param.mDefaultValue, hints.build(), enums);
                }
            }

            {
                FnAttribute::GroupBuilder hints;
                hints
                    .set("help", kodachi::StringAttribute("Name of sub-image if using a multi-part exr file"))
                    .set("label", kodachi::StringAttribute("file part"))
                    .set("widget", kodachi::StringAttribute("string"));
                addRenderObjectParam(rendererObjectInfo, "file_part", kFnRendererObjectValueTypeString,
                                     0, kodachi::StringAttribute(""), hints.build(), enums);
            }

            // Instead of exposing all of the attributes needed
            // to set up denoising, we will add a "denoise" option to the
            // RenderOutput that we will check for and add the additional
            // outputs and references that are necessary.
            {
                FnAttribute::GroupBuilder hints;
                hints
                    .set("help", kodachi::StringAttribute(
                            "Select 'on' to auto-generate the required outputs for denoising during disk renders. The original output is not generated."))
                    .set("label", kodachi::StringAttribute("generate denoiser outputs"))
                    .set("widget", FnAttribute::StringAttribute("popup"))
                    .set("options", FnAttribute::StringAttribute(std::vector<std::string>{"off", "on"}));

                addRenderObjectParam(rendererObjectInfo, "generate_denoiser_outputs", kFnRendererObjectValueTypeString,
                                     0, kodachi::StringAttribute("off"), hints.build(), enums);
            }

            // Specify whether or not this output will actually
            // be denoised
            {
                FnAttribute::GroupBuilder hints;
                hints
                    .set("help", kodachi::StringAttribute(
                            "Select 'on' to denoise the output."))
                    .set("label", kodachi::StringAttribute("run denoiser"))
                    .set("widget", FnAttribute::StringAttribute("popup"))
                    .set("options", FnAttribute::StringAttribute(std::vector<std::string>{"off", "on"}));

                addRenderObjectParam(rendererObjectInfo, "run_denoiser", kFnRendererObjectValueTypeString,
                                     0, kodachi::StringAttribute("off"), hints.build(), enums);
            }

            {
                attrName = "finalRenderLocation";
                paramType = kFnRendererObjectValueTypeString;
                FnAttribute::GroupBuilder hints;
                hints
                    .set("help", kodachi::StringAttribute(
                            "The final render location for a denoised output"))
                    .set("label", kodachi::StringAttribute("final render location"))
                    .set("widget", FnAttribute::StringAttribute("assetIdOutput"))
                    .set("conditionalVisOps.conditionalVisOp", FnAttribute::StringAttribute("equalTo"))
                    .set("conditionalVisOps.conditionalVisPath", FnAttribute::StringAttribute("../generate_denoiser_outputs"))
                    .set("conditionalVisOps.conditionalVisValue", FnAttribute::StringAttribute("on"));

                addRenderObjectParam(rendererObjectInfo, attrName, paramType,
                        arraySize, FnAttribute::StringAttribute(""), hints.build(), enums);
            }

            {
                attrName = "checkpoint_file_name";
                paramType = kFnRendererObjectValueTypeString;
                FnAttribute::GroupBuilder hints;
                hints
                    .set("help", kodachi::StringAttribute(
                            "Name of the checkpoint output file"))
                    .set("label", kodachi::StringAttribute("checkpoint file name"))
                    .set("widget", kodachi::StringAttribute("assetIdOutput"));
                addRenderObjectParam(rendererObjectInfo, attrName, paramType,
                        arraySize, FnAttribute::StringAttribute(""), hints.build(), enums);
            }

            // disable until we have material cryptomattes
//            {
//                attrName = "cryptomatte_layer";
//                paramType = kFnRendererObjectValueTypeString;
//                FnAttribute::GroupBuilder hints;
//                hints
//                    .set("help", kodachi::StringAttribute("The type of cryptomatte layer"))
//                    .set("label", kodachi::StringAttribute("cryptomatte layer"))
//                    .set("widget", kodachi::StringAttribute("popup"))
//                    .set("options", kodachi::StringAttribute(std::vector<std::string>{"object", "material"}));
//
//                addRenderObjectParam(rendererObjectInfo, attrName, paramType,
//                                     0, kodachi::StringAttribute("object"), hints.build(), enums);
//            }

            {
                // Cryptomatte manifest parameters
                FnAttribute::GroupBuilder hints;
                attrName = "cryptomatte_manifest";
                paramType = kFnRendererObjectValueTypeString;
                hints
                    .set("help", kodachi::StringAttribute("The path of the cryptomatte idmap manifest"))
                    .set("label", kodachi::StringAttribute("cryptomatte manifest"))
                    .set("widget", kodachi::StringAttribute("assetIdOutput"));
                addRenderObjectParam(rendererObjectInfo, attrName, paramType,
                                     0, kodachi::StringAttribute(""), hints.build(), enums);
            }

            {
                attrName = "resume_file_name";
                paramType = kFnRendererObjectValueTypeString;
                FnAttribute::GroupBuilder hints;
                hints
                    .set("help", kodachi::StringAttribute(
                            "Name of the resume render input file"))
                    .set("label", kodachi::StringAttribute("resume file name"))
                    .set("widget", kodachi::StringAttribute("assetIdOutput"));
                addRenderObjectParam(rendererObjectInfo, attrName, paramType,
                        arraySize, FnAttribute::StringAttribute(""), hints.build(), enums);
            }

        }
        return true;
    }

    return false;
}

bool
MoonrayRendererInfo::buildRendererObjectInfo(
    FnAttribute::GroupBuilder& rendererObjectInfo,
    const std::string& name,
    const std::string& type,
    const FnAttribute::GroupAttribute inputAttr) const
{
    if (type == kFnRendererObjectTypeRenderOutput) {
        _configureBasicRenderObjectInfo(rendererObjectInfo, name, type);
        buildRenderOutput(rendererObjectInfo, name, inputAttr);
        return true;
    } else if (type == kFnRendererObjectTypeShader) {
        if (kCustomArrayTypes.count(name) == 1) {
            _configureBasicRenderObjectInfo(rendererObjectInfo, name, type);

            // Katana has a bug where shader inputs don't appear if
            // at least 1 parameter doesn't exist
            FnAttribute::GroupBuilder hints;
            hints.set("widget", FnAttribute::StringAttribute("null"));
            addRenderObjectParam(rendererObjectInfo,
                                 "fake",
                                 kFnRendererObjectValueTypeNull,
                                 0,
                                 FnAttribute::NullAttribute(),
                                 hints.build(),
                                 EnumPairVector());
            return true;
        } else {
            auto cache = RDLObjectCache::get();
            const auto& objectMap = cache->getRenderObjects(type);
            auto i = objectMap.find(name);

            if (i != objectMap.end()) {
                _configureBasicRenderObjectInfo(rendererObjectInfo, name, type);

                const kodachi_moonray::RenderObject* renderObject = i->second.get();
                const bool isLight = renderObject->isA(RDLObjectCache::ObjectType::LIGHT);

                // Add parameters
                for (const auto& param : renderObject->mParams) {
                    // Hide the "on" and "node_xform" attribute for lights, since we'll
                    // use GafferThree's equivalent attributes instead
                    if (isLight && (param.mName == "on" ||
                                    param.mName == "node_xform" ||
                                    param.mName == "light_filters")) {
                        continue;
                    }

                    FnAttribute::GroupBuilder hints;

                    // Group
                    if (!param.mGroup.empty()) {
                        hints.set("page",
                            FnAttribute::StringAttribute(param.mGroup));
                    }

                    // Help
                    if (!param.mHelp.empty()) {
                        hints.set("help",
                            FnAttribute::StringAttribute(param.mHelp));
                    }

                    // Widget
                    if (!param.mWidget.empty()) {
                        hints.set("widget",
                            FnAttribute::StringAttribute(param.mWidget));
                        if (param.mWidget == "sortableArray") {
                            hints.set("isDynamicArray", FnAttribute::IntAttribute(1));
                            hints.set("forceArray", FnAttribute::StringAttribute("True"));
                            // this may be a good idea for SceneObjectVector
                            // hints.set("childHints",
                            //           FnAttribute::GroupAttribute("widget",
                            //           FnAttribute::StringAttribute("scenegraphLocation"), false));
                        }
                    }

                    // Options
                    if (!param.mOptions.empty()) {
                        hints.set("options",
                            FnAttribute::StringAttribute(param.mOptions));
                    }

                    // Display Name
                    if (!param.mWidgetDisplayName.empty()) {
                        hints.set("label",
                            FnAttribute::StringAttribute(param.mWidgetDisplayName));
                    }

                    // Aliases
                    if (!param.mAliases.empty()) {
                        hints.set("aliases", FnAttribute::StringAttribute(param.mAliases));
                    }

                    addRenderObjectParam(rendererObjectInfo,
                        param.mName,
                        param.mValueType,
                        0,
                        param.mDefaultValue,
                        hints.build(),
                        EnumPairVector());

                    // Add a material + shader pair to shader inputs for lights so they can be
                    // set in the gaffer. Currently these are only interpreted for the map_shader
                    // input to a MeshLight.
                    if (isLight && !(param.mBindType.empty())) {
                        FnAttribute::GroupBuilder hints0;
                        hints0.set("widget", FnAttribute::StringAttribute("scenegraphLocation"))
                            .set("allowRelativePath", FnAttribute::IntAttribute(1))
                            .set("page", FnAttribute::StringAttribute(param.mGroup))
                            .set("label", FnAttribute::StringAttribute(param.mWidgetDisplayName + " material"))
                            .set("help", FnAttribute::StringAttribute("Material containing the " +
                                                                      param.mName + ". If blank the material assigned to the geometry is used."));
                        addRenderObjectParam(rendererObjectInfo,
                                             param.mName + "_material",
                                             kFnRendererObjectValueTypeLocation,
                                             0,
                                             FnAttribute::StringAttribute(""),
                                             hints0.build(),
                                             EnumPairVector());
                        FnAttribute::GroupAttribute hints1(
                            "page", FnAttribute::StringAttribute(param.mGroup),
                            "label", FnAttribute::StringAttribute(param.mWidgetDisplayName),
                            "help", FnAttribute::StringAttribute(param.mHelp),
                            nullptr);
                        addRenderObjectParam(rendererObjectInfo,
                                             param.mName + "_shader",
                                             kFnRendererObjectValueTypeString,
                                             0,
                                             FnAttribute::StringAttribute(""),
                                             hints1,
                                             EnumPairVector());
                    }
                }

                if (isLight) {
                    // Set parameter mappings
                    // TODO: Is there a better option than to hard-code this?
                    setShaderParameterMapping(rendererObjectInfo,
                        "shader",
                        "moonrayLightParams.moonrayLightShader");
                    setShaderParameterMapping(rendererObjectInfo,
                        "color",
                        "moonrayLightParams.color");
                    setShaderParameterMapping(rendererObjectInfo,
                        "intensity",
                        "moonrayLightParams.intensity");
                    setShaderParameterMapping(rendererObjectInfo,
                        "exposure",
                        "moonrayLightParams.exposure");
                }

                return true;
            }
        }
    }
    else if (type == kFnRendererObjectTypeOutputChannel) {
        return buildOutputChannel(rendererObjectInfo, name);
    } else if (type == kFnRendererObjectTypeOutputChannelCustomParam) {
        return buildOutputChannel(rendererObjectInfo, name, true);
    } else if (type == kFnRendererObjectTypeOutputChannelAttrHints){
        // TODO
    } else {
        FnLogWarn("Unhandled RendererObject type: " << type);
    }

    return false;
}

void
MoonrayRendererInfo::flushCaches()
{
    RDLObjectCache::flush();
}

FnPlugStatus
MoonrayRendererInfo::setHost(FnPluginHost* host)
{
    const auto status = RendererInfoBase::setHost(host);
    if (status == FnPluginStatusOK) {
        kodachi::BackendClient::setHost(host);
    }

    return status;
}

