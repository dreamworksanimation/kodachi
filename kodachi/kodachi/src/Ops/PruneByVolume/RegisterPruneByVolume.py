# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks


def registerPruneByVolume():
    """
    Registers a new PruneByVolume node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    modes = { "create volume": "create volume" ,
              "use existing" : "use existing" }
    
    volumes = {"cube": "cube",
               "cylinder": "poly_cylinder",
               "sphere": "poly_sphere"}
               
    methods = { "intersect": "intersect" }
    
    executionModes = { "immediate": "immediate",
                       "deferred" : "deferred" }

    def buildPruneByVolumeOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
        @type node: C{Nodes3DAPI.NodeTypeBuilder.PruneByVolume}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        nameParam = node.getParameter("name")
        CELParam = node.getParameter("CEL")
        modeParam = node.getParameter("mode")
        pVolPathsParam = node.getParameter("pruneVolumePaths")
        volumeParam = node.getParameter("volume")
        invertParam = node.getParameter("invert")
        methodParam = node.getParameter("method")
        executionModeParam = node.getParameter("executionMode")
        prunePrimitivesParam = node.getParameter("prune_primitives")

        CEL = CELParam.getValue(0)
        modeType = modes[modeParam.getValue(0)]
        pVolPaths = [pVolPathsParam.getChildByIndex(i).getValue(0)
                     for i in xrange(pVolPathsParam.getNumChildren())]
        volumeType = volumes[volumeParam.getValue(0)]
        methodType = methods[methodParam.getValue(0)]
        executionType = executionModes[executionModeParam.getValue(0)]
        transform = interface.getTransformAsAttribute()
        prunePrimitives = prunePrimitivesParam.getValue(0)

        gb = FnAttribute.GroupBuilder()

        pruneVolumeLocation = nameParam.getValue(0)

        if modeType == modes["create volume"]:
            gb.set("volumeType", FnAttribute.StringAttribute(volumeType))
            sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
            sscb.addSubOpAtLocation(
                pruneVolumeLocation, "PruneVolumeSingleCreateOp", gb.build())
            sscb.setAttrAtLocation(
                pruneVolumeLocation, "xform.interactive", transform)
            sscb.setAttrAtLocation(
                pruneVolumeLocation, "attributeEditor.exclusiveTo", FnAttribute.StringAttribute(node.getName()))
            interface.appendOp('StaticSceneCreate', sscb.build())

        # Prune Op
        interface.addOpSystemArgs(gb)
        gb.set("CEL", FnAttribute.StringAttribute(CEL))
        gb.set("mode", FnAttribute.StringAttribute(modeType))
        gb.set("pruneVolumePaths", FnAttribute.StringAttribute(pVolPaths))
        gb.set("invert", FnAttribute.IntAttribute(invertParam.getValue(0)))
        gb.set("pruneVolumeLocation",
               FnAttribute.StringAttribute(pruneVolumeLocation))
        gb.set("method", FnAttribute.StringAttribute(methodType))
        gb.set("executionMode", FnAttribute.StringAttribute(executionType))
        gb.set("prune_primitives", FnAttribute.IntAttribute(prunePrimitives))
        
        interface.appendOp('PruneByVolumeOp', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('PruneByVolume')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('name', FnAttribute.StringAttribute('/root/world/geo/pruneVolume'))
    gb.set('CEL', FnAttribute.StringAttribute(''))
    gb.set('mode', FnAttribute.StringAttribute('create volume'))
    gb.set('pruneVolumePaths', FnAttribute.StringAttribute(''))
    gb.set('volume', FnAttribute.StringAttribute('cube'))
    gb.set('invert', FnAttribute.IntAttribute(0))
    gb.set('method', FnAttribute.StringAttribute('intersect'))
    gb.set('executionMode', FnAttribute.StringAttribute('immediate'))
    gb.set('prune_primitives', FnAttribute.IntAttribute(0))
    
    nodeTypeBuilder.addTransformParameters(gb)
    nodeTypeBuilder.addInteractiveTransformCallbacks(gb)

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build(), forceArrayNames=['pruneVolumePaths'])

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('name', {
                                         'widget': 'scenegraphLocation',
                                         'constant': 'True',
                                         'help': 'The location to create the prune volume.',
                                         'conditionalVisOps' : {
                                             'conditionalVisOp': 'equalTo', 
                                             'conditionalVisPath': '../mode', 
                                             'conditionalVisValue': 'create volume'}}
                                         )
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})
    nodeTypeBuilder.setHintsForParameter(
        'mode', {'widget': 'popup', 'constant': 'True', 'options': sorted(modes.keys())})
    nodeTypeBuilder.setHintsForParameter(
        'pruneVolumePaths', {'widget': 'scenegraphLocationArray',
                             'help': 'The scenegraph path to the prune volumes to be used',
                             'conditionalVisOps': {'conditionalVisOp': 'equalTo', 
                                       'conditionalVisPath': '../mode', 
                                       'conditionalVisValue': 'use existing'}})
    nodeTypeBuilder.setHintsForParameter(
        'invert', {'widget': 'boolean',
                   'constant': 'True',
                   'help': 'If \"No\", KEEP the locations that match the method; otherwise do the opposite!'})
    nodeTypeBuilder.setHintsForParameter(
        'volume', {'widget': 'popup', 'constant': 'True', 'options': sorted(volumes.keys()),
                   'conditionalVisOps': {'conditionalVisOp': 'equalTo', 
                                         'conditionalVisPath': '../mode', 
                                         'conditionalVisValue': 'create volume'}})
    nodeTypeBuilder.setHintsForParameter(
        'method', {'widget': 'popup', 'constant': 'True', 'options': methods.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'executionMode', {'widget': 'popup', 'constant': 'True', 'options': executionModes.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'prune_primitives', {'widget': 'checkBox',
                             'constant': 'True',
                             'help': 'Prunes individual primitives of a collection of geometry. Currently only supporting curves.'})
    nodeTypeBuilder.setHintsForParameter(
        'transform', {'conditionalVisOps': {'conditionalVisOp': 'equalTo', 
                                            'conditionalVisPath': '../mode', 
                                            'conditionalVisValue': 'create volume'}})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildPruneByVolumeOpChain)

    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerPruneByVolume()


def RegisterPruneByVolumeResolvers(*args, **kwargs):
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
    Callbacks.addCallback(cb, RegisterPruneByVolumeResolvers)

RegisterPruneByVolumeResolvers()