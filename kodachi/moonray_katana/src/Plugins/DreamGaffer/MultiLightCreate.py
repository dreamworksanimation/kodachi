# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute, Decorators

import SharedFunc


nb = Nodes3DAPI.NodeTypeBuilder('MultiLightCreate')

gb = FnAttribute.GroupBuilder()
gb.set("topLocation", FnAttribute.StringAttribute('/root/world/lgt'))
gb.set("locations", FnAttribute.StringAttribute([]))
gb.set("order", FnAttribute.IntAttribute([])) # to be implemented
gb.set("translate", FnAttribute.FloatAttribute([]))
gb.set("rotate", FnAttribute.FloatAttribute([]))
gb.set("scale", FnAttribute.FloatAttribute([]))
gb.set("material", FnAttribute.StringAttribute([])) #if it starts from /root/ then this is a master material assign


def buildOpChain(self, interface):
    interface.setMinRequiredInputs(0)
    frameTime = interface.getFrameTime()
    topLocation = self.getParameter("topLocation").getValue(0)
    locationList = self.getListFromParam("locations", frameTime)
    trans = self.getListFromParam("translate", frameTime)
    rotate = self.getListFromParam("rotate", frameTime)
    scale = self.getListFromParam("scale", frameTime)
    material = self.getListFromParam("material", frameTime)
    materialEditNodeName = self.getParent().getMaterialEditNode().getName()
    # set attr: material.moonrayLightShader : SpotLight
    sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
    locationPathList = []
    for i, loc in enumerate(locationList):
        locationPath = topLocation + "/" + loc
        locationPathList.append(locationPath)
        sscb.setAttrAtLocation(locationPath, "xform.interactive.translate", FnAttribute.DoubleAttribute([trans[i*3], trans[i*3+1], trans[i*3+2]]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.rotateZ", FnAttribute.DoubleAttribute([rotate[i*3+2], 0, 0, 1]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.rotateY", FnAttribute.DoubleAttribute([rotate[i*3+1], 0, 1, 0]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.rotateX", FnAttribute.DoubleAttribute([rotate[i*3], 1, 0, 0]))
        sscb.setAttrAtLocation(locationPath, "xform.interactive.scale", FnAttribute.DoubleAttribute([scale[i*3], scale[i*3+1], scale[i*3+2]]))
        sscb.setAttrAtLocation(locationPath, "type", FnAttribute.StringAttribute("light"))
        sscb.setAttrAtLocation(locationPath, "attributeEditor.xform.exclusiveTo", FnAttribute.StringAttribute(self.getName()))
        sscb.setAttrAtLocation(locationPath, "attributeEditor.material.exclusiveTo", FnAttribute.StringAttribute(materialEditNodeName))
        sscb.setAttrAtLocation(locationPath, "geometry.previewColor", FnAttribute.FloatAttribute([1, 1, 0]))
        mat = material[i]
        if mat!= "":
            if mat.startswith("../"):
                sscb.setAttrAtLocation(locationPath, "inherits", FnAttribute.StringAttribute(mat))
            else:
                sscb.setAttrAtLocation(locationPath, "material.moonrayLightShader", FnAttribute.StringAttribute(mat))

        #sscb.setAttrAtLocation(locationPath, "geometry.fov", FnAttribute.DoubleAttribute(70))
        #sscb.setAttrAtLocation(locationPath, "geometry.near", FnAttribute.DmultoubleAttribute(0.1))
        #sscb.setAttrAtLocation(locationPath, "geometry.far", FnAttribute.DoubleAttribute(100000))
        
    interface.appendOp("StaticSceneCreate", sscb.build())


    if locationPathList:
        argsGb = FnAttribute.GroupBuilder()
        argsGb.set('locationPaths', FnAttribute.StringAttribute(locationPathList, 1))
        argsGb.set('enable', FnAttribute.IntAttribute(1))
        interface.appendOp('LightListEdit', argsGb.build())


def getParamAtIndex(self, paramName, index):
    "return the parameter"

def setMaterialAtIndex(self, materialPath, index):
    try:
        self.getParameter("material").getChildByIndex(index).setValue(materialPath, 0)
    except:
        import traceback
        traceback.print_exc()
        print materialPath

def setParamAtIndex(self, param, index):
    #name = param.getFullName()
    name = param.getName()
    axis = ['x', 'y', 'z']
    if name in axis:
        paramName = param.getParent().getName()
        dstParam = self.getParameter(paramName).getChildByIndex(index*3+axis.index(name))
        SharedFunc.copyParamToParam(param, dstParam)

def increaseArrays(self):
    for paramName in ["locations", "material", "order"]:
        param = self.getParameter(paramName)
        num = param.getNumChildren()
        param.resizeArray(num + 1)
    
    self.initXformParam()


def removeLocationAtIndex(self, index):
    # reset values..
    self.getParameter("locations").getChildByIndex(index).setValue("", 0)
    self.getParameter("material").getChildByIndex(index).setValue("",0)
    
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
    SharedFunc.copyParamToParam(self.getParameter("material").getChildByIndex(srcIndex), 
                                self.getParameter("material").getChildByIndex(dstIndex))
    
    for paramName in ["translate", "rotate", "scale"]:
        param = self.getParameter(paramName)
        SharedFunc.copyParamToParam(param.getChildByIndex(srcIndex*3), param.getChildByIndex(dstIndex*3))
        SharedFunc.copyParamToParam(param.getChildByIndex(srcIndex*3+1), param.getChildByIndex(dstIndex*3+1))
        SharedFunc.copyParamToParam(param.getChildByIndex(srcIndex*3+2), param.getChildByIndex(dstIndex*3+2))


@Decorators.undogroup('DreamGaffer Add light')
def addLocationAtIndex(self, locName, index, isChild=False):
    SharedFunc.addLocationAtIndex(self, locName, index)
    if isChild:
        self.getParameter("material").getChildByIndex(index).setValue("../", 0)

def setInteractiveTransformFlag(self, path, time):
    pass

def canOverride(self, attrName):
    if attrName.startswith('xform.interactive.') or attrName.startswith("material"):
        return True
    return False

def setOverride(self, path, attrName, time, attrData, index=None, *args, **kwargs):
    '''
    print path
    print attrName
    print time, attrData, index, args, kwargs
    '''
    if not attrData:
        return
    rootPath = self.getParameter("topLocation").getValue(0)
    location = SharedFunc.getRelativePathToRoot(path, rootPath)
    locationIndex = self.getIndexForLocation(location)
    if attrName.startswith("xform.interactive"):
        SharedFunc.setOverrrideXform(self, locationIndex, index, attrName, attrData, time)
    if attrName.startswith("material"):
        print attrName



#nb.setCustomMethod('setInteractiveTransform', setInteractiveTransform)
nb.setCustomMethod('setInteractiveTransformFlag', setInteractiveTransformFlag)

nb.setCustomMethod('increaseArrays', increaseArrays)
nb.setCustomMethod('duplicateLocation', duplicateLocation)
nb.setCustomMethod('removeLocationAtIndex', removeLocationAtIndex)
nb.setCustomMethod('setParamAtIndex', setParamAtIndex)
nb.setCustomMethod('setMaterialAtIndex', setMaterialAtIndex)
nb.setCustomMethod('canOverride', canOverride)
nb.setCustomMethod('setOverride', setOverride)
nb.setCustomMethod('addLocationAtIndex', addLocationAtIndex)

nb.setCustomMethod('setXformValuesAtIndex', SharedFunc.setXformValuesAtIndex)
nb.setCustomMethod('getIndexForLocation', SharedFunc.getIndexForLocation)
nb.setCustomMethod('loadXformDataFromIndex', SharedFunc.loadXformDataFromIndex)
nb.setCustomMethod("getListFromParam", SharedFunc.getListFromParam)
nb.setCustomMethod('initXformParam', SharedFunc.initXformParam)
nb.setCustomMethod('renameLocation', SharedFunc.renameLocation)

nb.setBuildOpChainFnc(buildOpChain)
nb.addTransformParameters(gb)
nb.setParametersTemplateAttr(gb.build())
nb.setHintsForParameter('topLocation', {
        'widget': 'scenegraphLocation',
    })

nb.build()