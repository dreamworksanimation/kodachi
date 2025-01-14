# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def registerCoCMetricSet():
    """
    Registers a new CoCMetricSet node type using the NodeTypeBuilder utility
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
        for paramName in ("CEL", "cameraLocation"):
            gb.set(paramName, interface.buildAttrFromParam(node.getParameter(paramName)))
         
        interface.appendOp('CoCMetricSet', gb.build())
 
    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('CoCMetricSet')
 
    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))
 
    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute('/root/world/geo//*'))
    gb.set('cameraLocation', FnAttribute.StringAttribute('/root/world/cam/camera'))
     
    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())
 
    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('cameraLocation', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The location whose worldspace xform is measured from.'}
                                         )
    nodeTypeBuilder.setHintsForParameter('CEL', {
                                         'widget': 'cel',
                                         'help': 'The locations to measure against cameraLocation.'}
                                         )
 
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)
 
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerCoCMetricSet()
