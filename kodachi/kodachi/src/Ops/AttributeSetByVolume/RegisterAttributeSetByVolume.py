# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks


def registerAttributeSetByVolume():
    """
    Registers a new AttributeSetByVolume node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    volumes = {"cube": "cube",
               "cylinder": "poly_cylinder",
               "sphere": "poly_sphere"}
               
    methods = { "intersect": "intersect" }
    
    executionModes = { "immediate": "immediate",
                       "deferred" : "deferred" }

    def buildAttributeSetByVolumeOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
        
        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeSetByVolume}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Attribute Set node parameters
        nameParam = node.getParameter("name")
        CELParam = node.getParameter("CEL")
        volumeParam = node.getParameter("volume")
        invertParam = node.getParameter("invert")
        methodParam = node.getParameter("method")
        executionModeParam = node.getParameter("executionMode")
        attrSetPrimitivesParam = node.getParameter("attrSetPrimitives")
        attrSetAttributeNameParam = node.getParameter("attributeName")

        CEL = CELParam.getValue(0)
        volumeType = volumes[volumeParam.getValue(0)]
        methodType = methods[methodParam.getValue(0)]
        executionType = executionModes[executionModeParam.getValue(0)]
        transform = interface.getTransformAsAttribute()
        attrSetPrimitives = attrSetPrimitivesParam.getValue(0)
        getAttributeName = attrSetAttributeNameParam.getValue(0)

        gb = FnAttribute.GroupBuilder()

        attrSetVolumeLocation = nameParam.getValue(0)

        gb.set("volumeType", FnAttribute.StringAttribute(volumeType))
        sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
        sscb.addSubOpAtLocation(
            attrSetVolumeLocation, "AttributeSetVolumeCreateOp", gb.build())
        sscb.setAttrAtLocation(
            attrSetVolumeLocation, "xform.interactive", transform)
        sscb.setAttrAtLocation(
            attrSetVolumeLocation, "attributeEditor.exclusiveTo", FnAttribute.StringAttribute(node.getName()))
        interface.appendOp('StaticSceneCreate', sscb.build())

        # AttrSet Op
        interface.addOpSystemArgs(gb)
        gb.set("CEL", FnAttribute.StringAttribute(CEL))
        gb.set("invert", FnAttribute.IntAttribute(invertParam.getValue(0)))
        gb.set("attrSetVolumeLocation",
               FnAttribute.StringAttribute(attrSetVolumeLocation))
        gb.set("method", FnAttribute.StringAttribute(methodType))
        gb.set("executionMode", FnAttribute.StringAttribute(executionType))
        gb.set("collect_primitives", FnAttribute.IntAttribute(attrSetPrimitives))
        gb.set('attributeName', FnAttribute.StringAttribute(getAttributeName))
        
        interface.appendOp('AttributeSetByVolumeOp', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('AttributeSetByVolume')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('name', FnAttribute.StringAttribute('/root/world/geo/attrSetVolume'))
    gb.set('CEL', FnAttribute.StringAttribute(''))
    gb.set('volume', FnAttribute.StringAttribute('cube'))
    gb.set('invert', FnAttribute.IntAttribute(0))
    gb.set('method', FnAttribute.StringAttribute('intersect'))
    gb.set('executionMode', FnAttribute.StringAttribute('immediate'))
    gb.set('attrSetPrimitives', FnAttribute.IntAttribute(0))
    gb.set('attributeName', FnAttribute.StringAttribute('inVolume'))
    
    nodeTypeBuilder.addTransformParameters(gb)
    nodeTypeBuilder.addInteractiveTransformCallbacks(gb)

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('name', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The location to create the attrSet volume.'}
                                         )
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})
    nodeTypeBuilder.setHintsForParameter(
        'invert', {'widget': 'boolean',
                   'constant': 'True',
                   'help': 'If \"No\", KEEP the locations that match the method; otherwise do the opposite!'})
    nodeTypeBuilder.setHintsForParameter(
        'volume', {'widget': 'popup', 'constant': 'True', 'options': sorted(volumes.keys())})
    nodeTypeBuilder.setHintsForParameter(
        'method', {'widget': 'popup', 'constant': 'True', 'options': methods.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'executionMode', {'widget': 'popup', 'constant': 'True', 'options': executionModes.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'attrSetPrimitives', {'widget': 'checkBox',
                             'constant': 'True',
                             'help': 'Sets attribute of individual primitives of a collection of geometry. Currently only supporting curves.'})
    nodeTypeBuilder.setHintsForParameter(
        'attributeName', {'help': 'The name of the attribute that will be created and set for the objects in this volume.'})
    
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildAttributeSetByVolumeOpChain)

    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerAttributeSetByVolume()


def RegisterAttributeSetByVolumeResolvers(*args, **kwargs):
    """
    This converts "attrSet volume" locations to "polymesh" locations so that
    the viewer renders them correctly.
    """
    import Nodes3DAPI
    from Katana import FnAttribute

    opArgs = FnAttribute.GroupAttribute.parseXML("""<attr type="GroupAttr">
  <attr name="CEL" tupleSize="1" type="StringAttr">
    <sample size="1" time="0">
      <str value="((//*{@type==&quot;attrSet volume&quot;}))"/>
    </sample>
  </attr>
  <attr name="setAttrs" type="GroupAttr">
    <attr name="s0" type="GroupAttr">
      <attr name="name" tupleSize="1" type="StringAttr">
        <sample size="1" time="0">
          <str value="type"/>
        </sample>
      </attr>
      <attr name="attr" tupleSize="1" type="StringAttr">
        <sample size="1" time="0">
          <str value="polymesh"/>
        </sample>
      </attr>
      <attr name="inherit" tupleSize="1" type="IntAttr">
        <sample size="1" time="0" value="0 "/>
      </attr>
    </attr>
  </attr>
</attr>""")

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "AttributeSet", opArgs, addSystemArgs=True)


for cb in (Callbacks.Type.onStartup,):
    Callbacks.addCallback(cb, RegisterAttributeSetByVolumeResolvers)

RegisterAttributeSetByVolumeResolvers()
