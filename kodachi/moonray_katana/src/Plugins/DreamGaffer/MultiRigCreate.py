# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

#xform, constraint

from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute
import SharedFunc

nb = Nodes3DAPI.NodeTypeBuilder('MultiRigCreate')
nb.setOutputPortNames(('out', ))

gb = FnAttribute.GroupBuilder()
gb.set("topLocation", FnAttribute.StringAttribute('/root/world/lgt'))
gb.set("locations", FnAttribute.StringAttribute([]))
gb.set("order", FnAttribute.IntAttribute([]))
gb.set("translate", FnAttribute.FloatAttribute([]))
gb.set("rotate", FnAttribute.FloatAttribute([]))
gb.set("scale", FnAttribute.FloatAttribute([]))


def buildOpChain(self, interface):
    interface.setMinRequiredInputs(0)
    frameTime = interface.getFrameTime()
    topLocation = self.getParameter("topLocation").getValue(0)
    locationList = self.getListFromParam("locations", frameTime)
    trans = self.getListFromParam("translate", frameTime)
    rotate = self.getListFromParam("rotate", frameTime)
    scale = self.getListFromParam("scale", frameTime)

    sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
    for i, loc in enumerate(locationList):
        locationPath = topLocation + "/" + loc
        sscb.setAttrAtLocation(locationPath, "xform.interactive.translate", FnAttribute.DoubleAttribute([trans[i*3], trans[i*3+1], trans[i*3+2]]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.rotateZ", FnAttribute.DoubleAttribute([rotate[i*3+2], 0, 0, 1]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.rotateY", FnAttribute.DoubleAttribute([rotate[i*3+1], 0, 1, 0]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.rotateX", FnAttribute.DoubleAttribute([rotate[i*3], 1, 0, 0]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.scale", FnAttribute.DoubleAttribute([scale[i*3], scale[i*3+1], scale[i*3+2]]))
        sscb.setAttrAtLocation(locationPath, "type", FnAttribute.StringAttribute("rig"))
        sscb.setAttrAtLocation(locationPath, "attributeEditor.exclusiveTo", FnAttribute.StringAttribute(self.getName()))
        
    interface.appendOp("StaticSceneCreate", sscb.build())
    

def increaseArrays(self):
    for paramName in ["locations",  "order"]:
        param = self.getParameter(paramName)
        num = param.getNumChildren()
        param.resizeArray(num + 1)
    
    self.initXformParam()

def removeLocationAtIndex(self, index):
    # reset values..
    self.getParameter("locations").getChildByIndex(index).setValue("", 0)    
    for paramName in ["translate", "rotate", "scale"]:
        param = self.getParameter(paramName)
        newValue = 0
        if paramName == "scale":
            newValue = 1
        param.getChildByIndex(index*3).setValue(newValue, 0)
        param.getChildByIndex(index*3+1).setValue(newValue, 0)
        param.getChildByIndex(index*3+2).setValue(newValue, 0)

def duplicateLocation(self, srcIndex, dstIndex, dstLocation):
    self.addLocationAtIndex(dstLocation, dstIndex)
    
    for paramName in ["translate", "rotate", "scale"]:
        param = self.getParameter(paramName)
        SharedFunc.copyParamToParam(param.getChildByIndex(srcIndex*3), param.getChildByIndex(dstIndex*3))
        SharedFunc.copyParamToParam(param.getChildByIndex(srcIndex*3+1), param.getChildByIndex(dstIndex*3+1))
        SharedFunc.copyParamToParam(param.getChildByIndex(srcIndex*3+2), param.getChildByIndex(dstIndex*3+2))

def setParamAtIndex(self, param, index):
    #name = param.getFullName()
    name = param.getName()
    axis = ['x', 'y', 'z']
    if name in axis:
        paramName = param.getParent().getName()
        dstParam = self.getParameter(paramName).getChildByIndex(index*3+axis.index(name))
        SharedFunc.copyParamToParam(param, dstParam)


def setInteractiveTransformFlag(self, path, time):
    pass


def canOverride(self, attrName):
    return attrName.startswith('xform.interactive.')

def setOverride(self, path, attrName, time, attrData, index=None, *args, **kwargs):
    if not attrData:
        return
    rootPath = self.getParameter("topLocation").getValue(0)
    location = SharedFunc.getRelativePathToRoot(path, rootPath)
    locationIndex = self.getIndexForLocation(location)
    if attrName.startswith("xform.interactive"):
        SharedFunc.setOverrrideXform(self, locationIndex, index, attrName, attrData, time)

nb.setCustomMethod('setInteractiveTransformFlag', setInteractiveTransformFlag)
nb.setCustomMethod('increaseArrays', increaseArrays)
nb.setCustomMethod('duplicateLocation', duplicateLocation)
nb.setCustomMethod('removeLocationAtIndex', removeLocationAtIndex)

nb.setCustomMethod('setParamAtIndex', setParamAtIndex)
nb.setCustomMethod('canOverride', canOverride)
nb.setCustomMethod('setOverride', setOverride)

nb.setCustomMethod('setXformValuesAtIndex', SharedFunc.setXformValuesAtIndex)
nb.setCustomMethod('getIndexForLocation', SharedFunc.getIndexForLocation)
nb.setCustomMethod('loadXformDataFromIndex', SharedFunc.loadXformDataFromIndex)
nb.setCustomMethod("getListFromParam", SharedFunc.getListFromParam)
nb.setCustomMethod('initXformParam', SharedFunc.initXformParam)
nb.setCustomMethod('addLocationAtIndex', SharedFunc.addLocationAtIndex)
nb.setCustomMethod('renameLocation', SharedFunc.renameLocation)


nb.setBuildOpChainFnc(buildOpChain)
nb.addTransformParameters(gb)
nb.setParametersTemplateAttr(gb.build())

nb.setHintsForParameter('topLocation', {
        'widget': 'scenegraphLocation',
})

nb.build()