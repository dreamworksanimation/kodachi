# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Utils, NodegraphAPI, UI4
from platform import node


def onRampParameterValueFinalized(eventType, eventID, node, param, *args, **kwargs):
    parametersPath = param.getFullName()
    nodeName = node.getName()
    parametersPath = parametersPath.replace(nodeName + ".", "")
    
    # Make sure it's a RampMap by checking for the ramp.value 
    # or ramp.enable parameter
    if "ramp.value" in parametersPath:
        parametersPath = parametersPath[:parametersPath.find("ramp.value")]

        # enable the interpolations if the ramp value changed
        enableParam = node.getParameter(parametersPath + "interpolations.enable")
        enableParam.setValue(1, 0)
        
        interpolationsParam = node.getParameter(parametersPath + "interpolations.value")
        # ramp.value is the number of knots in the ramp.
        # Set the size of the interpolations to that same value
        interpolationsParam.resizeArray(int(param.getValue(0)))
        
    # Disable the interpolations if the ramp is disabled    
    if "ramp.enable" in parametersPath:
        # Get the path on the node to the ramp parameters 
        parametersPath = parametersPath[:parametersPath.find("ramp.enable")]

        enableParam = node.getParameter(parametersPath + "interpolations.enable")
        enableParam.setValue(param.getValue(0), 0)

       
def registerRampParameterValueFinalizedEvents():
    """
    Register event handlers.
    """
    Utils.EventModule.RegisterEventHandler(
        onRampParameterValueFinalized, 'parameter_finalizeValue')

registerRampParameterValueFinalizedEvents()