// Copyright 2025 DreamWorks Animation LLC
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <FnRendererInfo/plugin/RendererInfoBase.h>

class MoonrayRendererInfo : public FnKat::RendererInfo::RendererInfoBase
{
public:
    MoonrayRendererInfo() {}

    static FnKat::RendererInfo::RendererInfoBase* create()
    {
        return new MoonrayRendererInfo();
    }

    static void flush() {}

    void configureBatchRenderMethod(
        FnKat::RendererInfo::DiskRenderMethod& batchRenderMethod) const override;

    typedef std::vector<FnKat::RendererInfo::RenderMethod*> RenderMethods;
    
    void fillRenderMethods(RenderMethods& renderMethods) const override;

    void fillRendererObjectTypes(std::vector<std::string>& renderObjectTypes,
        const std::string& type) const override;

    void fillRendererObjectNames(std::vector<std::string>& rendererObjectNames,
        const std::string& type,
        const std::vector<std::string>& typeTags) const override;

    std::string getRendererObjectDefaultType(const std::string& type) 
        const override;

    std::string getRegisteredRendererName() const override;

    std::string getRegisteredRendererVersion() const override;

    bool isNodeTypeSupported(const std::string& nodeType) const override;

    /**
     * Determines the parameters on the shader that should be exposed as
     * inputs in the MoonrayShadingNode. This is limited to the attributes
     * that are marked as "bindable" in SceneRDL2 or attributes that are
     * specifically inputs to other shaders (e.g. CombineDisplacementMap).
     */
    void fillShaderInputNames(std::vector<std::string>& names,
        const std::string& shaderName) const override;

    /**
     * For the parameters chosen in fillShaderInputsNames(), this specifies
     * the type of value that is an acceptable input. The tags here should
     * match values in fillShaderOutputTags.
     */
    void fillShaderInputTags(std::vector<std::string>& tags,
        const std::string& shaderName,
        const std::string& inputName) const override;

    /**
     * Determines the parameters on the shader that should be exposed as outputs
     * in the MoonrayShadingNode.
     */
    void fillShaderOutputNames(std::vector<std::string>& names,
        const std::string& shaderName) const override;

    /**
     * Tag that determines whether a shader input (defined in 
     * fillShaderInputNames() and fillShaderInputTags()) can be bound to this 
     * output.
     */
    void fillShaderOutputTags(std::vector<std::string>& shaderOutputTags,
        const std::string& shaderName,
        const std::string& outputName) const override;

    void fillRendererShaderTypeTags(std::vector<std::string>& shaderTypeTags,
        const std::string& shaderType) const override;

    void fillRenderTerminalOps(
            FnKat::RendererInfo::RendererInfoBase::OpDefinitionQueue& terminalOps,
            const FnAttribute::GroupAttribute& stateArgs) const override;

    bool buildRendererObjectInfo(FnAttribute::GroupBuilder& rendererObjectInfo,
        const std::string& name,
        const std::string& type,
        const FnAttribute::GroupAttribute inputAttr) const override;

    void flushCaches() override;

    static FnPlugStatus setHost(FnPluginHost* host);

private:

    void _configureBasicRenderObjectInfo(FnAttribute::GroupBuilder &gb,
                                         const std::string &name,
                                         const std::string &type) const;

    bool buildRenderOutput(FnAttribute::GroupBuilder& rendererObjectInfo,
                           const std::string& name,
                           const FnAttribute::GroupAttribute inputAttr) const;
    bool buildOutputChannel(FnAttribute::GroupBuilder &gb,
                            const std::string &name,
                            bool isCustom = false) const;
};

