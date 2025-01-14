# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute, NodegraphAPI, Utils
import ast, copy
import SharedFunc
import xml.etree.ElementTree as ET 

nb = Nodes3DAPI.NodeTypeBuilder('MultiAttributesEdit')
nb.setInputPortNames(('in', ))

gb = FnAttribute.GroupBuilder()
gb.set("topLocation", FnAttribute.StringAttribute('/root/world/lgt'))
gb.set("edits", FnAttribute.StringAttribute(""))
gb.set("xformEdits", FnAttribute.StringAttribute("")) #this is for downstream edit only...
gb.set("muteSolo", FnAttribute.StringAttribute(""))
gb.set("users", FnAttribute.GroupAttribute())
gb.set("convertMatrix", FnAttribute.StringAttribute(""))


def buildOpChain(self, interface):
    interface.setMinRequiredInputs(1)
    frameTime = interface.getFrameTime()
    topLocation = self.getParameter("topLocation").getValue(frameTime)
    
    opType = 'MultiAttributeSetOp'
    argsGb = FnAttribute.GroupBuilder()
    editsString = self.getParameter('edits').getValue(frameTime)
    xformEditsString = self.getParameter('xformEdits').getValue(frameTime)
    muteSoloString = self.getParameter("muteSolo").getValue(frameTime)
    editDict = {}
    muteSoloDict = {}
    xformEditDict = {}
    soloedLocations = []

    if editsString != "":   
        editDict = ast.literal_eval(editsString)

    if xformEditsString != "":
        xformEditDict = ast.literal_eval(xformEditsString)    
    
    if muteSoloString != "":
        muteSoloDict = ast.literal_eval(muteSoloString)
        for location, info in muteSoloDict.iteritems():
            if info.get("info.light.solo") and info.get("info.light.solo").get('value', 0) == 1:
                soloedLocations.append(topLocation+"/"+ location)
            editInfo = editDict.get(location)
            if editInfo:
                editInfo.update(info)
            else:
                editDict[location] = info

    users = self.getParameter("users")
    children = users.getChildren()

    if children:
        for child in children:
            locationParam = child.getChild("location_dwa")
            if not locationParam:
                continue
            location = locationParam.getValue(0)
            for param in child.getChildren():
                paramName = param.getName().replace("_", ".")
                if paramName == "location.dwa":
                    continue
                elif paramName.startswith("xform"):
                    info = xformEditDict.get(location)
                    if info:
                        info[paramName]["value"] = param.getValue(frameTime)
                else:
                    info = editDict.get(location)
                    if info:
                        info[paramName]["value"] = param.getValue(frameTime)
          
    #print editDict
    argsGb.set('input', str(editDict))
    argsGb.set('xformInput', str(xformEditDict))
    argsGb.set('topLocation', topLocation)

    opArgs = argsGb.build()
    interface.appendOp('MultiAttributeSetOp', opArgs)
    
    convertMatrixAttrLocations = self.getParameter("convertMatrix").getValue(0)
    if convertMatrixAttrLocations!="":
        locationList = convertMatrixAttrLocations.split(" ")
        fullpathList = []
        for location in locationList:
            fullpath = topLocation + "/" + location
            fullpathList.append(fullpath)
        
        # append an opscript
        opType = 'OpScript.Lua'
        argsGb = FnAttribute.GroupBuilder()
        deleteAttrOpScript = 'Interface.DeleteAttr("xform.matrix")'
        argsGb.set('script', deleteAttrOpScript)
        opArgs = argsGb.build()
        asb = FnGeolibServices.OpArgsBuilders.AttributeSet()
        asb.setCEL(FnAttribute.StringAttribute(" ".join(fullpathList)))
        asb.addSubOp(opType, opArgs)
        interface.appendOp('AttributeSet', asb.build())

    if soloedLocations:
        argsGb = FnAttribute.GroupBuilder()
        argsGb.set('CEL', '/root/world')
        argsGb.set('setAttrs.s0.name', "globals.itemLists.soloedLocations")
        argsGb.set('setAttrs.s0.attr', FnAttribute.StringAttribute(soloedLocations))
        interface.appendOp('AttributeSet', argsGb.build())


def getMaterialParamName(parameter):
    fullName = parameter.getFullName()
    paramName = fullName[fullName.find("shaders"):]
    paramName = paramName.replace("shaders", "material", 1)
    return paramName

def getXformParamName(parameter):
    fullName = parameter.getFullName()
    paramName = fullName[fullName.find("args"):]
    paramName = paramName.replace("args.xform", "xform.interactive", 1)
    return paramName

def duplicateLocation(self, location, newLocation):
    for paramName in ["edits", "muteSolo"]:
        editsValue = self.getParameter(paramName).getValue(0)
        if editsValue!="":
            editDict = ast.literal_eval(editsValue)
            info = editDict.get(location, {})
            duplicatedInfo = copy.deepcopy(info)
            editDict[newLocation] = duplicatedInfo
            self.getParameter(paramName).setValue(str(editDict), 0)
    
    locationParamName = location.replace("/", "_")
    param = self.getParameter("users").getChild(locationParamName)
    if not param:
        return
    newLocation = newLocation.replace("/", "_")
    dstParam = self.getParameter("users").createChildGroup(newLocation)
    dstParam.parseXML(param.getXML())
    dstParam.getChild("location_dwa").setValue(newLocation, 0)

def setSparseEditsForLocation(self, location, param, paramName):
    if not param.isAnimated() and not param.isExpression():
        return False
    locationParam = location.replace("/", "_")
    dstParamName = paramName.replace(".", "_")
    parent = self.getParameter("users").getChild(locationParam)
    if not parent:
        parent = self.getParameter("users").createChildGroup(locationParam)
    if not parent.getChild("location_dwa"):
        parent.createChildString("location_dwa", location)
    dstParam = parent.getChild(dstParamName)
    if not dstParam:
        if param.getType() == "number":
            dstParam = parent.createChildNumber(dstParamName, 0)
        elif param.getType() == "string":
            dstParam = parent.createChildString(dstParamName, "")
    dstParam.parseXML(param.getXML())
    return True

def getParamValue(parentParam):
    param = parentParam.getChild("value")
    children = param.getChildren()
    if not children:
        return param.getValue(0)
    values = []
    for child in children:
        values.append(child.getValue(0))
    return values
        

def adoptLocation(self, location):
    frameTime = NodegraphAPI.GetCurrentTime()
    editsString = self.getParameter('edits').getValue(frameTime)
    editDict = {}
    if editsString!="":
        editDict = ast.literal_eval(editsString)

    editDict[location] = {"attributeEditor.material.exclusiveTo": {"enable": 1.0, "value": self.getName(), "type":"StringAttr"}, 
                          "attributeEditor.xform.exclusiveTo": {"enable": 1.0, "value": self.getName(), "type":"StringAttr"}}
    self.getParameter('edits').setValue(str(editDict), frameTime)


def setParamEditForLocation(self, location, param, isXform=False):
    #print "setParamEditForLocation"
    #print location
    #print param
    if location == None:
        return
    editDict = {}
    parentParam = param
    while not parentParam.getChild("type"):
        parentParam = parentParam.getParent()
        # refactor here..
        if parentParam == None:
            return
        
    name = param.getName() 
    paramType = parentParam.getChild("type").getValue(0)

    paramName = ""
    editsValue = ""

    paramName = getMaterialParamName(parentParam)
    if isXform:
        paramName = getXformParamName(parentParam)
        editsValue = self.getParameter("xformEdits").getValue(0)
    else:
        paramName = getMaterialParamName(parentParam)
        editsValue = self.getParameter("edits").getValue(0)
    
    if editsValue!="":
        editDict = ast.literal_eval(editsValue)

    if location not in editDict:
        editDict[location] = {}

    locationEdits = editDict.get(location, {})
    paramEdit = locationEdits.get(paramName, {})
    paramEdit["type"] = paramType # always save type..

    if name == "enable":
        paramEdit["enable"] = param.getValue(0)

    # always save value related edit.. (color widget doesn't give us value param)
    result = self.setSparseEditsForLocation(location, param, paramName) # expresson/animation
    if result == False:
        if not paramEdit:
            paramEdit["value"] = getParamValue(parentParam)
        else:
            paramEdit["value"] = getParamValue(parentParam)
    
    locationEdits[paramName] = paramEdit
    editDict[location] = locationEdits

    if isXform:
        self.getParameter("xformEdits").setValue(str(editDict), 0)
    else:
        self.getParameter("edits").setValue(str(editDict), 0)

def getParamEditsForLocation(self, location, isXformParam=False):
    editsValue = self.getParameter("edits").getValue(0)
    if isXformParam == True:
        editsValue = self.getParameter("xformEdits").getValue(0)
    if editsValue =="":
        return
    editDict = ast.literal_eval(editsValue)
    locationDict = editDict.get(location, {})
    return locationDict


def getSparseEditsForLocation(self, location):
    locationParamName = location.replace("/", "_")
    param = self.getParameter("users").getChild(locationParamName)
    return param

def getAdoptedLocations(self):
    editsValue = self.getParameter("edits").getValue(0)
    if editsValue =="":
        return []
    editDict = ast.literal_eval(editsValue)
    return editDict.keys()

def renameLocation(self, location, newLocation):
    for paramName in ["edits", "muteSolo"]:
        editsValue = self.getParameter(paramName).getValue(0)
        if editsValue == "":
            continue
        editDict = ast.literal_eval(editsValue)
        if not editDict.get(location):
            continue 
        info = editDict.pop(location)
        editDict[newLocation] = info
        self.getParameter(paramName).setValue(str(editDict), 0)
    
    locationParam = self.getParameter("users").getChild(location.replace("/", "_"))
    if locationParam:
        locationParam.getChild("location_dwa").setValue(newLocation, 0)


def removeLocation(self, location):
    for paramName in ["edits", "muteSolo"]:
        editsValue = self.getParameter(paramName).getValue(0)
        if editsValue != "":
            editDict = ast.literal_eval(editsValue)
            if editDict.get(location):
                editDict.pop(location)
                self.getParameter(paramName).setValue(str(editDict), 0)
    
    parent = self.getParameter("users")
    locationParamName = location.replace("/", "_")
    if parent.getChild(locationParamName):
        self.getParameter("users").deleteChild(parent.getChild(locationParamName))

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
    
    print attrData
    '''
    if not attrData:
        return
    xyz = ['x', 'y', 'z']
    rootPath = self.getParameter("topLocation").getValue(0)
    location = SharedFunc.getRelativePathToRoot(path, rootPath)
    #print attrName
    # override xform
    if attrName.startswith("xform.interactive"):
        if attrName.endswith("X"):
            attrName = attrName.replace("X", ".x")
        elif attrName.endswith("Y"):
            attrName = attrName.replace("Y", ".y")
        elif attrName.endswith("Z"):
            attrName = attrName.replace("Z", ".z")
        else:
            attrName = attrName + "." + xyz[index] 
        editsValue = self.getParameter("xformEdits").getValue(0)
        editDict = {}
        #print attrName

        if editsValue!="":
            editDict = ast.literal_eval(editsValue)
            locationEdit = editDict.get(location, {})
            if locationEdit:
                locationEdit[attrName] = {"enable": 1.0, 'type': 'DoubleAttr', 'value': attrData[0]}
            else:
                editDict[location] = {attrName:{"enable": 1.0, 'type': 'DoubleAttr', 'value': attrData[0]}}
        else:
            editDict[location] = {attrName:{"enable": 1.0, 'type': 'DoubleAttr', 'value': attrData[0]}}
        
        #print editDict
        self.getParameter('xformEdits').setValue(str(editDict), 0)
        Utils.EventModule.QueueEvent('dreamGaffer_viewerOverrideXform', 1)
    elif attrName.startswith("material"):
        print attrName
        print attrData


def editAttributes(self, attrInfo, paramName="edits"):
    editsValue = self.getParameter(paramName).getValue(0)
    editDict = {}
    if editsValue != "":
        editDict = ast.literal_eval(editsValue)
        for location, attr in attrInfo.iteritems():
            if not editDict.get(location):
                editDict[location] = attr
            else:
                editDict.get(location, {}).update(attr)
    else:
        editDict = attrInfo
    self.getParameter(paramName).setValue(str(editDict), 0)


def setXformWithMatrix(self, location, matrix):
    from Katana import GeoAPI
    translate, scale, rotate = GeoAPI.Util.Matrix.explodeMatrix4x4(matrix)
    xformDict = {}
    xformDict["xform.interactive.translate.x"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': translate[0]}
    xformDict["xform.interactive.translate.y"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': translate[1]}
    xformDict["xform.interactive.translate.z"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': translate[2]}
    xformDict["xform.interactive.rotate.x"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': rotate[0]}
    xformDict["xform.interactive.rotate.y"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': rotate[1]}
    xformDict["xform.interactive.rotate.z"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': rotate[2]}
    xformDict["xform.interactive.scale.x"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': scale[0]}
    xformDict["xform.interactive.scale.y"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': scale[1]}
    xformDict["xform.interactive.scale.z"] = {'enable': 1.0, 'type': 'DoubleAttr', 'value': scale[2]}

    currentXformDict = {}
    xformEditsValue = self.getParameter("xformEdits").getValue(0)
    if xformEditsValue != "":
        currentXformDict = ast.literal_eval(xformEditsValue)
    
    currentXformDict[location] = xformDict
    self.getParameter("xformEdits").setValue(str(currentXformDict), 0)
    convertMatrixValue = self.getParameter("convertMatrix").getValue(0)
    if convertMatrixValue == "":
        self.getParameter("convertMatrix").setValue(location, 0)
    else:
        self.getParameter("convertMatrix").setValue(convertMatrixValue+" "+location, 0)

def getEditDict(attr, parentAttrName, editDict):
    if attr:
        num = attr.getNumberOfChildren()
        for i in range(0, num):
            child = attr.getChildByIndex(i)
            childName = attr.getChildName(i)
            childXML = ET.fromstring(child.getXML())
            
            attrType = childXML.get("type")
            if attrType == "GroupAttr":
                getEditDict(child, parentAttrName+"."+childName, editDict)
            else:
                
                value = child.getNearestSample(0)
                num =  child.getNumberOfValues()
                if num == 1:
                    value = value[0]
                attrName = parentAttrName+"."+childName
                editDict[attrName] = {"enable": 1, "type": attrType, "value": value}


def getMuteSoloEdit(attr, attrName, editDict):
    if attr:
        value = attr.getNearestSample(0)
        num =  attr.getNumberOfValues()
        if num == 1:
            value = value[0]
        editDict[attrName] = {"enable": 1, "type": "IntAttr", "value": value}

def editLocationWithAttrs(self, location, attrs):
    # this is used in duplicate incoming location..
    try:
        editDict = {}
        attrNameList = ["material.moonrayLightParams", "material.moonrayLightfilterParams"]
        for attrName in attrNameList:
            getEditDict(attrs.getChildByName(attrName), attrName, editDict)
        #print editDict
        self.editAttributes({location: editDict})
        
        mutesolo = {}
        attrNameList = ["info.light.mute", "info.light.solo"]
        for attrName in attrNameList:
            getMuteSoloEdit(attrs.getChildByName(attrName), attrName, mutesolo)
        self.editAttributes({location: mutesolo}, paramName="muteSolo")
    except:
        import traceback
        traceback.print_exc()


def updateSoloForLocation(self, location):
    enable = {"info.light.solo": {"type": "IntAttr", "enable": 1, "value" : 1}}
    
    muteSoloValue = self.getParameter("muteSolo").getValue(0)
    muteSoloDict = {}
    if muteSoloValue!="":
        muteSoloDict = ast.literal_eval(muteSoloValue)
        info =  muteSoloDict.get(location, {})
        if info:
            if info.get("info.light.solo"):
                value = bool(info.get("info.light.solo").get("value"))
                newValue = not value
                info["info.light.solo"]["value"] = int(newValue)
            else:
                info.update(enable)
        else:
            muteSoloDict[location] = enable
    else:
        muteSoloDict[location] = enable 
    self.getParameter("muteSolo").setValue(str(muteSoloDict), 0)


def updateMuteForLocation(self, location):
    enable = {"info.light.mute": {"type": "IntAttr", "enable": 1, "value" : 1}}
    muteSoloValue = self.getParameter("muteSolo").getValue(0)
    muteSoloDict = {}
    if muteSoloValue!="":
        muteSoloDict = ast.literal_eval(muteSoloValue)
        info =  muteSoloDict.get(location, {})
        if info:
            if info.get("info.light.mute"):
                value = bool(info.get("info.light.mute").get("value"))
                newValue = not value
                info["info.light.mute"]["value"] = int(newValue)
            else:
                info.update(enable)
        else:
            muteSoloDict[location] = enable
    else:
        muteSoloDict[location] = enable

    self.getParameter("muteSolo").setValue(str(muteSoloDict),0)

nb.setCustomMethod('getParamEditsForLocation', getParamEditsForLocation)
nb.setCustomMethod('setParamEditForLocation', setParamEditForLocation)
nb.setCustomMethod('updateMuteForLocation', updateMuteForLocation)
nb.setCustomMethod('updateSoloForLocation', updateSoloForLocation)
nb.setCustomMethod('editAttributes', editAttributes)
nb.setCustomMethod('editLocationWithAttrs', editLocationWithAttrs)
nb.setCustomMethod('setXformWithMatrix', setXformWithMatrix)


nb.setCustomMethod('renameLocation', renameLocation)
nb.setCustomMethod('duplicateLocation', duplicateLocation)
nb.setCustomMethod('removeLocation', removeLocation)
nb.setCustomMethod('getSparseEditsForLocation', getSparseEditsForLocation)
nb.setCustomMethod('setSparseEditsForLocation', setSparseEditsForLocation)
nb.setCustomMethod('adoptLocation', adoptLocation)
nb.setCustomMethod('getAdoptedLocations', getAdoptedLocations)

nb.setCustomMethod('canOverride', canOverride)
nb.setCustomMethod('setOverride', setOverride)
nb.setCustomMethod('setInteractiveTransformFlag', setInteractiveTransformFlag)


nb.setBuildOpChainFnc(buildOpChain)
nb.setParametersTemplateAttr(gb.build())
nb.setHintsForParameter('topLocation', {
        'widget': 'scenegraphLocation',
    })

nb.build()
