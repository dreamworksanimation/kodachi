# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerPruneByFrustum():
    """
    Registers a new PruneByFrustum node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute
    
    methods = {"intersect": "intersect",
               "contains center": "contains center",
               "contains all": "contains all"}
    
    executionModes = { "immediate": "immediate",
                       "deferred" : "deferred" }

    def buildPruneByFrustumOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.PruneByFrustum}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        camLocParam = node.getParameter("camera_location")
        CELParam = node.getParameter("CEL")
        invertParam = node.getParameter("invert")
        methodParam = node.getParameter("method")
        executionModeParam = node.getParameter("executionMode")
        prunePrimitivesParam = node.getParameter("prune_primitives")
        paddingParam = node.getParameter("padding")

        camLocationValue = camLocParam.getValue(0)
        CEL = CELParam.getValue(0)
        methodType = methods[methodParam.getValue(0)]
        executionType = executionModes[executionModeParam.getValue(0)]
        prunePrimitives = prunePrimitivesParam.getValue(0)
        padding = paddingParam.getValue(0)

        gb = FnAttribute.GroupBuilder()

        # Prune Op
        interface.addOpSystemArgs(gb)
        gb.set("cameraLocation", FnAttribute.StringAttribute(camLocationValue))
        gb.set("CEL", FnAttribute.StringAttribute(CEL))
        gb.set("method", FnAttribute.StringAttribute(methodType))
        gb.set("executionMode", FnAttribute.StringAttribute(executionType))    
        gb.set("prune_primitives", FnAttribute.IntAttribute(prunePrimitives))
        gb.set("padding", FnAttribute.FloatAttribute(padding))    

        # invert == No -> do as expected -> keep objects fully inside the frustum
        gb.set("invert", FnAttribute.IntAttribute(invertParam.getValue(0)))

        interface.appendOp('PruneByFrustum', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('PruneByFrustum')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('camera_location', FnAttribute.StringAttribute(''))
    gb.set('CEL', FnAttribute.StringAttribute(''))
    gb.set('invert', FnAttribute.IntAttribute(0))
    gb.set('method', FnAttribute.StringAttribute('intersect'))
    gb.set('executionMode', FnAttribute.StringAttribute('immediate'))
    gb.set('prune_primitives', FnAttribute.IntAttribute(0))
    gb.set('padding', FnAttribute.FloatAttribute(1.0))

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('camera_location', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'Camera location.'})
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})
    nodeTypeBuilder.setHintsForParameter(
        'invert', {'widget': 'boolean',
                   'constant': 'True',
                   'help': 'If \"No\", KEEP the locations that match the method; otherwise do the opposite!'})
    nodeTypeBuilder.setHintsForParameter(
        'method', {'widget': 'popup', 'constant': 'True', 'options': methods.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'executionMode', {'widget': 'popup', 'constant': 'True', 'options': executionModes.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'prune_primitives', {'widget': 'checkBox',
                             'constant': 'True',
                             'help': 'Prunes individual primitives of a collection of geometry. Currently only supporting curves.'})
    nodeTypeBuilder.setHintsForParameter(
        'padding', {'constant': 'True',
                    'help': 'Adds a padding to the calculated frustum of the camera.'})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildPruneByFrustumOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerPruneByFrustum()
