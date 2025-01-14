# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import os
from Katana import NodegraphAPI


def copyParamToParam(srcParam, dstParam):
    if srcParam.isAnimated():
        dstParam.setCurve(srcParam.getCurve())
        dstParam.setAutoKey(srcParam.getAutoKey())
        
    elif srcParam.isExpression():
        expr = srcParam.getExpression()
        dstParam.setExpression(expr)
    else:
        dstParam.setValue(srcParam.getValue(0), 0)


def initXformParam(node):
    for paramName in ["translate", "rotate", "scale"]:
        param = node.getParameter(paramName)
        num = param.getNumChildren()
        param.resizeArray(num + 3)
        if paramName == "scale":
            param.getChildByIndex(num).setValue(1, 0)
            param.getChildByIndex(num+1).setValue(1, 0)
            param.getChildByIndex(num+2).setValue(1, 0)

def addLocationAtIndex(node, locName, index):
    # find available location first before resizing array..
    param = node.getParameter("locations")

    if index == param.getNumChildren():
        node.increaseArrays()
    
    node.getParameter("locations").getChildByIndex(index).setValue(locName, 0)

def renameLocation(node, index,  newLocation):
    node.getParameter("locations").getChildByIndex(index).setValue(newLocation, 0)

def getListFromParam(node, paramName, frameTime):
    param = node.getParameter(paramName)
    result = []
    for c in param.getChildren():
        result.append(c.getValue(frameTime))
    
    return result

def loadXformDataFromIndex(node, index):
    for paramName in ["translate", "rotate", "scale"]:
        param = node.getParameter(paramName)
        dstParam = node.getParameter('transform.'+paramName)
        copyParamToParam(param.getChildByIndex(index*3), dstParam.getChildByIndex(0))
        copyParamToParam(param.getChildByIndex(index*3+1), dstParam.getChildByIndex(1))
        copyParamToParam(param.getChildByIndex(index*3+2), dstParam.getChildByIndex(2))

def getIndexForLocation(node, location):
    for i, child in  enumerate(node.getParameter("locations").getChildren()):
        #print i
        #print child.getValue(0)
        if child.getValue(0) == location:
            return i

def setXformValuesAtIndex(node, paramName, values, index):
    param = node.getParameter(paramName)
    frameTime = NodegraphAPI.GetCurrentTime()
    param.getChildByIndex(index*3).setValue(values[0], frameTime)
    param.getChildByIndex(index*3+1).setValue(values[1], frameTime)
    param.getChildByIndex(index*3+2).setValue(values[2], frameTime)

def getRelativePathToRoot(path, rootPath):
    if path == "":
        return path
    if path == rootPath:
        return ""
    return os.path.relpath(path, rootPath)

def setOverrrideXform(node, locationIndex, index, attrName, attrData, time):
    paramName = attrName.split(".")[-1]
    if paramName == "rotateX":
        node.getParameter("rotate").getChildByIndex(3*locationIndex+0).setValue(attrData[0], time)
    elif paramName == "rotateY":
        node.getParameter("rotate").getChildByIndex(3*locationIndex+1).setValue(attrData[0], time)
    elif paramName == "rotateZ":
        node.getParameter("rotate").getChildByIndex(3*locationIndex+2).setValue(attrData[0], time)
    else:
        node.getParameter(paramName).getChildByIndex(3*locationIndex+index).setValue(attrData[0], time)