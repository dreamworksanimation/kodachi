# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import NodegraphAPI

def AddNodeReferenceParam(destNode, paramName, node):
    paramName = "node_"+paramName
    param = destNode.getParameter(paramName)
    if not param:
        param = destNode.getParameters().createChildString(paramName, '')
    
    param.setExpression('getNode(%r).getNodeName()' % node.getName())

def GetRefNode(gnode, key):
    p = gnode.getParameter('node_'+key)
    if not p:
        return None
    
    return NodegraphAPI.GetNode(p.getValue(0))