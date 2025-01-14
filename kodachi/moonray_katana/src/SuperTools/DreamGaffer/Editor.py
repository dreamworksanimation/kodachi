# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import os
from Katana import QtCore, QtWidgets, UI4, QT4Widgets, QT4FormWidgets, NodegraphAPI, QtGui, Decorators
from Katana import Utils, Nodes3DAPI, Decorators, FormMaster, ScenegraphManager
from UI4.Widgets.SceneGraphView.ColumnDataType import ColumnDataType
from UI4.Widgets.SceneGraphView import SceneGraphLocationTranslation

from Tabs import MaterialTab, ObjectTab, FilterTab
from Node import SYNC_SELECTION_OFF, SYNC_SELECTION_OUT, SYNC_SELECTION_IN_OUT

from InternalControl import InternalControl
import Helper


class DreamGafferEditor(QtWidgets.QWidget):
    def __init__(self, parent, node):
        try:
            QtWidgets.QWidget.__init__(self, parent)
            self.__mainNode = node
            self.__frozen = True
            self.__updateOnIdle = False
            self.__systemSetValue = False
            self.__expansionState = None
            self.__oldRootLocation = None
            self.__oldSelectionState = None
            self.__selectedPath = None
            self.__currentLocation = None
            self.__currentLocationType = None

            self.__lastRootLocation = self.__mainNode.getRootLocation()
            self.__redoStackSize = 0
            self.__nextRedoName = ""

            QtWidgets.QVBoxLayout(self)

            self.__buildToolbar()
            self.__mainPanel = QtWidgets.QWidget(self)
            self.layout().addWidget(self.__mainPanel)
            QtWidgets.QVBoxLayout(self.__mainPanel)

            # stretch box

            self.__listStretchBox = UI4.Widgets.StretchBox(self, allowHorizontal=True, allowVertical=True)
            self.__listStretchBox.setObjectName('listStretchBox')
            self.__listStretchBox.setFixedHeight(300)
            self.__mainPanel.layout().addWidget(self.__listStretchBox)
            
            # scenegraph view
            self.__sceneGraphView = UI4.Widgets.SceneGraphView(parent=self)
            self.__sceneGraphView.setDragMoveEventCallback(self.__dragMoveEventCallback)
            self.__sceneGraphView.setDropEventCallback(self.__dropEventCallback)
            
            self.__sceneGraphViewWidget = self.__sceneGraphView.getWidget()

            self.__configureSceneGraphView()
            self.__listStretchBox.layout().addWidget(self.__sceneGraphViewWidget)
            # tab panel area

            self.__tabsPanel = QtWidgets.QWidget(self.__mainPanel)
            self.__tabsPanel.setSizePolicy(QtWidgets.QSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding))
            QtWidgets.QVBoxLayout(self.__tabsPanel)
            
            self.__mainPanel.layout().addWidget(self.__tabsPanel)
            self.__tabWidget = QtWidgets.QTabWidget(self.__tabsPanel)
            self.__tabWidget.setSizePolicy(QtWidgets.QSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding))

            self.__tabWidget.setObjectName('tabWidget')
            self.__tabWidget.setTabBar(UI4.Widgets.WheelEventIgnoringTabBar())
            self.__tabWidget.currentChanged.connect(self.__tabChanged)
              
            self.__tabsPanel.layout().addWidget(self.__tabWidget)
            self.__setupTabs()
            
            self.__sceneGraphView.setLocationActive(self.__lastRootLocation)
            # update viewed node
            self.__showIncomingSceneEvent()
            self.__control = self.__mainNode.buildInternalControl()
            
            self.registerKeyboardShortcuts()
            from UI4.App.KeyboardShortcutManager import BindActions
            BindActions(self.getSuperToolName(), self.__sceneGraphViewWidget, self)
            
            Utils.UndoStack.RegisterCallback(self.__undoStackCallback)
        except:
            import traceback
            traceback.print_exc()


    def __undoStackCallback(self):
        # everytime redo stack changes, increase or decrease, we check the next redo name, 
        # if the size +1, we check GetNextRedoName(), if the name starts with dreamgaffer, it means user just undo a dreamgaffer action, we need to rebuild the cahce
        redoSize = Utils.UndoStack.GetRedoSize()
        if redoSize == self.__redoStackSize:
            return
        
        name = Utils.UndoStack.GetNextRedoName()
        if name and name.startswith("DreamGaffer"):
            self.__control.clearCache()
            self.__control.buildInternalCache()
            print self.__control.lightCache.cache
        self.__redoStackSize = redoSize


    @classmethod
    def getSuperToolName(cls):
        """
        @rtype: C{str}
        @return: The name of the SuperTool. Is used in the registration of
            keyboard shortcut actions for the SuperTool's UI, and for the
            separator above custom menu actions that are defined in
            L{getMenuActions()}.
        @raise NotImplementedError: If not implemented in the subclass.
        """
        return  "DreamGaffer"

    @classmethod
    def registerKeyboardShortcuts(self):
        from UI4.App.KeyboardShortcutManager import RegisterAction, GetActionID
        actionName = 'Add Light'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "L", self.__addLight)

        actionName = 'Add Rig'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "R", self.__addRig)

        actionName = 'Add Master Material'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "M", self.__addMaterial)
        
        actionName = 'Add Master Filter'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "F", self.__addFilter)

        actionName = 'Delete'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "Del", self.__deleteLocations)
        
        actionName = 'Edit'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "E", self.__editLocations)
        
        actionName = 'Duplicate'
        actionID = GetActionID(self.getSuperToolName(), actionName)
        if not actionID:
            name = '%s.%s' % (self.getSuperToolName(), actionName)
            RegisterAction(hash(name), name, "D", self.__duplicateLocation)
        

    def __dragMoveEventCallback(self, dragMoveEvent, draggedItems, parentItem, childItemIndex):
        if dragMoveEvent.source() != self.__sceneGraphView.getWidget():
            return
        else:
            locationPath = draggedItems[0].getLocationPath()
            attrs = self.__sceneGraphView.getSceneGraphAttributes(locationPath)
            locationType = attrs.getChildByName("type").getValue()

            if parentItem is None:
                dragMoveEvent.accept()
            else:
                parentLocation = parentItem.getLocationPath()
                attrs = self.__sceneGraphView.getSceneGraphAttributes(parentLocation)
                parentLocationType = attrs.getChildByName("type").getValue()

                if locationType == parentLocationType:
                    dragMoveEvent.accept()
                elif locationType == "light" and parentLocationType == "rig":
                    dragMoveEvent.accept()
    
    def __dropEventCallback(self, dropEvent, droppedItems, parentItem, childItemIndex):
        if dropEvent.source() != self.__sceneGraphView.getWidget():
            return
        else:
            droppedItem = droppedItems[0]
            fullpath = droppedItem.getLocationPath()
            oldparentPath = os.path.dirname(fullpath)
            oldLocation = self.getRelativePathToRoot(fullpath)

            newParentType = None
            oldParentType = None
            name = os.path.basename(fullpath)
            newLocation = name
            if parentItem:
                parentLocation = parentItem.getLocationPath()
                attrs = self.__sceneGraphView.getSceneGraphAttributes(parentLocation)
                newParentType = attrs.getChildByName("type").getValue()
                newLocation = self.getRelativePathToRoot(parentLocation + "/" + name)
        
            if newLocation == oldLocation:
                return

            attrs = self.__sceneGraphView.getSceneGraphAttributes(fullpath)
            locationType = attrs.getChildByName("type").getValue()
            if not self.__acceptDrop(locationType, newParentType):
                return
            if oldparentPath != self.__rootLocPolicy.getValue():
                attrs = self.__sceneGraphView.getSceneGraphAttributes(oldparentPath)
                oldParentType = attrs.getChildByName("type").getValue()

            self.__control.move(oldLocation, newLocation, locationType, oldParentType, newParentType)
                
    def __acceptDrop(self, locationType, parentLocationType):
        if parentLocationType == None:
            return True
        elif locationType == parentLocationType:
            return True
        elif locationType == "light" and parentLocationType == "rig":
            return True
        return False

    def __tabChanged(self, int):
        Utils.UndoStack.DisableCapture()
        self.__tabWidget.currentWidget().reloadTab(self.__selectedPath, self.__currentLocation, self.__currentLocationType)
        Utils.UndoStack.EnableCapture()


    def __buildToolbar(self):
        # rootLocation
        toolbarLayout = QtWidgets.QVBoxLayout()
        self.layout().addLayout(toolbarLayout)
        self.__rootLocPolicy = QT4FormWidgets.PythonValuePolicy('', {'': self.__mainNode.getRootLocation()}, 
                             childHintDict={'': {'widget': 'scenegraphLocation',
                                                'label': 'topLocation'}}).getChildByName('')
        self.__rootLocPolicy.addCallback(self.__rootLocationPolicyEvent)
        self.__rootLocWidget = FormMaster.KatanaFactory.ParameterWidgetFactory.buildWidget(self, self.__rootLocPolicy)
        toolbarLayout.addWidget(self.__rootLocWidget)

        self.__showIncomingPolicy = QT4FormWidgets.PythonValuePolicy('', {'': self.__mainNode.getShowIncomingScene()}, 
                                    childHintDict={'': {'widget': 'checkBox',
                                                        'label': 'show incoming scene'}}).getChildByName('')
        self.__showIncomingPolicy.addCallback(self.__showIncomingSceneEvent)
        self.__showIncomingWidget = FormMaster.KatanaFactory.ParameterWidgetFactory.buildWidget(self, self.__showIncomingPolicy)
        toolbarLayout.addWidget(self.__showIncomingWidget)

        # sync selection
        secondToolbarLayout = QtWidgets.QHBoxLayout()
        self.layout().addLayout(secondToolbarLayout)
        self.__syncSelectionPolicy = QT4FormWidgets.PythonValuePolicy('', {'': self.__mainNode.getSyncSelection()}, childHintDict={'': {'widget': 'mapper', 
                'label': 'sync selection', 
                'options': 'off:%.1f|out:%.1f|in/out:%.1f' % (
                          SYNC_SELECTION_OFF,
                          SYNC_SELECTION_OUT,
                          SYNC_SELECTION_IN_OUT), 
                'constant': 'True'}}).getChildByName('')
        self.__syncSelectionPolicy.addCallback(self.__syncSelectionValueChanged)
        self.__syncSelectionWidget = FormMaster.KatanaFactory.ParameterWidgetFactory.buildWidget(self, self.__syncSelectionPolicy)
        secondToolbarLayout.addWidget(self.__syncSelectionWidget)
        secondToolbarLayout.addStretch()


    def __syncSelectionValueChanged(self, event):
        """
        Handler for changes to the B{Sync Selection} parameter.
        """
        self.__mainNode.setSyncSelection(event.getPolicy().getValue())
        if self.__mainNode.getSyncSelection() > SYNC_SELECTION_OFF:
            Utils.EventModule.QueueEvent('dreamGaffer_syncOutgoingSelection', hash(self.__mainNode))

    def __setupTabs(self):
        self.objectTab = ObjectTab.ObjectTab(self, self.__mainNode)
        self.materialTab = MaterialTab.MaterialTab(self, self.__mainNode)
        self.filterTab = FilterTab.FilterTab(self, self.__mainNode)
        self.__tabWidget.addTab(self.objectTab, "Object")
        self.__tabWidget.addTab(self.materialTab, "Material")
        #self.__tabWidget.addTab(self.filterTab, "Light Filter")

    def __configureSceneGraphView(self):
        self.__sceneGraphView.beginColumnConfiguration()
        nameColumn = self.__sceneGraphView.getColumnByName('Name')
        nameColumn.setDataType(ColumnDataType.NameDWA)
        nameColumn.setEditable(True)
        
        muteColumn = self.__sceneGraphView.addColumn('M')
        muteColumn.setDataType(ColumnDataType.MuteSoloDWA)
        muteColumn.setEditable(True)
        muteColumn.setAttributeName("info.light.muteState")

        soloColumn = self.__sceneGraphView.addColumn('S')
        soloColumn.setDataType(ColumnDataType.MuteSoloDWA)
        soloColumn.setEditable(True)
        soloColumn.setAttributeName("info.light.soloState")

        shaderColumn = self.__sceneGraphView.addColumn('Shader')
        shaderColumn.setDataType(ColumnDataType.ShaderDWA)
        shaderColumn.setEditable(True)

        intColumn = self.__sceneGraphView.addColumn("Int")
        intColumn.setDataType(ColumnDataType.NumberDWA)
        intColumn.setEditable(True)
        intColumn.setAttributeName("material.moonrayLightParams.intensity")
        intColumn.setProperty("defaultValue", 1)


        expColumn = self.__sceneGraphView.addColumn("Exp")
        expColumn.setDataType(ColumnDataType.NumberDWA)
        expColumn.setEditable(True)
        expColumn.setAttributeName("material.moonrayLightParams.exposure")
        expColumn.setProperty("defaultValue", 0)


        colorColumn = self.__sceneGraphView.addColumn("Color")
        colorColumn.setDataType(ColumnDataType.ColorDWA)
        colorColumn.setEditable(True)

        colorColumn.setAttributeName("material.moonrayLightParams.color")

        self.__sceneGraphView.setExpandsOnDoubleClick(False)
        self.__sceneGraphView.endColumnConfiguration()

        treeWidget = self.__sceneGraphView.getWidget()
        #nameItemDelegate = treeWidget.itemDelegateForColumn(self.__sceneGraphView.getColumnByName('Name').getIndex())

        self.__sceneGraphView.setContextMenuEventCallback(self.__contextMenuEventHandler)
        self.__sceneGraphView.getSceneGraphChildren(self.__lastRootLocation, self.__rootChildrenChangedCallback)
        self.__sceneGraphView.setSelectionChangedCallback(self.__selectionChangedHandler)

        #self.__sceneGraphView.setLocationAddedOrUpdatedCallback(self.__locationAddedOrUpdatedCallback)

        '''
        self.__sceneGraphView.setAboutToDragCallback(self.__aboutToDragCallback)
        self.__sceneGraphView.setDragMoveEventCallback(self.__dragMoveEventCallback)
        self.__sceneGraphView.setDropEventCallback(self.__dropEventCallback)
        '''

    
    def __showIncomingSceneEvent(self, *args, **kwds):
        value = self.__showIncomingPolicy.getValue()
        self.__mainNode.getParameter("showIncomingScene").setValue(value, 0)
        if value == 1:
            self.__sceneGraphView.setViewNode(self.__mainNode.getViewIncomingNode())
        else:
            self.__sceneGraphView.setViewNode(self.__mainNode.getViewNewNode())

    def __locationAddedOrUpdatedCallback(self, locationPath, topLevelLocationPath):
        """
        Callback function for the Geolib3 Runtime that is called when a scene
        graph location has been added or updated.

        Allows us to update the display state of packages that correspond to
        the location with the given path underneath the top-level location with
        the given path.
        """
        #print "__locationAddedOrUpdatedCallback"
        self.__restoreExpansionStateFor(locationPath, topLevelLocationPath)
        self.__restoreSelectionStateFor(locationPath, topLevelLocationPath)
        '''
        if locationPath in self.__locationPathsOfPackagesToSelect:
            self.__sceneGraphView.selectLocations([
             (
              topLevelLocationPath, locationPath)], replaceSelection=self.__replaceSelection)
            self.__locationPathsOfPackagesToSelect.remove(locationPath)
            self.__sceneGraphView.scrollToLocation(topLevelLocationPath, locationPath)
        '''
        #self.__tabAreaHandleChangesForLocation(locationPath)

    def __saveCurrentSelectionAndExpansionState(self, oldRootLocation):
        """
        Saves the current selection and expansion state. Called when the root
        location of the SuperTool changes.

        @type oldRootLocation: C{str}
        @param oldRootLocation: The path to the old root location, before it
            was changed.
        """
        self.__oldSelectionState = list(self.__sceneGraphView.getSelectedLocations())
        self.__sceneGraphView.clearExpandedLocationsRegistry()
        self.__expansionState = self.__sceneGraphView.saveExpandedLocations()
        self.__oldRootLocation = oldRootLocation

    def __restoreExpansionStateFor(self, locationPath, topLevelLocation):
        if self.__expansionState is None:
            return
        else:
            currentRootLocation = self.__mainNode.getRootLocation()
            oldPath = locationPath.replace(currentRootLocation, self.__oldRootLocation)
            oldTopLevelPath = topLevelLocation.replace(currentRootLocation, self.__oldRootLocation)
            testLocation = (
             oldPath, oldTopLevelPath)
            if testLocation in self.__expansionState:
                self.__sceneGraphView.setLocationExpanded(topLevelLocation, locationPath)
                self.__expansionState.discard(testLocation)
            if len(self.__expansionState) == 0:
                self.__expansionState = None
            return

    def __restoreSelectionStateFor(self, locationPath, topLevelLocation):
        if self.__oldSelectionState is None:
            return
        else:
            currentRootLocation = self.__mainNode.getRootLocation()
            oldPath = locationPath.replace(currentRootLocation, self.__oldRootLocation)
            oldTopLevelPath = topLevelLocation.replace(currentRootLocation, self.__oldRootLocation)
            testLocation = (
             oldTopLevelPath, oldPath)
            if testLocation in self.__oldSelectionState:
                self.__sceneGraphView.setSelectionState(topLevelLocation, locationPath, True)
                self.__oldSelectionState.remove(testLocation)
            if len(self.__oldSelectionState) == 0:
                self.__oldSelectionState = None
            return

    def __rootChildrenChangedCallback(self, locationPath, children):
        currentRootLocation = self.__mainNode.getRootLocation()
        if locationPath != currentRootLocation:
            self.__updateCurrentItem()
            return False
        self.__restoreTopLevelLocations(children, currentRootLocation)
        self.__updateCurrentItem()
        return True

    def __restoreTopLevelLocations(self, children, currentRootLocation):
        topLevelLocationPaths = [ '%s/%s' % (currentRootLocation, childName) for childName in children
                                ]
        self.__sceneGraphView.setTopLevelLocations(topLevelLocationPaths)

    def __rootLocationPolicyEvent(self, *args, **kwds):
        rootLocationPath = self.__rootLocPolicy.getValue()
        oldRootLocationPath = self.__mainNode.getRootLocation()
        self.__mainNode.getParameter("rootLocation").setValue(rootLocationPath, 0)
        if oldRootLocationPath != rootLocationPath:
            self.__saveCurrentSelectionAndExpansionState(oldRootLocationPath)
        self.__sceneGraphView.getSceneGraphChildren(rootLocationPath, self.__rootChildrenChangedCallback)


    def __updateCurrentItem(self):
        selectedLocations = self.__sceneGraphView.getSelectedLocations()
        for topLevelLocationPath, locationPath in selectedLocations:
            self.__sceneGraphView.setSelectionState(topLevelLocationPath, locationPath, True)

    def __contextMenuEventHandler(self, contextMenuEvent, menu):
        self.__populateStandardContextMenuItems(menu)
    
    def __populateStandardContextMenuItems(self, menu):
        from UI4.App.KeyboardShortcutManager import CreateAction, GetActionID

        sgvActions = menu.actions()
        for action in sgvActions:
            menu.removeAction(action)
        action = CreateAction(GetActionID(self.getSuperToolName(), 'Add Light'), self.__sceneGraphViewWidget, text='Add Light')
        menu.addAction(action)
        action = CreateAction(GetActionID(self.getSuperToolName(), 'Add Rig'), self.__sceneGraphViewWidget, text='Add Rig')
        menu.addAction(action)
        action = CreateAction(GetActionID(self.getSuperToolName(), 'Add Master Material'), self.__sceneGraphViewWidget, text='Add Master Material')
        menu.addAction(action)
        action = CreateAction(GetActionID(self.getSuperToolName(), 'Add Master Filter'), self.__sceneGraphViewWidget, text='Add Light Filter')
        menu.addAction(action)
        menu.addSeparator()
        action = CreateAction(GetActionID(self.getSuperToolName(), 'Delete'), self.__sceneGraphViewWidget, text='Delete')
        menu.addAction(action)    

        action = CreateAction(GetActionID(self.getSuperToolName(), 'Edit'), self.__sceneGraphViewWidget, text='Edit')
        menu.addAction(action)
        action = CreateAction(GetActionID(self.getSuperToolName(), 'Duplicate'), self.__sceneGraphViewWidget, text='Duplicate')
        menu.addAction(action)

        menu.addSeparator()
        
        for action in sgvActions:
            menu.addAction(action)

    def getMainNode(self):
        return self.__mainNode

    def getRelativePathToRoot(self, path):
        if path == "":
            return path
        if path == self.__rootLocPolicy.getValue():
            return ""
        return os.path.relpath(path, self.__rootLocPolicy.getValue())

    def getFirstSelectedLoation(self):
        selectedLocations = self.__sceneGraphView.getSelectedLocations()
        if selectedLocations:
            return selectedLocations[0][1]
    
    def getSelectedLocations(self):
        selectedLocations = self.__sceneGraphView.getSelectedLocations()
        result = []
        for each in selectedLocations:
            result.append(each[1])
        return result

    @Decorators.undogroup('DreamGaffer Delete locations')
    def __deleteLocations(self):
        locations = self.getSelectedLocations()
        for fullpath in locations:
            parentPath = os.path.dirname(fullpath)
            attrs = self.__sceneGraphView.getSceneGraphAttributes(fullpath)
            locationType = attrs.getChildByName("type").getValue()
            self.__control.removeLocation(self.getRelativePathToRoot(parentPath), self.getRelativePathToRoot(fullpath), locationType)
    
    @Decorators.undogroup('DreamGaffer Duplicate locations')
    def __duplicateLocation(self):
        try:
            locations = self.getSelectedLocations()
            for fullpath in locations:
                attrs = self.__sceneGraphView.getSceneGraphAttributes(fullpath)
                locationType = attrs.getChildByName("type").getValue()
                location = self.getRelativePathToRoot(fullpath)
                if self.__control.locationExists(location, locationType):
                    #print self.__control.lightCache.cache
                    print "duplicate location"
                    print location
                    print self.__control.getCacheOfLocationType(locationType).cache
                    self.__control.duplicateLocation(location, locationType)
                else:
                    basename = os.path.basename(fullpath) + "_copy"
                    parentType = None
                    parentPath = self.getRelativePathToRoot(os.path.dirname(fullpath))
                    self.__duplicateUpstreamLocation(fullpath, parentType, basename, parentPath)
        except:
            import traceback
            traceback.print_exc()

    def __duplicateUpstreamLocation(self,  fullpath, parentType, basename, parentPath):
        try:
            attrs = self.__sceneGraphView.getSceneGraphAttributes(fullpath)
            if not attrs:
                print "%s: has no attrs" %fullpath
            else:
                locationType = attrs.getChildByName("type").getValue()
                
                name = self.__control.duplicateIncomingLocation(parentPath, basename, parentType, locationType, attrs)

                children = self.__sceneGraphView.getSceneGraphChildren(fullpath)
                if parentPath!="":
                    newParentPath = parentPath + "/" + name
                else:
                    newParentPath = name
                
                for child in children:
                    childfullpath = fullpath + "/" + child
                    self.__duplicateUpstreamLocation(childfullpath, locationType, child, newParentPath)
        except:
            import traceback
            traceback.print_exc()
            print fullpath

    @Decorators.undogroup('DreamGaffer Edit incoming location')
    def __editLocations(self):
        locations = self.getSelectedLocations()
        for fullpath in locations:
            location = self.getRelativePathToRoot(fullpath)
            self.__control.editLocation(location)
            attrs = self.__sceneGraphView.getSceneGraphAttributes(fullpath)
            if attrs.getChildByName("xform.matrix"):
                matrix = attrs.getChildByName("xform.matrix").getNearestSample(0)
                self.__mainNode.getDownstreamEditNode().setXformWithMatrix(location, matrix)
            
        self.objectTab.setDirty(True)
        self.materialTab.setDirty(True) 
        self.filterTab.setDirty(True)
        self.__tabWidget.currentWidget().reloadTab(self.__selectedPath, self.__currentLocation, self.__currentLocationType)

    @Decorators.undogroup('DreamGaffer Add light')
    def __addLight(self):
        try:
            parentPath = ""
            parentType = None
            basename = "light"
            locationType = "light"
            if self.__currentLocation:
                if self.__currentLocationType in ["light", "rig"]:
                    parentPath = self.__currentLocation
                    parentType = self.__currentLocationType
            self.__control.addLocation(parentPath, basename, parentType, locationType)
        except:
            #print self.__control.lightCache.cache
            import traceback
            traceback.print_exc()
    
    @Decorators.undogroup('DreamGaffer Add rig')
    def __addRig(self):
        parentPath = ""
        parentType = None
        basename = "rig"
        locationType = "rig"
        if self.__currentLocation:
            if self.__currentLocationType == "rig":
                parentPath = self.__currentLocation
                parentType = self.__currentLocationType
        self.__control.addLocation(parentPath, basename, parentType, locationType)

    @Decorators.undogroup('DreamGaffer Add material')
    def __addMaterial(self):
        parentPath = ""
        parentType = None
        basename = "mm"
        locationType = "material"

        if self.__currentLocation:
            if self.__currentLocationType == "material":
                parentPath = self.__currentLocation
                parentType = self.__currentLocationType
        self.__control.addLocation(parentPath, basename, parentType, locationType)
    
    '''
    @Decorators.undogroup('DreamGaffer Add filter')
    def __addFilter(self):
        parentPath = ""
        parentType = None
        basename = "lightFilter"
        locationType = "light filter"
        if self.__currentLocation:
            if self.__currentLocationType == "light filter":
                parentPath = self.__currentLocation
                parentType = self.__currentLocationType
        self.__control.addLocation(parentPath, basename, parentType, locationType)
    '''

    @Decorators.undogroup('DreamGaffer Add filter')
    def __addFilter(self):
        parentPath = ""
        parentType = None
        basename = "lightFilter"
        locationType = "light filter"
        if self.__currentLocation:
            if self.__currentLocationType == "light filter" or self.__currentLocationType == "light":
                parentPath = self.__currentLocation
                parentType = self.__currentLocationType
        self.__control.addLocation(parentPath, basename, parentType, locationType)


    def __updateRootLocation(self, currentRoot):
        if currentRoot == self.__lastRootLocation:
            return
        self.__sceneGraphView.setLocationActive(currentRoot)
        self.__lastRootLocation = currentRoot
        #self.__updateTerminalOps()


    def __on_scenegraphManager_selectionChanged(self, *args):
        if self.__mainNode.getSyncSelection() <= SYNC_SELECTION_OUT:
            return
        else:
            scenegraph = ScenegraphManager.getActiveScenegraph()
            if not scenegraph:
                return
            rootPath = self.__mainNode.getRootLocation()
            locationsPaths = set(scenegraph.getSelectedLocations())
            scopedLocations = set(path for path in locationsPaths if path != rootPath and SceneGraphLocationTranslation.IsLocationUnderTopLevelLocation(path, rootPath))
            if not scopedLocations:
                return
            self.__sceneGraphView.setSelectionChangedCallback(None)
            selectedLocations = self.__sceneGraphView.getSelectedLocations()
            locationsPaths = set(locationPath for topLevelLocationPath, locationPath in selectedLocations)                
            newlySelected = scopedLocations.difference(locationsPaths)
            newlyUnselected = locationsPaths.difference(scopedLocations)

            for locationPath in newlyUnselected:
                self.__sceneGraphView.setSelectionState(None, locationPath, False)

            rootTokens = rootPath.split('/')
            for locationPath in newlySelected:
                locationTokens = locationPath.split('/')
                topLevelLocationPath = ('/').join(locationTokens[:len(rootTokens) + 1])
                for i in xrange(len(rootTokens) + 1, len(locationTokens)):
                    self.__sceneGraphView.setLocationExpanded(topLevelLocationPath, ('/').join(locationTokens[:i]))

                self.__sceneGraphView.setSelectionState(None, locationPath, True)
            
            self.__selectionChangedHandler(syncSelection=False)
            self.__sceneGraphView.setSelectionChangedCallback(self.__selectionChangedHandler)

    def __selectionChangedHandler(self, syncSelection=True):
        self.onSelectionChanged()
        if syncSelection and self.__mainNode.getSyncSelection() > SYNC_SELECTION_OFF:
            Utils.EventModule.QueueEvent('dreamGaffer_syncOutgoingSelection', hash(self.__mainNode))

    def __syncSelectionEvent(self, args):
        """
        Callback for C{dreamGaffer_syncOutgoingSelection} events.

        @type args: C{list}
        @param args: arguments C{list} of C{tuple} composed of a C{str} with the
            eventType, a hashable value with the eventID, and a C{dict} with
            additional keyword arguments.
        """
        for arg in args:
            if arg[1] == hash(self.__mainNode):
                scenegraph = ScenegraphManager.getActiveScenegraph()
                paths = [ locationPath for _topLevelLocationPath, locationPath in self.__sceneGraphView.getSelectedLocations()
                        ]
                if not scenegraph or not paths:
                    continue
                for path in paths:
                    scenegraph.ensureLocationVisible(path)

                scenegraph.addSelectedLocations(paths, True)

    def onSelectionChanged(self):
        Utils.UndoStack.DisableCapture()
        try:
            location = self.getFirstSelectedLoation()
            if not location:
                self.__selectedPath = None
                self.__currentLocation = None
                self.__currentLocationType = None
                return
            attrs = self.__sceneGraphView.getSceneGraphAttributes(location)
            self.__currentLocationType = attrs.getChildByName("type").getValue()
            self.__currentLocation = self.getRelativePathToRoot(location)
            self.__selectedPath = location
            self.objectTab.setDirty(True)
            self.materialTab.setDirty(True)
            self.filterTab.setDirty(True)
            self.__tabWidget.currentWidget().reloadTab(self.__selectedPath, self.__currentLocation, self.__currentLocationType)
        except:
            import traceback
            traceback.print_exc()
        finally:
            Utils.UndoStack.EnableCapture()

    def reloadMaterialTab(self):
        #print "reloadMaterialTab"
        self.materialTab.reloadTab(self.__selectedPath, self.__currentLocation, self.__currentLocationType)

    def getInternalControl(self):
        return self.__control

    def __updateCB(self, args):
        try:
            if self.__systemSetValue:
                return
            for arg in args:
                if arg[0] not in 'parameter_finalizeValue':
                    return
                node = arg[2].get('node')
                param = arg[2].get('param')
                paramName = Helper.stripNodeName(param.getFullName())
                if paramName.startswith("transform") or paramName.startswith("args"):
                    self.processXformEdit(param, node)
                elif paramName.startswith("shaders"):
                    self.processMaterialEdit(param, node, paramName)
                self.__updateOnIdle = True
            return
        except:
            import traceback
            traceback.print_exc()

    def selectLocations(self, locations):
        replaceSelection = True
        self.__sceneGraphView.selectLocations(locations, replaceSelection)

    def processXformEdit(self, param, node):
        if self.__currentLocation and self.__currentLocationType:
            if node in [self.__mainNode.getLightCreateNode(), self.__mainNode.getRigCreateNode()]:
                index = self.__control.getIndexForLocation(self.__currentLocation, self.__currentLocationType)
                node.setParamAtIndex(param, index)
            elif node == self.__mainNode.getProxyXformNode():
                self.__mainNode.getDownstreamEditNode().setParamEditForLocation(self.__currentLocation, param, isXform=True)
    
    def processMaterialEdit(self, param, node, paramName):
        if self.__currentLocation and self.__currentLocationType:
            if node == self.__mainNode.getProxyMaterialNode() or node == self.__mainNode.getProxyFilterNode():
                if paramName.startswith("shaders.moonrayLightShader") or paramName.startswith("shaders.moonrayLightfilterShader"):
                    if param.getName()=="value":
                        self.__control.setMaterialForLocation(self.__currentLocationType, param.getValue(0), self.__currentLocation)
                else:
                    self.__mainNode.getMaterialEditNode().setParamEditForLocation(self.__currentLocation, param)
            elif node == self.__mainNode.getProxyDownstreamMaterialNode():
                self.__mainNode.getDownstreamEditNode().setParamEditForLocation(self.__currentLocation, param)

    '''
    def __idle_callback(self, *args, **kwargs):
        if self.__updateOnIdle:
            self.__updateOnIdle = False
    '''
    def showEvent(self, event):
        QtWidgets.QWidget.showEvent(self, event)
        if self.__frozen:
            self._thaw()

    def hideEvent(self, event):
        QtWidgets.QWidget.hideEvent(self, event)
        if not self.__frozen:
            self._freeze()

    def _thaw(self):
        
        if not self.__frozen:
            return
        else:
            self.__frozen = False
            currentRoot = self.__mainNode.getParameter("rootLocation").getValue(0)
            self.__updateRootLocation(currentRoot)
        
            self.__setupEventHandlers(True)

    def _freeze(self):
        
        if self.__frozen:
            return
        else:
            self.__frozen = True
            self.__setupEventHandlers(False)
    
    def systemSetValue(self, value):
        self.__systemSetValue = value

    def __setupEventHandlers(self, enabled):
        #Utils.EventModule.RegisterEventHandler(self.__idle_callback, 'event_idle', enabled=enabled)
        Utils.EventModule.RegisterCollapsedHandler(self.__updateCB, 'parameter_finalizeValue', enabled=enabled)
        Utils.EventModule.RegisterCollapsedHandler(self.__syncSelectionEvent, 'dreamGaffer_syncOutgoingSelection', hash(self.__mainNode), enabled=enabled)
        Utils.EventModule.RegisterCollapsedHandler(self.__on_scenegraphManager_selectionChanged, 'scenegraphManager_selectionChanged', enabled=enabled)
        Utils.EventModule.RegisterCollapsedHandler(self.__viewerOverrideXformCallback, 'dreamGaffer_viewerOverrideXform', enabled=enabled)
        #Utils.EventModule.RegisterCollapsedHandler(self.__updateCB, 'port_connect', enabled=enabled)
        #Utils.EventModule.RegisterCollapsedHandler(self.__updateCB, 'port_disconnect', enabled=enabled)
    
    def __viewerOverrideXformCallback(self, *args):
        self.objectTab.setDirty(True)
        if self.__tabWidget.currentWidget() == self.objectTab:
            self.__systemSetValue = True
            self.objectTab.reloadTabForAdoptedLocation(self.__selectedPath, self.__currentLocation, self.__currentLocationType)
            self.__systemSetValue = False

    ######################################
    '''
    def __getInternalViewPort(self):
        if self.__mainNode.getShowIncomingScene():
            postMergeViewNode = NU.GetRefNode(self.__mainNode, 'postMergeView')
            if postMergeViewNode is None:
                return
            return postMergeViewNode.getOutputPortByIndex(0)
        else:
            editStack = NU.GetRefNode(self.__mainNode, 'editPackageStack')
            if editStack and editStack.getNumChildren():
                postMergeViewNode = NU.GetRefNode(self.__mainNode, 'postMergeView')
                if postMergeViewNode is None:
                    return
                return postMergeViewNode.getOutputPortByIndex(0)
            mergeView = NU.GetRefNode(self.__mainNode, 'preMergeView')
            if mergeView is None:
                return
            return mergeView.getOutputPortByIndex(0)
    
    def __terminalOpCallback(self, portOpClient, op, graphState, txn):
        port = portOpClient.port
        if port != self.__viewedPort:
            return
        if op != self.__viewedOp:
            self.__viewedOp = op
        self.__sceneGraphView.setViewOp(self.__viewedOp)
        self.__updateTerminalOps(graphState, txn)
    

    def __updatePort(self, port):
        if self.__viewedPort == port:
            return False
        else:
            self.__viewedPort = port
            if self.__viewedPort is None:
                Nodes3DAPI.UnregisterPortOpClient(self.__portOpClient)
                self.__sceneGraphView.setViewOp(None)
                return True
            if self.__portOpClient is None:
                mainNode = self.getMainNode()
                searchPort = mainNode.getOutputPortByIndex(0)
                nodeTraversalEndpointsSpec = Nodes3DAPI.PortOpClient.NodeTraversalEndpointsSpec(searchPort)
                self.__portOpClient = Nodes3DAPI.PortOpClient.CallbackPortOpClient(self.__viewedPort, self.__terminalOpCallback, nodeTraversalEndpointsSpec=nodeTraversalEndpointsSpec)
                Nodes3DAPI.RegisterPortOpClient(self.__portOpClient)
            else:
                self.__portOpClient.port = self.__viewedPort
                Nodes3DAPI.MarkPortOpClientDirty(self.__portOpClient)
            return True
    '''