# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerInstanceCreate():
    """
    Registers a new InstanceCreate node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    def buildInstanceCreateOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.InstanceCreate}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        srcLocParam = node.getParameter("source_location")
        instanceLocsParam = node.getParameter("instance_locations")

        srcLocationValue = srcLocParam.getValue(0)
        if not srcLocationValue:
            return
        
        srcLocationStrAttr = FnAttribute.StringAttribute(srcLocationValue)
        instanceLocationsValue = instanceLocsParam.getValue(0)        
        # Split sortableDelimitedString value into separate locations
        listOfInstanceLocations = instanceLocationsValue.split('|')
        if not listOfInstanceLocations:
            return

        gb = FnAttribute.GroupBuilder()
        interface.addOpSystemArgs(gb)
        gb.set("source_location", srcLocationStrAttr)        
        gb.set("instance_locations", FnAttribute.StringAttribute(instanceLocationsValue))

        # Create new locations, set type to "instance", set geometry.instanceSource attr
        instanceTypeStrAttr = FnAttribute.StringAttribute("instance")
        sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
        for loc in listOfInstanceLocations:
            if loc:
                sscb.setAttrAtLocation(loc, "type", instanceTypeStrAttr)
                sscb.setAttrAtLocation(loc, "geometry.instanceSource", srcLocationStrAttr)
                
        interface.appendOp('StaticSceneCreate', sscb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('InstanceCreate')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('source_location', FnAttribute.StringAttribute(''))
    gb.set("instance_locations", FnAttribute.StringAttribute(''))    

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints    
    nodeTypeBuilder.setHintsForParameter('source_location', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'Source location.'})
    
    nodeTypeBuilder.setHintsForParameter('instance_locations', {
                                         'widget': 'sortableDelimitedString',
                                         'help': 'Instance locations.'})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildInstanceCreateOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerInstanceCreate()
