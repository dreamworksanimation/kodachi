# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks


def registerPruneVolumeCreate():
    """
    Registers a new PruneVolumeArrayCreate node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    volumes = {"cube": "cube",
               "cylinder": "poly_cylinder",
               "sphere": "poly_sphere"}

    def buildPruneVolumeCreateOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
        @type node: C{Nodes3DAPI.NodeTypeBuilder.PruneVolumeArrayCreate}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        nameParam = node.getParameter("name")
        volumeParam = node.getParameter("volume")
          
        volumeType = volumes[volumeParam.getValue(0)]
        transform = interface.getTransformAsAttribute()
  
        gb = FnAttribute.GroupBuilder()
        
        pruneVolumeLocation = nameParam.getValue(0)
         
        interface.addOpSystemArgs(gb)
        gb.set("pruneVolumeLocation",
               FnAttribute.StringAttribute(pruneVolumeLocation))
        gb.set("volumeType", FnAttribute.StringAttribute(volumeType))
          
        sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
        sscb.addSubOpAtLocation(
            pruneVolumeLocation, "PruneVolumeCreateOp", gb.build())
        sscb.setAttrAtLocation(
            pruneVolumeLocation, "xform.interactive", transform)
        sscb.setAttrAtLocation(
            pruneVolumeLocation, "attributeEditor.exclusiveTo", FnAttribute.StringAttribute(node.getName()))
        interface.appendOp('StaticSceneCreate', sscb.build())
         
        interface.appendOp('PruneVolumeCreateOp', gb.build())
        
    def buildParameters(node):
        """
        Defines a callback function which has an opportunity to
        procedurally create parameters on the newly created node instance.
        """
        nameParam = node.getParameter("name")
        nodeName = node.getName()
        pvc = "PruneVolumeCreate"
        num = nodeName[len(pvc):]
        path = nameParam.getValue(0) + num
        nameParam.setValue(path, 0, False)

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('PruneVolumeCreate')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('name', FnAttribute.StringAttribute('/root/world/geo/pruneVolumeArray/pruneVolume'))
    gb.set('volume', FnAttribute.StringAttribute('cube'))
    
    nodeTypeBuilder.addTransformParameters(gb)
    nodeTypeBuilder.addInteractiveTransformCallbacks(gb)

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('name', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The location to create the prune volume.'}
                                         )
    nodeTypeBuilder.setHintsForParameter(
        'volume', {'widget': 'popup', 'constant': 'True', 'options': sorted(volumes.keys())})

    # Set the callback responsible to build the parameters
    nodeTypeBuilder.setBuildParametersFnc(buildParameters)
    
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildPruneVolumeCreateOpChain)

    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerPruneVolumeCreate()


def RegisterPruneVolumeCreateResolvers(*args, **kwargs):
    """
    This converts "prune volume" locations to "polymesh" locations so that
    the viewer renders them correctly.
    """
    import Nodes3DAPI
    from Katana import FnAttribute

    opArgs = FnAttribute.GroupAttribute.parseXML("""<attr type="GroupAttr">
  <attr name="CEL" tupleSize="1" type="StringAttr">
    <sample size="1" time="0">
      <str value="((//*{@type==&quot;prune volume&quot;}))"/>
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
    Callbacks.addCallback(cb, RegisterPruneVolumeCreateResolvers)

RegisterPruneVolumeCreateResolvers()