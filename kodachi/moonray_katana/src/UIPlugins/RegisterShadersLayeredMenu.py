# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import NodegraphAPI, RenderingAPI, LayeredMenuAPI
from RenderingAPI import RenderPlugins


def PopulateMaterialCallback(layeredMenu):
    """
    Callback for the layered menu, which adds entries to the given
    C{layeredMenu} based on the available PRMan shaders.

    @type layeredMenu: L{LayeredMenuAPI.LayeredMenu}
    @param layeredMenu: The layered menu to add entries to.
    """
    # Obtain a list of names of available Moonray shaders from the renderer
    # info plug-in    
    rendererInfoPlugin = RenderPlugins.GetInfoPlugin('moonray')
    shaderType = RenderingAPI.RendererInfo.kRendererObjectTypeShader
    shaderNames = rendererInfoPlugin.getRendererObjectNames(shaderType, [
        "material", "displacement", "volume", "dwabaselayerable"])

    # Iterate over the names of shaders and add a menu entry for each of them
    # to the given layered menu, using a green-ish color
    for shaderName in shaderNames:
        layeredMenu.addEntry(shaderName, text=shaderName,
                             color=(0.3, 0.7, 0.3))
        
global __OPAXIS        
__OPAXIS = {"OpMap": "operation"}
        
        
def ShaderVariationGenerator(shaderName, rendererInfoPlugin):
    paramName = __OPAXIS.get(shaderName)
    if paramName:
        info = rendererInfoPlugin.getRendererObjectInfo(shaderName,
                                                        RenderingAPI.RendererInfo.kRendererObjectTypeShader)
        if info:
            paramInfo = info.getParam(paramName)
            if paramInfo:
                optionsAttr = paramInfo.getHints().getChildByName("options")
                if optionsAttr:
                    for option in optionsAttr.getNearestSample(0):
                        yield ".".join([shaderName, option])


def PopulateMapCallback(layeredMenu):
    """
    Callback for the layered menu, which adds entries to the given
    C{layeredMenu} based on the available PRMan shaders.

    @type layeredMenu: L{LayeredMenuAPI.LayeredMenu}
    @param layeredMenu: The layered menu to add entries to.
    """
    # Obtain a list of names of available Moonray shaders from the renderer
    # info plug-in
    rendererInfoPlugin = RenderPlugins.GetInfoPlugin('moonray')
    shaderType = RenderingAPI.RendererInfo.kRendererObjectTypeShader
    shaderNames = rendererInfoPlugin.getRendererObjectNames(shaderType, ["map"])

    # Iterate over the names of shaders and add a menu entry for each of them
    # to the given layered menu, using a green-ish color
    for shaderName in shaderNames:
        layeredMenu.addEntry(shaderName, text=shaderName,
                             color=(0.7, 0.3, 0.3))
        for shaderName in ShaderVariationGenerator(shaderName, rendererInfoPlugin):
            layeredMenu.addEntry(shaderName, text=shaderName,
                             color=(0.7, 0.3, 0.3))


def ActionCallback(value):
    """
    Callback for the layered menu, which creates a PrmanShadingNode node and
    sets its B{nodeType} parameter to the given C{value}, which is the name of
    a PRMan shader as set for the menu entry in L{PopulateCallback()}.

    @type value: C{str}
    @rtype: C{object}
    @param value: An arbitrary object that the menu entry that was chosen
        represents. In our case here, this is the name of a PRMan shader as
        passed to the L{LayeredMenuAPI.LayeredMenu.addEntry()} function in
        L{PopulateCallback()}.
    @return: An arbitrary object. In our case here, we return the created
        PrmanShadingNode node, which is then placed in the B{Node Graph} tab
        because it is a L{NodegraphAPI.Node} instance.
    """
    # Create the node, set its shader, and set the name with the shader name
    node = NodegraphAPI.CreateNode('MoonrayShadingNode')
    value, _, param = value.partition(".")
    node.getParameter('nodeType').setValue(value, 0)
    node.setName(value)
    node.getParameter('name').setValue(node.getName(), 0)
    node.checkDynamicParameters()
    if param:
        node.getParameter("parameters.%s.value" % __OPAXIS[value]).setValue(param, 0)
        node.getParameter("parameters.%s.enable" % __OPAXIS[value]).setValue(1, 0)
    return node

# Create and register a layered menu using the above callbacks
layeredMenuShaders = LayeredMenuAPI.LayeredMenu(PopulateMaterialCallback, ActionCallback,
                                         'Alt+P', alwaysPopulate=False,
                                         onlyMatchWordStart=False)
LayeredMenuAPI.RegisterLayeredMenu(layeredMenuShaders, 'MoonrayMaterials')

layeredMenuMaps = LayeredMenuAPI.LayeredMenu(PopulateMapCallback, ActionCallback,
                                         'Ctrl+P', alwaysPopulate=False,
                                         onlyMatchWordStart=False)
LayeredMenuAPI.RegisterLayeredMenu(layeredMenuMaps, 'MoonrayMaps')
