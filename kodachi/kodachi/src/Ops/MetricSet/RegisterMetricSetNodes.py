# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def registerDistanceMetricSet():
    """
    Registers a new DistanceMetricSet node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnAttribute
 
    def buildOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
 
        @type node: C{Nodes3DAPI.NodeTypeBuilder.DistanceMetricSet}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        gb = FnAttribute.GroupBuilder()
        for paramName in ("CEL", "targetLocation", "distanceAttributeName"):
            gb.set(paramName, interface.buildAttrFromParam(node.getParameter(paramName)))
         
        interface.appendOp('DistanceMetricSet', gb.build())
 
    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('DistanceMetricSet')
 
    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))
 
    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute('/root/world/geo//*'))
    gb.set('targetLocation', FnAttribute.StringAttribute('/root/world/cam/camera'))
    gb.set('distanceAttributeName', FnAttribute.StringAttribute('distance.camera'))
     
    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())
 
    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('targetLocation', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The location whose worldspace xform is measured from.'}
                                         )
    nodeTypeBuilder.setHintsForParameter('CEL', {
                                         'widget': 'cel',
                                         'help': 'The locations to measure against targetLocation.'}
                                         )
    nodeTypeBuilder.setHintsForParameter('distanceAttributeName', {
                                         'help': 'The distance value will be stored at metrics.<distanceAttributeName>'}
                                         )
 
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)
 
    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerDistanceMetricSet()


def registerVolumeMetricSet():
    """
    Registers a new VolumeMetricSet node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnAttribute
 
    def buildOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
 
        @type node: C{Nodes3DAPI.NodeTypeBuilder.VolumeMetricSet}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        gb = FnAttribute.GroupBuilder()
        for paramName in ("CEL",):
            gb.set(paramName, interface.buildAttrFromParam(node.getParameter(paramName)))
         
        interface.appendOp('VolumeMetricSet', gb.build())
 
    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('VolumeMetricSet')
 
    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))
 
    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute('/root/world/geo//*'))
     
    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())
 
    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('CEL', {
                                         'widget': 'cel',
                                         'help': 'The locations whose volume will be measured and stored as metrics.volume.'}
                                         )
 
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)
 
    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerVolumeMetricSet()

