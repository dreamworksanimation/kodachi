# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# master material

from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute
import SharedFunc

nb = Nodes3DAPI.NodeTypeBuilder('MultiMaterialCreate')
nb.setOutputPortNames(('out', ))

gb = FnAttribute.GroupBuilder()
gb.set("topLocation", FnAttribute.StringAttribute('/root/world/lgt'))
gb.set("locations", FnAttribute.StringAttribute([]))
gb.set("order", FnAttribute.IntAttribute([]))
gb.set("material", FnAttribute.StringAttribute([])) #spotlight, pointlight, etc


def buildOpChain(self, interface):
    interface.setMinRequiredInputs(0)
    frameTime = interface.getFrameTime()
    topLocation = self.getParameter("topLocation").getValue(0)
    locationList = self.getListFromParam("locations", frameTime)
    material = self.getListFromParam("material", frameTime)
    materialEditNodeName = self.getParent().getMaterialEditNode().getName()

    # set attr: material.moonrayLightShader : SpotLight
    sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
    for i, loc in enumerate(locationList):
        locationPath = topLocation + "/" + loc
        sscb.setAttrAtLocation(locationPath, "type", FnAttribute.StringAttribute("material"))
        sscb.setAttrAtLocation(locationPath, "attributeEditor.exclusiveTo", FnAttribute.StringAttribute(self.getName()))
        sscb.setAttrAtLocation(locationPath, "attributeEditor.material.exclusiveTo", FnAttribute.StringAttribute(materialEditNodeName))

        mat = material[i]
        if mat!= "":
            if mat.startswith("../"):
                sscb.setAttrAtLocation(locationPath, "inherits", FnAttribute.StringAttribute(mat))
            else:
                sscb.setAttrAtLocation(locationPath, "material.moonrayLightShader", FnAttribute.StringAttribute(mat))
        
    interface.appendOp("StaticSceneCreate", sscb.build())


def setMaterialAtIndex(self, materialPath, index):
    self.getParameter("material").getChildByIndex(index).setValue(materialPath, 0)

def increaseArrays(self):
    for paramName in ["locations", "material", "order"]:
        param = self.getParameter(paramName)
        num = param.getNumChildren()
        param.resizeArray(num + 1)

def removeLocationAtIndex(self, index):
    # reset values..
    self.getParameter("locations").getChildByIndex(index).setValue("", 0)
    self.getParameter("material").getChildByIndex(index).setValue("",0)
    

def duplicateLocation(self, srcIndex, dstIndex, dstLocation):
    self.addLocationAtIndex(dstLocation, dstIndex)
    SharedFunc.copyParamToParam(self.getParameter("material").getChildByIndex(srcIndex), 
                                self.getParameter("material").getChildByIndex(dstIndex))


def addLocationAtIndex(self, locName, index, isChild=False):
    SharedFunc.addLocationAtIndex(self, locName, index)
    if isChild:
        self.getParameter("material").getChildByIndex(index).setValue("../", 0)

nb.setCustomMethod('increaseArrays', increaseArrays)
nb.setCustomMethod('duplicateLocation', duplicateLocation)
nb.setCustomMethod('removeLocationAtIndex', removeLocationAtIndex)
nb.setCustomMethod('setMaterialAtIndex', setMaterialAtIndex)
nb.setCustomMethod('addLocationAtIndex', addLocationAtIndex)

nb.setCustomMethod('getIndexForLocation', SharedFunc.getIndexForLocation)
nb.setCustomMethod("getListFromParam", SharedFunc.getListFromParam)
nb.setCustomMethod('renameLocation', SharedFunc.renameLocation)


nb.setBuildOpChainFnc(buildOpChain)
nb.addTransformParameters(gb)
nb.setParametersTemplateAttr(gb.build())
nb.setHintsForParameter('topLocation', {
        'widget': 'scenegraphLocation',
    })

nb.build()