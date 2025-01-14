# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def registerLightFilterAssign():
    """
    Registers a new LightFilterAssign node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnAttribute
 
    def buildOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
 
        @type node: C{Nodes3DAPI.NodeTypeBuilder.CoCMetricSet}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        gb = FnAttribute.GroupBuilder()
        for paramName in ("lights", "lightFilterLocation"):
            gb.set(paramName, interface.buildAttrFromParam(node.getParameter(paramName)))
         
        interface.appendOp('LightFilterAssign', gb.build())
 
    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('LightFilterAssign')
 
    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))
 
    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('lights', FnAttribute.StringAttribute(''))
    gb.set('lightFilterLocation', FnAttribute.StringAttribute(''))
     
    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())
 
    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('lightFilterLocation', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The light filter to assign.'}
                                         )
    nodeTypeBuilder.setHintsForParameter('lights', {
                                         'widget': 'cel',
                                         'help': 'The locations to assign to (only valid for light locations).'}
                                         )
 
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)
 
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerLightFilterAssign()

def registerLightFilterAssignResolve():
    """
    Registers a new LightFilterAssignResolve node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnAttribute

    def buildOpChain(node, interface):    
        gb = FnAttribute.GroupBuilder()
        
        celParam = node.getParameter('CEL')
        cel = celParam.getValue(0)
    
        gb.set('CEL', FnAttribute.StringAttribute(cel))
        interface.appendOp('LightFilterAssignResolve', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('LightFilterAssignResolve')
 
    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute(''))

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())
 
    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('CEL', {
                                         'widget': 'cel',
                                         'help': 'The light locations to resolve.'}
                                         )

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)

    # Build the new node type
    nodeTypeBuilder.build()

registerLightFilterAssignResolve()

# Register the LightFilterAssign resolver.
def RegisterLightFilterAssignResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeStandardResolvers,
        "LightFilterAssignResolve", FnAttribute.GroupAttribute(), addSystemArgs=False)

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "LightFilterAssignResolve", FnAttribute.GroupAttribute(), addSystemArgs=False)

RegisterLightFilterAssignResolvers()


