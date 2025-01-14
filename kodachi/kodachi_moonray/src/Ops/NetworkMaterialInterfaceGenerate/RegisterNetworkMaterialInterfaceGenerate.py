# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def registerNetworkMaterialInterface():
    """
    Registers a new CoCMetricSet node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnAttribute, FnGeolibServices
 
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
        locationAttr = interface.buildAttrFromParam(node.getParameter('location'))
        nodesAttr = interface.buildAttrFromParam(node.getParameter('nodes'))
        parametersAttr = interface.buildAttrFromParam(node.getParameter('parameters'))
        
        asb = FnGeolibServices.OpArgsBuilders.AttributeSet()
        asb.setLocationPaths(locationAttr)
        asb.setAttr(
            "material.__applyNodeDefaults",
            FnAttribute.IntAttribute(1)
        )
        interface.appendOp('AttributeSet', asb.build())
        
        gb = FnAttribute.GroupBuilder()
        gb.set('location', locationAttr)
        gb.set('nodes', nodesAttr)
        gb.set('parameters', parametersAttr)
         
        interface.appendOp('NetworkMaterialInterfaceGenerate', gb.build())
 
    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('NetworkMaterialInterfaceGenerate')
 
    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))
 
    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('location', FnAttribute.StringAttribute(''))

    # nodes
    ngb = FnAttribute.GroupBuilder()
    ngb.set('mode', FnAttribute.StringAttribute('pattern'))
    ngb.set('pattern', FnAttribute.StringAttribute(''))
    ngb.set('whitelist', FnAttribute.StringAttribute(''))

    gb.set('nodes', ngb.build())

    # parameters
    pgb = FnAttribute.GroupBuilder()
    pgb.set('whitelist', FnAttribute.StringAttribute(''))

    gb.set('parameters', pgb.build())
     
    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # nodeTypeBuilder.setGenericAssignRoots('args', '__networkMaterialInterface')
 
    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('location', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The location of network material to generate an interface for'}
                                         )

    nodeTypeBuilder.setHintsForParameter('nodes.mode', {
        'widget': 'popup',
        'options': [
            'pattern',
            'whitelist',
        ]
    })

    nodeTypeBuilder.setHintsForParameter('nodes.pattern', {
        'conditionalVisOps': {
            'conditionalVisPath': '../mode',
            'conditionalVisOp': 'equalTo',
            'conditionalVisValue': 'pattern'
        }
    })

    nodeTypeBuilder.setHintsForParameter('nodes.whitelist', {
        'conditionalVisOps': {
            'conditionalVisPath': '../mode',
            'conditionalVisOp': 'equalTo',
            'conditionalVisValue': 'whitelist'
        },
        'widget': 'sortableDelimitedString',
        'isDynamicArray': True,
        'delimiter': ' ',
        'tupleSize': 1,
    })

    nodeTypeBuilder.setHintsForParameter('parameters.whitelist', {
        'widget': 'sortableDelimitedString',
        'isDynamicArray': True,
        'delimiter': ' ',
        'tupleSize': 1,
    })
 
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)
 
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerNetworkMaterialInterface()
