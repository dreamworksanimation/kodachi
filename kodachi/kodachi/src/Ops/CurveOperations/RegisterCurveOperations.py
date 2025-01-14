# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerCurveOperations():
    """
    Registers a new CurveOperations node type using the NodeTypeBuilder utility
    class.
    """
    from Katana import Nodes3DAPI, FnAttribute
    
    def buildCurveOperationsOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeOperation}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        from Katana import FnGeolibServices

        # Parse node parameters
        CELParam          = node.getParameter('CEL')
        simplifyModeParam = node.getParameter('simplification.mode')
        simplifyParam     = node.getParameter('simplification.value')
        minCVParam        = node.getParameter('simplification.min_cv')
        densityParam      = node.getParameter('density')
        
        invertScaleParam = node.getParameter('widthScaling.invertWorldScale')
        maxWidthParam    = node.getParameter('widthScaling.maxWidthFactor')
        widthKnotsParam  = node.getParameter('widthScaling.widthFactor_Knots')
        widthFloatsParam = node.getParameter('widthScaling.widthFactor_Floats')
        widthInterpParam = node.getParameter('widthScaling.widthFactor_Interpolation')

        CEL          = CELParam.getValue(0)
        simplifyMode = simplifyModeParam.getValue(0)
        simplify     = simplifyParam.getValue(0)
        minCV        = minCVParam.getValue(0)
        density      = densityParam.getValue(0)
        invertScale  = invertScaleParam.getValue(0)
        
        maxWidth    = maxWidthParam.getValue(0)
        widthInterp = widthInterpParam.getValue(0)

        asb = FnGeolibServices.OpArgsBuilders.AttributeSet()
        asb.setCEL(FnAttribute.StringAttribute(CEL))
        asb.setAttr("curveOperations.simplificationMode", FnAttribute.StringAttribute(simplifyMode))
        asb.setAttr("curveOperations.simplification", FnAttribute.FloatAttribute(simplify))
        asb.setAttr("curveOperations.minCv", FnAttribute.IntAttribute(minCV))
        asb.setAttr("curveOperations.density", FnAttribute.FloatAttribute(density))
        asb.setAttr("curveOperations.invertWorldScale", FnAttribute.IntAttribute(invertScale))
        
        asb.setAttr("curveOperations.widthFactor.maxWidth", FnAttribute.FloatAttribute(maxWidth))
        asb.setAttr("curveOperations.widthFactor.interpolation", FnAttribute.StringAttribute(widthInterp))

        widthKnots  = widthKnotsParam.getChildren()
        widthKnotsData = [knotParam.getValue(0) for knotParam in widthKnots]
        asb.setAttr("curveOperations.widthFactor.knots", FnAttribute.FloatAttribute(widthKnotsData))
        
        widthFloats  = widthFloatsParam.getChildren()
        widthFloatsData = [floatParam.getValue(0) for floatParam in widthFloats]
        asb.setAttr("curveOperations.widthFactor.values", FnAttribute.FloatAttribute(widthFloatsData))
        
        interface.appendOp('AttributeSet', asb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('CurveOperationsEdit')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("input",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL',                  FnAttribute.StringAttribute(''))
    gb.set('density',              FnAttribute.FloatAttribute(1.0))
    gb.set('simplification.mode',  FnAttribute.StringAttribute('percent'))
    gb.set('simplification.value', FnAttribute.FloatAttribute(0.0))
    gb.set('simplification.min_cv',FnAttribute.IntAttribute(4))

    ### width scaling and related ramp attributes
    gb.set('widthScaling.invertWorldScale',          FnAttribute.IntAttribute(0))
    gb.set('widthScaling.maxWidthFactor',            FnAttribute.FloatAttribute(1))
    gb.set('widthScaling.widthFactor',               FnAttribute.IntAttribute(7))
    gb.set('widthScaling.widthFactor_Interpolation', FnAttribute.StringAttribute('linear'))
    gb.set('widthScaling.widthFactor_Knots',         FnAttribute.FloatAttribute([0, 0, 0.25, 0.5, 0.75, 1, 1]))
    gb.set('widthScaling.widthFactor_Floats',        FnAttribute.FloatAttribute([1, 1, 1, 1, 1, 1, 1]))
    ### -----------------------------------------

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})
    nodeTypeBuilder.setHintsForParameter('simplification.mode', 
                                        {'widget': 'mapper',
                                         'default': '0',
                                         'options' : { 'Percent Based' : 'percent', 'Distance Based' : 'distance' },
                                         'help': "Percent Based Reduction: Visvalingam curve reduction that reduces the number of CV's "
                                                 "by the specified percentage. The higher the value, the more CV's are removed.\n"
                                                 "Distance Based Reduction: Douglas-Peucker curve reduction that reduces the number of "
                                                 "CV's based on a provided tolerance distance. The higher the value, the more CV's are removed."
                                        })
    nodeTypeBuilder.setHintsForParameter('simplification.value', 
                                        {'widget': 'number',
                                         'default': '0',
                                         'min': '0',
                                         'slider': '1',
                                         'slidermin': '0',
                                         'slidermax': '1',
                                         'help': "Reduces CV's in a curve while preserving its shape. "
                                                 "A higher value removes more CV's, whereas zero is default and does not alter the curve."
                                        })
    nodeTypeBuilder.setHintsForParameter('simplification.min_cv', 
                                        {'widget': 'number',
                                         'default': '4',
                                         'help': "Minimum number of CV's a curve can be reduced to. This cannot go under 2, and currently "
                                                 "only affects 'Percent Based' mode."
                                        })
    nodeTypeBuilder.setHintsForParameter('density', 
                                        {'widget': 'number',
                                         'default': '1',
                                         'slider': '1',
                                         'slidermin': '0',
                                         'slidermax': '1',
                                         'help': "Percentage [0 - 1] value of a curve location's density, with 0 being no curves and "
                                                 "1 being full density."
                                        })                              

    ### width scaling and related ramp attributes
    nodeTypeBuilder.setHintsForParameter('widthScaling.invertWorldScale', 
                                        {'widget': 'checkBox',
                                         'default': '0', 
                                         'help': "Ignore world space scaling on the widths of the curve. Turning this on "
                                                 "will keep the thickness of the curve the same as the geometry is scaled up or down."
                                        })
    nodeTypeBuilder.setHintsForParameter('widthScaling.maxWidthFactor', 
                                        {'widget': 'number',
                                         'default': '1',
                                         'min': '0',
                                         'help': "Scale factor applied to widths to each curve based on the widthFactor ramp."
                                        })
    # other potential hints: minPosition, maxPosition, minValue, maxValue
    nodeTypeBuilder.setHintsForParameter('widthScaling.widthFactor', 
                                        {'widget': 'floatRamp',
                                         'help': "Scale factor of each CV widths normalized along the length of each curve. "
                                                 "The y-axis is a [0,1] factor of maxWidthFactor."
                                        })
    nodeTypeBuilder.setHintsForParameter('widthScaling.widthFactor_Interpolation', 
                                        {'widget': 'null',
                                         'default': 'linear',
                                         'options': 'linear'
                                        })
    nodeTypeBuilder.setHintsForParameter('widthScaling.widthFactor_Knots', 
                                        {'widget': 'null',
                                         'default': '0,0,0.25,0.5,0.75,1,1',
                                         'size': '7',
                                         'isDynamicArray': '1'
                                        })    
    nodeTypeBuilder.setHintsForParameter('widthScaling.widthFactor_Floats', 
                                        {'widget': 'null',
                                         'default': '1,1,1,1,1,1,1',
                                         'size': '7',
                                         'isDynamicArray': '1'
                                        })
    ### -----------------------------------------


    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildCurveOperationsOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerCurveOperations()

def RegisterCurveOperationResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute
    
    # reduces individual curve CV's; 
    # currently this op needs to come before all other curve operations as it changes the actual geometry
    # instead of using the omitList
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "CurveReductionOp", FnAttribute.GroupAttribute(), addSystemArgs=True)
    
    # primitive prune operations
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "PrimitivePruneByFrustumOp", FnAttribute.GroupAttribute(), addSystemArgs=True)
        
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "PrimitivePruneByVolumeOp", FnAttribute.GroupAttribute(), addSystemArgs=True)
    
    # omits curve CV's or whole curves
    # populates the omitList; does not change geometry
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "CurveDensityOp", FnAttribute.GroupAttribute(), addSystemArgs=True)
    
    # alters the widths of curve CV's; changes the geometry.point.width
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "CurveWidthOp", FnAttribute.GroupAttribute(), addSystemArgs=True)

RegisterCurveOperationResolvers()
