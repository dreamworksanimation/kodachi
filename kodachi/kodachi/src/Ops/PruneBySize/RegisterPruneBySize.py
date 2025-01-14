# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks


def registerPruneBySize():
    """
    Registers a new PruneBySize node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute
    
    kCompareVolume = "compare volume"
    kCompareDimensions = "compare dimensions"
    kDontCheck = "don't check"
    kGreaterThan = "greater than"
    kLessThan = "less than"

    modes = { kCompareVolume : kCompareVolume,
              kCompareDimensions: kCompareDimensions }
    
    volumeConds = { kGreaterThan: kGreaterThan ,
                    kLessThan : kLessThan }
    
    dimensionConds = { kDontCheck: kDontCheck ,
                       kGreaterThan: kGreaterThan,
                       kLessThan: kLessThan }
               
    methods = { "intersect": "intersect" }
    
    executionModes = { "immediate": "immediate",
                       "deferred" : "deferred" }

    def buildPruneBySizeOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.
        @type node: C{Nodes3DAPI.NodeTypeBuilder.PruneBySize}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        CELParam = node.getParameter("CEL")
        modeParam = node.getParameter("mode")
        executionModeParam = node.getParameter("executionMode")
        xLengthParam = node.getParameter("dimensionConditions.xLength")
        xParam = node.getParameter("dimensionConditions.x")
        yLengthParam = node.getParameter("dimensionConditions.yLength")
        yParam = node.getParameter("dimensionConditions.y")
        zLengthParam = node.getParameter("dimensionConditions.zLength")
        zParam = node.getParameter("dimensionConditions.z")
        volumeParam = node.getParameter("volumeConditions.volume")
        vParam = node.getParameter("volumeConditions.v")
        
        CEL = CELParam.getValue(0)
        modeType = modes[modeParam.getValue(0)]
        executionType = executionModes[executionModeParam.getValue(0)]
        xLength = xLengthParam.getValue(0)
        xComp = xParam.getValue(0)
        yLength = yLengthParam.getValue(0)
        yComp = yParam.getValue(0)
        zLength = zLengthParam.getValue(0)
        zComp = zParam.getValue(0)
        volume = volumeParam.getValue(0)
        vComp = vParam.getValue(0)
        
        gb = FnAttribute.GroupBuilder()

        # Prune Op
        interface.addOpSystemArgs(gb)
        gb.set("CEL", FnAttribute.StringAttribute(CEL))
        gb.set("mode", FnAttribute.StringAttribute(modeType))
        gb.set("executionMode", FnAttribute.StringAttribute(executionType))
        gb.set("xLength", FnAttribute.StringAttribute(xLength))
        gb.set("xComp", FnAttribute.DoubleAttribute(xComp))
        gb.set("yLength", FnAttribute.StringAttribute(yLength))
        gb.set("yComp", FnAttribute.DoubleAttribute(yComp))
        gb.set("zLength", FnAttribute.StringAttribute(zLength))
        gb.set("zComp", FnAttribute.DoubleAttribute(zComp))
        gb.set("volume", FnAttribute.StringAttribute(volume))
        gb.set("vComp", FnAttribute.DoubleAttribute(vComp))
        
        interface.appendOp('PruneBySizeOp', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('PruneBySize')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute(''))
    gb.set('mode', FnAttribute.StringAttribute(kCompareVolume))
    
    gb1 = FnAttribute.GroupBuilder()
    gb1.set('xLength', FnAttribute.StringAttribute(kDontCheck))
    gb1.set('x', FnAttribute.DoubleAttribute(0.0))
    gb1.set('yLength', FnAttribute.StringAttribute(kDontCheck))
    gb1.set('y', FnAttribute.DoubleAttribute(0.0))
    gb1.set('zLength', FnAttribute.StringAttribute(kDontCheck))
    gb1.set('z', FnAttribute.DoubleAttribute(0.0))
    gb.set('dimensionConditions', gb1.build())

    gb2 = FnAttribute.GroupBuilder()
    gb2.set('volume', FnAttribute.StringAttribute(kGreaterThan))
    gb2.set('v', FnAttribute.DoubleAttribute(0.0))
    gb.set('volumeConditions', gb2.build())

    gb.set('executionMode', FnAttribute.StringAttribute('immediate'))
    

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})
    nodeTypeBuilder.setHintsForParameter(
        'mode', {'widget': 'popup', 'constant': 'True', 'options': sorted(modes.keys())})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions', {'conditionalVisOps': {'conditionalVisOp': 'contains', 
                                                      'conditionalVisPath': '../mode', 
                                                      'conditionalVisValue': kCompareDimensions},
                                'constant': 'True',
                                'open': 'True'})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions.xLength', {'widget': 'popup',
                                         'constant': 'True',
                                         'options': sorted(dimensionConds.keys())})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions.x', {'conditionalVisOps': {'conditionalVisOp': 'notEqualTo', 
                                    'conditionalVisPath': '../xLength', 
                                    'conditionalVisValue': kDontCheck},
                                    'constant': 'True'})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions.yLength', {'widget': 'popup',
                                         'constant': 'True',
                                         'options': sorted(dimensionConds.keys())})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions.y', {'conditionalVisOps': {'conditionalVisOp': 'notEqualTo', 
                                    'conditionalVisPath': '../yLength', 
                                    'conditionalVisValue': kDontCheck},
                                    'constant': 'True'})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions.zLength', {'widget': 'popup',
                                         'constant': 'True',
                                         'options': sorted(dimensionConds.keys())})
    nodeTypeBuilder.setHintsForParameter(
        'dimensionConditions.z', {'conditionalVisOps': {'conditionalVisOp': 'notEqualTo', 
                                    'conditionalVisPath': '../zLength', 
                                    'conditionalVisValue': kDontCheck},
                                    'constant': 'True'})
    nodeTypeBuilder.setHintsForParameter(
        'volumeConditions', {'conditionalVisOps': {'conditionalVisOp': 'contains', 
                                                   'conditionalVisPath': '../mode', 
                                                   'conditionalVisValue': kCompareVolume},
                             'open': 'True',
                             'constant': 'True'})
    nodeTypeBuilder.setHintsForParameter(
        'volumeConditions.volume', {'widget': 'popup',
                                      'constant': 'True',
                                      'options': volumeConds.keys()})
    nodeTypeBuilder.setHintsForParameter(
        'volumeConditions.v', {'constant': 'True'})
    nodeTypeBuilder.setHintsForParameter(
        'executionMode', {'widget': 'popup', 'constant': 'True', 'options': executionModes.keys()})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildPruneBySizeOpChain)

    # Build the new node type
    nodeTypeBuilder.build()


# Register the node
registerPruneBySize()
