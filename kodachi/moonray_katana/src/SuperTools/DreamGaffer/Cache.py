# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import os
import Helper

def getLocationPath(parentPath, name):
    if parentPath and parentPath!="":
        return parentPath+"/"+name
    return name
'''
lights = {"a": {"index": 0, "children": ["b", "c"]}, 
            "a/b": {"index": 1, "children": ["d"]}, 
            "a/c":{"index":2, "children": []}
            "a/b/d": {"index":3, "children": []}}
'''
class BaseCache():
    def __init__(self, control, locationType):
        self.cache = {}
        self.unused = []
        self.control = control
        self.locationType = locationType
        self.node = None # the node that created the location

    def clearCache(self):
        self.unused = []
        self.cache.clear()

    def buildCache(self, targetNode):
        if not targetNode:
            return

        self.node = targetNode
        p = targetNode.getParameter("locations")
        locationList = {}
        for i, child in enumerate(p.getChildren()): 
            if child.getValue(0) == "":
                self.unused.append(i)
            else:
                locationList[child.getValue(0)] = i
        
        # process the list of locations
        for location, i in locationList.iteritems():
            self.cache[location] = {"index": i, "children": []}
        
        for location in locationList.keys():
            parent = os.path.dirname(location)
            if parent=="":
                continue
            
            if not self.addChildToParent(parent, os.path.basename(location), self.locationType):
                if self.locationType == "light filter":
                    self.control.getLightCache().addChildToParent(parent, os.path.basename(location), self.locationType)

    def addChildToParent(self, parentPath, childName, locationType):
        if parentPath not in self.cache:
            return False
        children = self.cache[parentPath].get("children", [])
        children.append(childName)
        return True

    def setNode(self, node):
        self.node = node

    def locationExists(self, location):
        if location in self.cache:
            return True
        return False

    def getIndexForLocation(self, location):
        info = self.cache.get(location)
        if info:
            return info.get("index")
    
    def getIndexForNewLocation(self):
        if self.unused:
            return self.unused.pop()
        return len(self.cache)

    def updateChildName(self, parentPath, newName, oldName, locationType):
        if parentPath in self.cache:
            children = self.cache[parentPath].get("children")
            if oldName!=None and oldName in children:
                children.remove(oldName)
            if newName:
                children.append(newName)
            return True
        return False
    
    def rename(self, oldLocation, newName): # rename a leaf
        try:
            # update parent cache..
            parentPath = os.path.dirname(oldLocation)
            newLocation = getLocationPath(parentPath, newName)
            oldName = os.path.basename(oldLocation)
            self.control.findAndUpdateChildName(parentPath, newName, oldName, self.locationType)
            self.renameLocation(oldLocation, newLocation)
        except:
            import traceback
            traceback.print_exc()

    def renameLocation(self, oldLocation, newLocation):
        # update the key...info stays the same.
        info = self.cache.pop(oldLocation)
        index = info.get("index")
        self.cache[newLocation] = info
        self.node.renameLocation(index, newLocation)
        self.control.renameSingleLocation(oldLocation, newLocation)
        # update children..
        self.recursiveRenameChildLocation(oldLocation, info.get("children"), newLocation)

    def recursiveRenameChildLocation(self, parentPath, children, newParentPath):
        # children of light are always lights..
        for childName in children:
            location = getLocationPath(parentPath, childName)
            newLocation = getLocationPath(newParentPath, childName)
            if location in self.cache:
                info = self.cache.pop(location)
                index = info.get("index")
                children = info.get("children") # direct children name stays the same
                self.cache[newLocation] = info
                self.node.renameLocation(index, newLocation)
                self.control.renameSingleLocation(location, newLocation)
                self.recursiveRenameChildLocation(location, children, newLocation)
    
    def recursiveDuplicateLocation(self, srcLocation, dstLocation):
        if srcLocation in self.cache:
            srcIndex = self.cache[srcLocation].get("index")
            dstIndex = self.getIndexForNewLocation()
            # add new location to cache..
            self.cache[dstLocation] = {"index": dstIndex, "children": self.cache[srcLocation].get("children")[:]}
            # copy data
            self.node.duplicateLocation(srcIndex, dstIndex, dstLocation)
            self.control.duplicateSingleLocation(srcLocation, dstLocation) # duplicate other attributes

            children = self.cache[srcLocation].get("children", [])
            for name in children:
                childLocationSrc = getLocationPath(srcLocation, name)
                childLocationDst = getLocationPath(dstLocation, name)
                self.recursiveDuplicateLocation(childLocationSrc, childLocationDst)

    
    def recursiveRemoveLocation(self, parentPath, name):
        try:
            location = getLocationPath(parentPath, name)
            lightInfo = self.cache.pop(location)
            index = lightInfo.get("index")
            self.unused.append(index)
            self.node.removeLocationAtIndex(index)
            self.control.removeSingleLocation(location)

            for childName in lightInfo.get("children", []):
                self.recursiveRemoveLocation(location, childName)
        except:
            import traceback
            traceback.print_exc()

    def getChildren(self, parentPath):
        if parentPath in self.cache:
            return self.cache[parentPath].get("children")

    
    def addLocation(self, locationPath, index, isChild):
        self.cache[locationPath] = {"index": index,   "children": []}
        if not self.node:
            self.node = self.control.initTargetNode(self.locationType)
        
        self.node.addLocationAtIndex(locationPath, index, isChild)
    
    def copyXformMatAttr(self, attr, index):
        try:
            translate = rotate = scale = []
            if attr.getChildByName("xform.matrix"):
                from Katana import GeoAPI
                matrix = attr.getChildByName("xform.matrix").getNearestSample(0)
                translate, scale, rotate = GeoAPI.Util.Matrix.explodeMatrix4x4(matrix)

            elif attr.getChildByName("xform.interactive"):
                translate = attr.getChildByName("xform.interactive.translate").getNearestSample(0)
                scale = attr.getChildByName("xform.interactive.scale").getNearestSample(0)
                rotate = []
                rotate.append(attr.getChildByName("xform.interactive.rotateX").getValue(0))
                rotate.append(attr.getChildByName("xform.interactive.rotateY").getValue(0))
                rotate.append(attr.getChildByName("xform.interactive.rotateZ").getValue(0))

            if translate and rotate and scale:
                self.node.setXformValuesAtIndex("translate", translate, index)
                self.node.setXformValuesAtIndex("rotate", rotate, index)
                self.node.setXformValuesAtIndex("scale", scale, index)

                if attr.getChildByName("inherits"):
                    inherits =  attr.getChildByName("inherits").getData()[0]
                    self.node.getParameter("material").getChildByIndex(index).setValue(inherits, 0)
                else:
                    if attr.getChildByName("material.moonrayLightShader"):
                        material = attr.getChildByName("material.moonrayLightShader").getData()[0]
                        self.node.getParameter("material").getChildByIndex(index).setValue(material, 0)

                    elif attr.getChildByName("material.moonrayLightfilterShader"):
                        material = attr.getChildByName("material.moonrayLightfilterShader").getData()[0]
                        self.node.getParameter("material").getChildByIndex(index).setValue(material, 0)

        except:
            import traceback
            traceback.print_exc()

    
    def removeChild(self, parentPath, childName):
        if parentPath not in self.cache:
            return False
        children = self.cache[parentPath].get("children")
        if childName in children:
            children.remove(childName)
            return True
    

    def getUnusedName(self, parentPath, basename):
        children = []

        if parentPath == "":
            if basename not in self.cache:
                print "no parent path"
                return basename
            else:
                children = self.cache.keys()
        else:
            children = self.getChildren(parentPath)
            if not children:
                children = self.control.getRigCache().getChildren(parentPath)
           
            if not children: # parent rig might exist in upstream...
                paths = self.cache.keys()
                children = []
                for each in paths:
                    if not each.startswith(parentPath):
                        continue
                    children.append(os.path.relpath(each, parentPath))
                        
        return Helper.getUnusedName(basename, children)

    def setMaterialForLocation(self, location, materialValue):
        if self.locationType == "rig":
            return
        index = self.getIndexForLocation(location)
        if index!=None:
            self.node.setMaterialAtIndex(materialValue, index)
    
    def getLocationList(self):
        return self.cache.keys()



class LightRigCache(BaseCache):
    def __init__(self, control, locationType):
        BaseCache.__init__(self, control, locationType)

    
    #override
    def buildCache(self, targetNode):
        if not targetNode:
            return
        
        self.node = targetNode
        p = targetNode.getParameter("locations")
        locationList = {}
        for i, child in enumerate(p.getChildren()): 
            if child.getValue(0) == "":
                self.unused.append(i)
            else:
                locationList[child.getValue(0)] = i
        
        # process the list of locations
        for location, i in locationList.iteritems():
            self.cache[location] = {"index": i, "children": {}}
        
        for location in locationList.keys():
            parent = os.path.dirname(location)
            if parent=="":
                continue
            # its parent is a light
            childName = os.path.basename(location)
            if not self.addChildToParent(parent, childName, self.locationType):
                if self.locationType == "light":
                    self.control.getRigCache().addChildToParent(parent, childName, self.locationType)
    
    def addChildToParent(self, parentPath, childName, locationType):
        if parentPath not in self.cache:
            return False
        children = self.cache[parentPath].get("children")
        children[childName] = locationType
        return True
    

    #override
    def recursiveRemoveLocation(self, parentPath, name):
        try:
            location = getLocationPath(parentPath, name)
            lightInfo = self.cache.pop(location)
            index = lightInfo.get("index")
            self.unused.append(index)
            self.node.removeLocationAtIndex(index)
            self.control.removeSingleLocation(location)

            children = lightInfo.get("children")
            for childName, childType in children.iteritems():
                if childType == self.locationType:
                    self.recursiveRemoveLocation(location, childName)
                else:
                    childCache = self.control.getCacheOfLocationType(childType)
                    if childCache:
                        childCache.recursiveRemoveLocation(location, childName)
        except:
            import traceback
            traceback.print_exc()


    #override
    def recursiveRenameChildLocation(self, parentPath, children, newParentPath):
        for childName, childType in children.iteritems():
            location = getLocationPath(parentPath, childName)
            newLocation = getLocationPath(newParentPath, childName)
            if childType == self.locationType:
                if location in self.cache:
                    info = self.cache.pop(location)
                    index = info.get("index")
                    children = info.get("children") # children is a dictionary
                    
                    self.cache[newLocation] = info
                    self.node.renameLocation(index, newLocation)
                    self.control.renameSingleLocation(location, newLocation)
                    self.recursiveRenameChildLocation(location, children, newLocation)
            else:
                childCache = self.control.getCacheOfLocationType(childType)
                if childCache:
                    childCache.renameLocation(location, newLocation)


    def recursiveDuplicateLocation(self, srcLocation, dstLocation):
        if srcLocation in self.cache:
            srcIndex = self.cache[srcLocation].get("index")
            dstIndex = self.getIndexForNewLocation()
            # add new location to cache..
            self.cache[dstLocation] = {"index": dstIndex, "children": self.cache[srcLocation].get("children").copy()}
            # copy data
            self.node.duplicateLocation(srcIndex, dstIndex, dstLocation)
            self.control.duplicateSingleLocation(srcLocation, dstLocation) # duplicate other attributes

            children = self.cache[srcLocation].get("children", {})
            for name, locationType in children.iteritems():
                childLocationSrc = getLocationPath(srcLocation, name)
                childLocationDst = getLocationPath(dstLocation, name)
                if locationType == self.locationType:
                    self.recursiveDuplicateLocation(childLocationSrc, childLocationDst)
                else:
                    childCache = self.control.getCacheOfLocationType(locationType)
                    if childCache:
                        childCache.recursiveDuplicateLocation(childLocationSrc, childLocationDst)
    

    def getChildren(self, parentPath):
        if parentPath in self.cache:
            return self.cache[parentPath].get("children").keys()

    def addLocation(self, locationPath, index, isChild):
        self.cache[locationPath] = {"index": index,   "children": {}}
        if not self.node:
            self.node = self.control.initTargetNode(self.locationType)
        self.node.addLocationAtIndex(locationPath, index)
    
    def updateChildName(self, parentPath, newName, oldName, locationType):
        if parentPath in self.cache:
            children = self.cache[parentPath].get("children")
            if oldName!=None and oldName in children:
                del children[oldName]
            if newName:
                children[newName]=locationType
            return True
        return False

    def removeChild(self, parentPath, childName):
        if parentPath not in self.cache:
            return False
        children = self.cache[parentPath].get("children")
        if childName in children:
            del children[childName]
            return True
