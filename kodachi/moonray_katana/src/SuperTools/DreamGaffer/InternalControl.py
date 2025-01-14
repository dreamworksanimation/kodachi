# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

#build internal cache when the supertool is initiated
#the cache holds all the rigs and lights and materials..a list of rigs, a list lights..a list of material..that are created in this gaffer.
#this helps when duplicate upstream lights - we get attributes for upstream locations first and then create locations.
#for each one, it's worth caching its index so that its faster to retrieve the information.?

import os
from Katana import Utils, Decorators
from Cache import *
'''
example: 
light cache for light node..
rig cache for rig node..
because of the index
rig cache can point to light cache for renaming, duplicating..
light cache can't point back to rig ..
light cache and material cache are similar..

light cache can have light filter ...


lights = {"a": {"index": 0, "children": ["b", "c"]}, 
            "a/b": {"index": 1, "children": ["d"]}, 
            "a/c":{"index":2, "children": []}
            "a/b/d": {"index":3, "children": []}}
materials = {"a": {"index": 0, "children": ["b", "c"]}, 
            "a/b": {"index": 1, "children": ["d"]}, 
            "a/c":{"index":2, "children": []}
            "a/b/d": {"index":3, "children": []}}

rigs = {"a": {"index": 0, "children": {"b": "light", "c": "rig"}}, 
            "a/b": {"index": 1, "children": ["d": "rig"]}, 
            "a/c":{"index":2, "children": []}
            "a/b/d": {"index":3, "children": []}}
'''


class InternalControl():
    def __init__(self, node):
        #self.lightCache = BaseCache(self, "light")
        self.lightCache = LightRigCache(self, "light")
        self.rigCache = LightRigCache(self, "rig")
        self.materialCache = BaseCache(self, "material")
        self.lightFilterCache = BaseCache(self, "light filter")
        self.adoptedLocations = []
        self.node = node
        self.buildInternalCache()
    
        
    @Decorators.undogroup('DreamGaffer Rename location')
    def rename(self, locationPath, newName, locationType):
        self.getCacheOfLocationType(locationType).rename(locationPath, newName)

    @Decorators.undogroup('DreamGaffer Move location')
    def move(self, oldLocation, newLocation, locationType, oldParentType, newParentType):
        if oldParentType:
            #print oldParentType
            childName = os.path.basename(oldLocation)
            parentPath = os.path.dirname(oldLocation)
            self.getCacheOfLocationType(oldParentType).removeChild(parentPath, childName)
            #print self.getCacheOfLocationType(oldParentType).cache
        
        if newParentType:
            #print newParentType
            newParentPath = os.path.dirname(newLocation)
            childName = os.path.basename(newLocation)
            newParentCache = self.getCacheOfLocationType(newParentType)
            newParentCache.addChildToParent(newParentPath, childName, locationType)
            #print newParentCache.cache
        try:
            self.getCacheOfLocationType(locationType).renameLocation(oldLocation, newLocation)
        except:
            #print "oldlocation", oldLocation
            #print "newlocation", newLocation
            import traceback
            traceback.print_exc()
        
    def renameSingleLocation(self, location, newLocation):
        # material edit
        self.node.getMaterialEditNode().renameLocation(location, newLocation)
        # todo: light link..


    def duplicateSingleLocation(self, location, newLocation):
        self.node.getMaterialEditNode().duplicateLocation(location, newLocation)
        # todo: light link



    def removeSingleLocation(self, location):
        self.node.getMaterialEditNode().removeLocation(location)


    def initTargetNode(self, locationType): 
        node = self.node.getCreateNodeForLocationType(locationType, forceCreate=True)
        return node

    def locationExists(self, location, locationType):
        if not self.getCacheOfLocationType(locationType):
            return False
        return self.getCacheOfLocationType(locationType).locationExists(location)
    
    def locationAdopted(self, location):
        if location in self.adoptedLocations:
            return True
        return False

    def updateSoloForLocation(self, location, locationType):
        if self.locationExists(location, locationType):
            self.node.getMaterialEditNode().updateSoloForLocation(location)
        elif self.locationAdopted(location):
            self.node.getDownstreamEditNode().updateSoloForLocation(location)
    
    def updateMuteForLocation(self, location, locationType):
        if self.locationExists(location, locationType):
            self.node.getMaterialEditNode().updateMuteForLocation(location)
        elif self.locationAdopted(location):
            self.node.getDownstreamEditNode().updateMuteForLocation(location)

    def getIndexForLocation(self, location, locationType):
        if not self.getCacheOfLocationType(locationType):
            return None
        return self.getCacheOfLocationType(locationType).getIndexForLocation(location)

    def clearCache(self):
        self.rigCache.clearCache()
        self.lightCache.clearCache()
        self.materialCache.clearCache()
        self.lightFilterCache.clearCache()

    def buildInternalCache(self):
        # build rig cache first so that we can add light to it...        
        self.rigCache.buildCache(self.node.getRigCreateNode())
        self.lightCache.buildCache(self.node.getLightCreateNode())
        self.lightFilterCache.buildCache(self.node.getLightFilterCreateNode())
        self.materialCache.buildCache(self.node.getMaterialCreateNode())
        
        node = self.node.getDownstreamEditNode()
        if node:
            self.adoptedLocations = node.getAdoptedLocations()


    def findAndUpdateChildName(self, parentPath, newName, oldName, locationType, parentType=None):
        # inform parent that the child's name has been change..if the location has a parent..
        if parentType:
            self.getCacheOfLocationType(parentType).updateChildName(parentPath, newName, oldName, locationType)
            return
        
        # we dont know the parent type...
        if locationType == "light":
            # light's parent can be rig or light
            if not self.lightCache.updateChildName(parentPath, newName, oldName, locationType):
                self.rigCache.updateChildName(parentPath, newName, oldName, locationType)
        elif locationType == "light filter":
            # light filter's parent can be light filter or a light
            if not self.getCacheOfLocationType(locationType).updateChildName(parentPath, newName, oldName, locationType):
                self.lightCache.updateChildName(parentPath, newName, oldName, locationType)
        else:
            # rig or material's parent is of the same type
            self.getCacheOfLocationType(locationType).updateChildName(parentPath, newName, oldName, locationType)
        
    
    def removeLocation(self, parentPath, location, locationType):
        name = os.path.basename(location)
        newName = None
        # remove light from its parent if there is one
        self.findAndUpdateChildName(parentPath, newName, name, locationType)
        self.getCacheOfLocationType(locationType).recursiveRemoveLocation(parentPath, name)


    def duplicateLocation(self, srcLocation, locationType):
        try:
            parentPath = os.path.dirname(srcLocation)
            cache = self.getCacheOfLocationType(locationType)
            name = cache.getUnusedName(parentPath, os.path.basename(srcLocation))

            dstLocation = getLocationPath(parentPath, name)
            oldName = None
            self.findAndUpdateChildName(parentPath, name, oldName, locationType)
            print "going to duplicate"
            cache.recursiveDuplicateLocation(srcLocation, dstLocation)
        except:
            import traceback
            traceback.print_exc()
            

    def duplicateIncomingLocation(self, parentPath, basename, parentType, locationType, attrs):
        try:
            name, index, cache = self.addLocation(parentPath, basename, parentType, locationType)
            cache.copyXformMatAttr(attrs, index)

            location = getLocationPath(parentPath, name)
            self.node.getMaterialEditNode(forceCreate=True).editLocationWithAttrs(location, attrs)
            return name
        except:
            import traceback
            traceback.print_exc()

    def addLocation(self, parentPath, basename, parentType, locationType):
        cache = self.getCacheOfLocationType(locationType)
        name = cache.getUnusedName(parentPath, basename)
        index = cache.getIndexForNewLocation()
        fullPath = getLocationPath(parentPath, name)
        isChild = False
        if parentType and parentType == locationType:
            isChild=True
        
        cache.addLocation(fullPath, index, isChild)
        if parentType:
            oldName=None 
            self.findAndUpdateChildName(parentPath, name, oldName, locationType, parentType)
        return name, index, cache

    def editLocation(self, location):
        self.adoptedLocations.append(location)
        self.node.getDownstreamEditNode(forceCreate=True).adoptLocation(location)

    def setMaterialForLocation(self, locationType, materialValue, location):
        cache = self.getCacheOfLocationType(locationType)
        cache.setMaterialForLocation(location, materialValue)

    def getAvailableMasterMaterials(self):
        return self.materialCache.getLocationList()

    def getCacheOfLocationType(self, locationType):
        if locationType == "light":
            return self.lightCache
        elif locationType == "rig":
            return self.rigCache
        elif locationType == "material":
            return self.materialCache
        elif locationType == "light filter":
            return self.lightFilterCache

    def getLightCache(self):
        return self.lightCache
    
    def getRigCache(self):
        return self.rigCache
    
    def getMaterialCache(self):
        return self.materialCache
    

        
