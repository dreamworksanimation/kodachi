# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

valueVisConditions = { 'conditionalVis10Left': 'conditionalVis11',
                        'conditionalVis10Op': 'or',
                        'conditionalVis10Right': 'conditionalVis12',
                        'conditionalVis11Op': 'equalTo',
                        'conditionalVis11Path': '../operation',
                        'conditionalVis11Value': 'min',
                        'conditionalVis12Left': 'conditionalVis13',
                        'conditionalVis12Op': 'or',
                        'conditionalVis12Right': 'conditionalVis14',
                        'conditionalVis13Op': 'equalTo',
                        'conditionalVis13Path': '../operation',
                        'conditionalVis13Value': 'max',
                        'conditionalVis14Left': 'conditionalVis15',
                        'conditionalVis14Op': 'or',
                        'conditionalVis14Right': 'conditionalVis16',
                        'conditionalVis15Op': 'equalTo',
                        'conditionalVis15Path': '../operation',
                        'conditionalVis15Value': 'copysign',
                        'conditionalVis16Op': 'equalTo',
                        'conditionalVis16Path': '../operation',
                        'conditionalVis16Value': 'fmod',
                        'conditionalVis1Op': 'equalTo',
                        'conditionalVis1Path': '../operation',
                        'conditionalVis1Value': 'add',
                        'conditionalVis2Left': 'conditionalVis3',
                        'conditionalVis2Op': 'or',
                        'conditionalVis2Right': 'conditionalVis4',
                        'conditionalVis3Op': 'equalTo',
                        'conditionalVis3Path': '../operation',
                        'conditionalVis3Value': 'subtract',
                        'conditionalVis4Left': 'conditionalVis5',
                        'conditionalVis4Op': 'or',
                        'conditionalVis4Right': 'conditionalVis6',
                        'conditionalVis5Op': 'equalTo',
                        'conditionalVis5Path': '../operation',
                        'conditionalVis5Value': 'multiply',
                        'conditionalVis6Left': 'conditionalVis7',
                        'conditionalVis6Op': 'or',
                        'conditionalVis6Right': 'conditionalVis8',
                        'conditionalVis7Op': 'equalTo',
                        'conditionalVis7Path': '../operation',
                        'conditionalVis7Value': 'divide',
                        'conditionalVis8Left': 'conditionalVis9',
                        'conditionalVis8Op': 'or',
                        'conditionalVis8Right': 'conditionalVis10',
                        'conditionalVis9Op': 'equalTo',
                        'conditionalVis9Path': '../operation',
                        'conditionalVis9Value': 'pow',
                        'conditionalVisLeft': 'conditionalVis1',
                        'conditionalVisOp': 'or',
                        'conditionalVisRight': 'conditionalVis2' }

boundsVisConditions = { 'conditionalVis1Path': '../operation',
                        'conditionalVis3Op': 'equalTo',
                        'conditionalVis2Op': 'or',
                        'conditionalVis4Value': 'random',
                        'conditionalVis1Value': 'clamp',
                        'conditionalVisOp': 'or',
                        'conditionalVis4Path': '../operation',
                        'conditionalVis1Op': 'equalTo',
                        'conditionalVisLeft': 'conditionalVis1',
                        'conditionalVis2Right': 'conditionalVis4',
                        'conditionalVisRight': 'conditionalVis2',
                        'conditionalVis3Value': 'lerp',
                        'conditionalVis4Op': 'equalTo',
                        'conditionalVis3Path': '../operation',
                        'conditionalVis2Left': 'conditionalVis3' }

min_max_visConditions = { 'conditionalVis1Path': '../operation',
                            'conditionalVis3Op': 'equalTo',
                            'conditionalVis2Op': 'or',
                            'conditionalVis4Value': 'softcfit',
                            'conditionalVis1Value': 'fit',
                            'conditionalVisOp': 'or',
                            'conditionalVis4Path': '../operation',
                            'conditionalVis1Op': 'equalTo',
                            'conditionalVisLeft': 'conditionalVis1',
                            'conditionalVis2Right': 'conditionalVis4',
                            'conditionalVisRight': 'conditionalVis2',
                            'conditionalVis3Value': 'cfit',
                            'conditionalVis4Op': 'equalTo',
                            'conditionalVis3Path': '../operation',
                            'conditionalVis2Left': 'conditionalVis3' }

retimeVisConditions = { 'conditionalVisOp': 'equalTo',
                        'conditionalVisPath': '../operation',
                        'conditionalVisValue': 'retime' }

holdModeVisConditions = { 'conditionalVisOp': 'equalTo',
                          'conditionalVisPath': '../operation',
                          'conditionalVisValue': 'retime' }

def registerAttributeOperation():
    """
    Registers a new AttributeOperation node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    def buildAttributeOperationOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeOperation}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        CELParam        = node.getParameter('CEL')
        attrNameParam   = node.getParameter('attributeName')
        operationParam  = node.getParameter('operation')
        valueParam      = node.getParameter('value')
        convertToParam  = node.getParameter('convert_to')
        copyToParam     = node.getParameter('copy_to')
        modeParam       = node.getParameter('mode')
        
        lowerBoundParam = node.getParameter('lower_bound')
        upperBoundParam = node.getParameter('upper_bound')
        t_Param         = node.getParameter('t')
        autoSeedParam   = node.getParameter('auto_seed')
        seedParam       = node.getParameter('seed')
        
        dimensionsParam = node.getParameter('dimensions')
        x_Param         = node.getParameter('x')
        y_Param         = node.getParameter('y')
        z_Param         = node.getParameter('z')
        w_Param         = node.getParameter('w')
        
        oldMinParam     = node.getParameter('old_min')
        oldMaxParam     = node.getParameter('old_max')
        newMinParam     = node.getParameter('new_min')
        newMaxParam     = node.getParameter('new_max')
        
        frameParam       = node.getParameter('frame')
        startParam       = node.getParameter('start')
        endParam         = node.getParameter('end')
        holdModeInParam  = node.getParameter('hold_mode_in')
        holdModeOutParam = node.getParameter('hold_mode_out')
                
        # Based on initial feedback from the KUG, we always want to cook the
        # daps. The op defaults to true, so for now we will do nothing
        #cookDapsParam = node.getParameter('cookDaps')
 
        CEL        = CELParam.getValue(0)
        attrName   = attrNameParam.getValue(0)
        operation  = operationParam.getValue(0)
        value      = valueParam.getValue(0)
        convertTo  = convertToParam.getValue(0)
        copyTo     = copyToParam.getValue(0)
        mode       = modeParam.getValue(0)
        nodeName   = node.getName()
        
        lowerBound  = lowerBoundParam.getValue(0)
        upperBound  = upperBoundParam.getValue(0)
        t_val       = t_Param.getValue(0)
        autoSeedVal = autoSeedParam.getValue(0)
        seedVal     = seedParam.getValue(0)
        
        dimensionsVal = dimensionsParam.getValue(0)
        x_val         = x_Param.getValue(0)
        y_val         = y_Param.getValue(0)
        z_val         = z_Param.getValue(0)
        w_val         = w_Param.getValue(0)
        
        oldMin     = oldMinParam.getValue(0)
        oldMax     = oldMaxParam.getValue(0)
        newMin     = newMinParam.getValue(0)
        newMax     = newMaxParam.getValue(0)
        
        frameVal       = frameParam.getValue(0)
        startVal       = startParam.getValue(0)
        endVal         = endParam.getValue(0)
        holdModeInVal  = holdModeInParam.getValue(0)
        holdModeOutVal = holdModeOutParam.getValue(0)
        
        gb = FnAttribute.GroupBuilder()

        # MeshSubdivide Op
        interface.addOpSystemArgs(gb)
        gb.set('CEL', FnAttribute.StringAttribute(CEL))
        gb.set('attributeName', FnAttribute.StringAttribute(attrName))
        gb.set('operation', FnAttribute.StringAttribute(operation))
        gb.set('value', FnAttribute.DoubleAttribute(value))
        gb.set('convert_to', FnAttribute.StringAttribute(convertTo))
        gb.set('copy_to', FnAttribute.StringAttribute(copyTo))
        gb.set('mode', FnAttribute.IntAttribute(mode))
        gb.set('nodeName', FnAttribute.StringAttribute(nodeName))
        
        gb.set('lower_bound', FnAttribute.DoubleAttribute(lowerBound))
        gb.set('upper_bound', FnAttribute.DoubleAttribute(upperBound))
        gb.set('t', FnAttribute.DoubleAttribute(t_val))
        gb.set('auto_seed', FnAttribute.IntAttribute(autoSeedVal))
        gb.set('seed', FnAttribute.IntAttribute(seedVal))
        
        gb.set('dimensions', FnAttribute.IntAttribute(dimensionsVal))
        gb.set('x', FnAttribute.DoubleAttribute(x_val))
        gb.set('y', FnAttribute.DoubleAttribute(y_val))
        gb.set('z', FnAttribute.DoubleAttribute(z_val))
        gb.set('w', FnAttribute.DoubleAttribute(w_val))
        
        gb.set('old_min', FnAttribute.DoubleAttribute(oldMin))
        gb.set('old_max', FnAttribute.DoubleAttribute(oldMax))
        gb.set('new_min', FnAttribute.DoubleAttribute(newMin))
        gb.set('new_max', FnAttribute.DoubleAttribute(newMax))
        
        gb.set('frame', FnAttribute.DoubleAttribute(frameVal))
        gb.set('start', FnAttribute.DoubleAttribute(startVal))
        gb.set('end', FnAttribute.DoubleAttribute(endVal))
        gb.set('hold_mode_in', FnAttribute.DoubleAttribute(holdModeInVal))
        gb.set('hold_mode_out', FnAttribute.DoubleAttribute(holdModeOutVal))

        interface.appendOp('AttributeOperation', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('AttributeOperation')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute(''))
    gb.set('attributeName', FnAttribute.StringAttribute(''))
    gb.set('operation', FnAttribute.StringAttribute('multiply'))
    gb.set('value', FnAttribute.DoubleAttribute(0))
    gb.set('convert_to', FnAttribute.StringAttribute('int'))
    gb.set('copy_to', FnAttribute.StringAttribute(''))
    gb.set('mode', FnAttribute.IntAttribute(0))
    
    gb.set('lower_bound', FnAttribute.DoubleAttribute(0))
    gb.set('upper_bound', FnAttribute.DoubleAttribute(1))
    gb.set('t', FnAttribute.DoubleAttribute(0))
    gb.set('auto_seed', FnAttribute.IntAttribute(1))
    gb.set('seed', FnAttribute.IntAttribute(0))
    
    gb.set('dimensions', FnAttribute.IntAttribute(1))
    gb.set('x', FnAttribute.DoubleAttribute(0))
    gb.set('y', FnAttribute.DoubleAttribute(0))
    gb.set('z', FnAttribute.DoubleAttribute(0))
    gb.set('w', FnAttribute.DoubleAttribute(0))
    
    gb.set('old_min', FnAttribute.DoubleAttribute(0))
    gb.set('old_max', FnAttribute.DoubleAttribute(1))
    gb.set('new_min', FnAttribute.DoubleAttribute(0))
    gb.set('new_max', FnAttribute.DoubleAttribute(1))
    
    gb.set('frame', FnAttribute.DoubleAttribute(0))
    gb.set('start', FnAttribute.DoubleAttribute(0))
    gb.set('end', FnAttribute.DoubleAttribute(1))
    gb.set('hold_mode_in', FnAttribute.IntAttribute(0))
    gb.set('hold_mode_out', FnAttribute.IntAttribute(0))
    

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})
    
    nodeTypeBuilder.setHintsForParameter('operation', {
                                         'widget': 'popup',
                                         'constant': 'True',
                                         'options' : [ # binary operations
                                                       'add', 
                                                       'subtract', 
                                                       'multiply',
                                                       'divide',
                                                       'pow',
                                                       'min',
                                                       'max',
                                                       'copysign',
                                                       'fmod',
                                                       # unary operations
                                                       'abs',
                                                       'negate',
                                                       'sqrt',
                                                       'cos',
                                                       'sin',
                                                       'tan',
                                                       'acos',
                                                       'asin',
                                                       'atan',
                                                       'ceil',
                                                       'floor',
                                                       'round',
                                                       'trunc',
                                                       'exp',
                                                       'expm1',
                                                       'exp2',
                                                       'log',
                                                       'log1p',
                                                       'log2',
                                                       'log10',
                                                       # type conversion
                                                       'convert',
                                                       # copy
                                                       'copy',
                                                       # ExpressionMath operations
                                                       'clamp',
                                                       'lerp',
                                                       'smoothstep',                                                       
                                                       'fit',
                                                       'cfit',
                                                       'softcfit',
                                                       'retime',
                                                       'random',
                                                       'noise',
                                                       'signed_noise',
                                                       ],
                                         'help': "Let x = the value of the attribute specified by 'attributeName'<br><br>"
                                                 "Binary Operations (require the 'value' attribute):<br>"
                                                 "add     : x + value<br>"
                                                 "subtract: x - value<br>"
                                                 "multiply: x * value<br>"
                                                 "divide  : x / value<br>"
                                                 "pow     : x to the power of value<br>"
                                                 "min     : minimum of x and value<br>"
                                                 "max     : maximum of x and value<br>"
                                                 "copysign: magnitude of x and sign of value<br>"
                                                 "fmod    : the remainder of x / value <br>"
                                                 
                                                 "<br>Unary Operations:<br>"
                                                 "abs     : absolute value of x<br>"
                                                 "negate  : -x<br>"
                                                 "sqrt    : square root of x<br>"
                                                 "cos     : cosine of x<br>"
                                                 "sin     : sine of x<br>"
                                                 "tan     : tangent of x<br>"
                                                 "acos    : arccosine of x<br>"
                                                 "asin    : arcsine of x<br>"
                                                 "atan    : arctangent of x<br>"
                                                 "ceil    : the smallest integer value not less than x<br>"
                                                 "floor   : the largest integer value not greater than x<br>"
                                                 "round   : the nearest integer value to x<br>, rounding halfway cases away from 0"
                                                 "trunc   : the nearest integer not greater in magnitude than x<br>"
                                                 "exp     : e raised to the power of x<br>"
                                                 "expm1   : e raised to the power of x, minus 1. More accurate that exp(x)-1 when x is close to 0<br>"
                                                 "exp2    : 2 raised to the power of x<br>"
                                                 "log     : natural (base-e) logarithm of x<br>"
                                                 "log1p   : natural logarithm of 1 plus x. More accurate than log(1+x) when x is close to 0<br>"
                                                 "log2    : base 2 logarithm of x<br>"
                                                 "log10   : base 10 logarithm of x<br>"
                                                 
                                                 "<br>Math Expressions:<br>"
                                                 "clamp        : Clamp the value between lower_bound and upper_bound <br>"
                                                 "lerp         : Linearly interpolate between lower_bound and upper_bound using the specified t <br>"
                                                 "smoothstep   : Compute a smoothstep (ease in, ease out) version of t: [0,1] <br>"
                                                 "fit          : Returns a number between new_min and new_max, which is relative to the value in the range between old_min and old_max. <br>"
                                                 "cfit         : Same as fit, but clamps to new borders. <br>"
                                                 "softcfit     : Like regular cfit, only softer, specifically, uses SmoothStep to ease in and out of the fit. <br>"
                                                 "retime       : Options: <br>"
                                                 "                   freeze: Hold the first/last frame of the sequence. 1111 1234 4444 <br>"
                                                 "                   repeat: Repeat the sequence.                       1234 1234 1234 <br>"
                                                 "                   mirror: Mirror the sequence;                       3432 1234 3212<br>"
                                                 "random       : Random value between lower_bound and upper_bound; seed is optional. <br>"
                                                 "noise        : Improved Perlin noise (Siggraph 2002); range [0, 1] <br>"
                                                 "signed_noise : Signed Improved Perlin noise (Siggraph 2002); range [-1, 1] <br>"
                                                 
                                                 "<br>Other Operations:<br>"
                                                 "convert : attempt to convert the attribute to the specified type<br>"
                                                 "copy    : copy the attribute to the specified destination<br>"
                                                 })

    # only display 'value' for binary operations
    # copied from creating a userParameter and getting the hint string
    nodeTypeBuilder.setHintsForParameter('value', { 'conditionalVisOps' : valueVisConditions })
    nodeTypeBuilder.setHintsForParameter('lower_bound', { 'conditionalVisOps': boundsVisConditions })
    nodeTypeBuilder.setHintsForParameter('upper_bound', { 'conditionalVisOps': boundsVisConditions })
    nodeTypeBuilder.setHintsForParameter('auto_seed', {
                                            'widget': 'boolean',
                                            'conditionalVisOps':
                                            { 'conditionalVisOp': 'equalTo',
                                              'conditionalVisPath': '../operation',
                                              'conditionalVisValue': 'random'}})
    nodeTypeBuilder.setHintsForParameter('seed', {
                                            'conditionalVisOps':
                                                { 'conditionalVis1Path': '../operation',
                                                  'conditionalVis2Op': 'equalTo',
                                                  'conditionalVis1Value': 'random',
                                                  'conditionalVisOp': 'and',
                                                  'conditionalVis1Op': 'equalTo',
                                                  'conditionalVisLeft': 'conditionalVis1',
                                                  'conditionalVisRight': 'conditionalVis2',
                                                  'conditionalVis2Path': '../auto_seed',
                                                  'conditionalVis2Value': '0'}})
    
    nodeTypeBuilder.setHintsForParameter('t', {
                                         'conditionalVisOps':
                                            { 'conditionalVisOp': 'equalTo',
                                              'conditionalVisPath': '../operation',
                                              'conditionalVisValue': 'lerp'}})
    
    nodeTypeBuilder.setHintsForParameter('old_min', { 'conditionalVisOps': min_max_visConditions })
    nodeTypeBuilder.setHintsForParameter('old_max', { 'conditionalVisOps': min_max_visConditions })
    nodeTypeBuilder.setHintsForParameter('new_min', { 'conditionalVisOps': min_max_visConditions })    
    nodeTypeBuilder.setHintsForParameter('new_max', { 'conditionalVisOps': min_max_visConditions })
    
    nodeTypeBuilder.setHintsForParameter('frame', { 'conditionalVisOps': retimeVisConditions })
    nodeTypeBuilder.setHintsForParameter('start', { 'conditionalVisOps': retimeVisConditions })
    nodeTypeBuilder.setHintsForParameter('end', { 'conditionalVisOps': retimeVisConditions})
    
    nodeTypeBuilder.setHintsForParameter('hold_mode_in', {
                                            'widget': 'mapper',
                                            'constant': 'True',
                                            'options': {
                                                'freeze' : 0,
                                                'repeat' : 1,
                                                'mirror' : 2 },
                                            'conditionalVisOps': holdModeVisConditions })
    
    nodeTypeBuilder.setHintsForParameter('hold_mode_out', {
                                            'widget': 'mapper',
                                            'constant': 'True',
                                            'options': {
                                                'freeze' : 0,
                                                'repeat' : 1,
                                                'mirror' : 2 },
                                            'conditionalVisOps': holdModeVisConditions })
    
    nodeTypeBuilder.setHintsForParameter('dimensions',
                                        { 'widget': 'popup',
                                          'conditionalVisOps':
                                           { 'conditionalVis1Path': '../operation',
                                             'conditionalVis2Op': 'equalTo',
                                             'conditionalVis1Value': 'noise',
                                             'conditionalVisOp': 'or',
                                             'conditionalVis1Op': 'equalTo',
                                             'conditionalVisLeft': 'conditionalVis1',
                                             'conditionalVisRight': 'conditionalVis2',
                                             'conditionalVis2Path': '../operation',
                                             'conditionalVis2Value': 'signed_noise' },
                                          'options': ['1.0', '2.0', '3.0', '4.0'],
                                        })
    
    nodeTypeBuilder.setHintsForParameter('x',
                                        { 'conditionalVisOps':
                                          { 'conditionalVis3Op': 'equalTo',
                                            'conditionalVis1Right': 'conditionalVis3',
                                            'conditionalVis1Left': 'conditionalVis2',
                                            'conditionalVis2Op': 'equalTo',
                                            'conditionalVis4Value': '1',
                                            'conditionalVisOp': 'and',
                                            'conditionalVis4Path': '../dimensions',
                                            'conditionalVisRight': 'conditionalVis4',
                                            'conditionalVis1Op': 'or',
                                            'conditionalVis2Value': 'noise',
                                            'conditionalVisLeft': 'conditionalVis1',
                                            'conditionalVis3Value': 'signed_noise',
                                            'conditionalVis2Path': '../operation',
                                            'conditionalVis3Path': '../operation',
                                            'conditionalVis4Op': 'greaterThanOrEqualTo' }})
    
    nodeTypeBuilder.setHintsForParameter('y',
                                         { 'conditionalVisOps':
                                            { 'conditionalVis3Op': 'equalTo',
                                              'conditionalVis1Right': 'conditionalVis3',
                                              'conditionalVis1Left': 'conditionalVis2',
                                              'conditionalVis2Op': 'equalTo',
                                              'conditionalVis4Value': '2',
                                              'conditionalVisOp': 'and',
                                              'conditionalVis4Path': '../dimensions',
                                              'conditionalVisRight': 'conditionalVis4',
                                              'conditionalVis1Op': 'or',
                                              'conditionalVis2Value': 'noise',
                                              'conditionalVisLeft': 'conditionalVis1',
                                              'conditionalVis3Value': 'signed_noise',
                                              'conditionalVis2Path': '../operation',
                                              'conditionalVis3Path': '../operation',
                                              'conditionalVis4Op': 'greaterThanOrEqualTo' }})
    
    nodeTypeBuilder.setHintsForParameter('z', {'conditionalVisOps': {'conditionalVis3Op': 'equalTo',
                                                'conditionalVis1Right': 'conditionalVis3',
                                                'conditionalVis1Left': 'conditionalVis2',
                                                'conditionalVis2Op': 'equalTo',
                                                'conditionalVis4Value': '3',
                                                'conditionalVisOp': 'and',
                                                'conditionalVis4Path': '../dimensions',
                                                'conditionalVisRight': 'conditionalVis4',
                                                'conditionalVis1Op': 'or',
                                                'conditionalVis2Value': 'noise',
                                                'conditionalVisLeft': 'conditionalVis1',
                                                'conditionalVis3Value': 'signed_noise',
                                                'conditionalVis2Path': '../operation',
                                                'conditionalVis3Path': '../operation',
                                                'conditionalVis4Op': 'greaterThanOrEqualTo'}})
    
    nodeTypeBuilder.setHintsForParameter('w', {'conditionalVisOps': {'conditionalVis3Op': 'equalTo',
                                                'conditionalVis1Right': 'conditionalVis3',
                                                'conditionalVis1Left': 'conditionalVis2',
                                                'conditionalVis2Op': 'equalTo',
                                                'conditionalVis4Value': '4',
                                                'conditionalVisOp': 'and',
                                                'conditionalVis4Path': '../dimensions',
                                                'conditionalVisRight': 'conditionalVis4',
                                                'conditionalVis1Op': 'or',
                                                'conditionalVis2Value': 'noise',
                                                'conditionalVisLeft': 'conditionalVis1',
                                                'conditionalVis3Value': 'signed_noise',
                                                'conditionalVis2Path': '../operation',
                                                'conditionalVis3Path': '../operation',
                                                'conditionalVis4Op': 'equalTo'}})
    
    nodeTypeBuilder.setHintsForParameter('convert_to', {
                                         'widget': 'popup',
                                         'constant': 'True',
                                         'options' : [ 'int', 
                                                       'float', 
                                                       'double',
                                                       'string'],
                                         'conditionalVisOps' : {
                                            'conditionalVisOp': 'equalTo',
                                            'conditionalVisPath': '../operation',
                                            'conditionalVisValue': 'convert' }})
                                            
    nodeTypeBuilder.setHintsForParameter('copy_to', {
                                             'conditionalVisOps' : {
                                                'conditionalVisOp': 'equalTo',
                                                'conditionalVisPath': '../operation',
                                                'conditionalVisValue': 'copy' }})
    
    nodeTypeBuilder.setHintsForParameter('mode', {'widget': 'mapper',
                                                  'constant': 'True',
                                                  'options': {'immediate' : 0, 'deferred' : 1}})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildAttributeOperationOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerAttributeOperation()
