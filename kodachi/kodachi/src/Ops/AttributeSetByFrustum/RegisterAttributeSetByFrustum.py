# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks


def registerAttributeSetByFrustum():
    """
    Registers a new AttributeSetByFrustum node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    methods = {"intersect": "intersect",
               "contains center": "contains center",
               "contains all": "contains all"}
    
    executionModes = { "immediate": "immediate",
                       "deferred" : "deferred" }

    def buildAttributeSetByFrustumOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
        
        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeSetByFrustum}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Attribute Set node parameters
        camLocParam = node.getParameter("camera_location")
        CELParam = node.getParameter("CEL")
        invertParam = node.getParameter("invert")
        methodParam = node.getParameter("method")
        executionModeParam = node.getParameter("executionMode")
        attrSetAttributeNameParam = node.getParameter("attributeName")
        paddingParam = node.getParameter("padding")

        camLocationValue = camLocParam.getValue(0)
        CEL = CELParam.getValue(0)
        methodType = methods[methodParam.getValue(0)]
        executionType = executionModes[executionModeParam.getValue(0)]
        getAttributeName = attrSetAttributeNameParam.getValue(0)
        padding = paddingParam.getValue(0)

        gb = FnAttribute.GroupBuilder()

        # AttrSet Op
        interface.addOpSystemArgs(gb)
        gb.set("cameraLocation", FnAttribute.StringAttribute(camLocationValue))
        gb.set("CEL", FnAttribute.StringAttribute(CEL))
        gb.set("invert", FnAttribute.IntAttribute(invertParam.getValue(0)))
        gb.set("method", FnAttribute.StringAttribute(methodType))
        gb.set("executionMode", FnAttribute.StringAttribute(executionType))
        gb.set('attributeName', FnAttribute.StringAttribute(getAttributeName))
        gb.set("padding", FnAttribute.FloatAttribute(padding))    
        
        interface.appendOp('AttributeSetByFrustumOp', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('AttributeSetByFrustum')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('camera_location', FnAttribute.StringAttribute(''))
    gb.set('CEL', FnAttribute.StringAttribute(''))
    gb.set('invert', FnAttribute.IntAttribute(0))
    gb.set('method', FnAttribute.StringAttribute('intersect'))
    gb.set('executionMode', FnAttribute.StringAttribute('immediate'))
    gb.set('attributeName', FnAttribute.StringAttribute('inFrustum'))
    gb.set('padding', FnAttribute.FloatAttribute(1.0))
    
    nodeTypeBuilder.addTransformParameters(gb)
    nodeTypeBuilder.addInteractiveTransformCallbacks(gb)

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
        'attributeName', {'help': 'The name of the attribute that will be created and set for the objects in this frustum.'})
    nodeTypeBuilder.setHintsForParameter(
        'padding', {'constant': 'True',
                    'help': 'Adds a padding to the calculated frustum of the camera.'})
    
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildAttributeSetByFrustumOpChain)

    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerAttributeSetByFrustum()
