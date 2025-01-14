# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import os, weakref
from Katana import FnAttribute, QtWidgets
from PluginAPI.BaseViewerPluginExtension import BaseViewerPluginExtension
from UI4.App.KeyboardShortcutManager import KeyboardShortcutManager

# helper to insert a viewport layer
def insertLayerBefore(name, viewportWidget, afterName):
    for i in range(0, viewportWidget.getNumberOfLayers()):
        if viewportWidget.getLayerName(i) == afterName:
            viewportWidget.insertLayer(name, name, i)
            return
    # append to end if no match or if it matches the last name:
    viewportWidget.addLayer(name, name)

class MoonrayLightViewerPluginExtension(BaseViewerPluginExtension):
    def __init__(self):
        BaseViewerPluginExtension.__init__(self)
        self.layerName = "MoonrayLightLayer"
        self.viewportWidgets = []

    def onDelegateCreated(self, viewerDelegate, pluginName):
        NAME = 'MoonrayLightDelegateComponent'
        viewerDelegate.addComponent(NAME, NAME)
        
    def onViewportCreated(self, viewportWidget, pluginName, viewportName):
        # The lights are wireframes, so draw then along with the grid so antialising works
        insertLayerBefore(self.layerName, viewportWidget, "GridLayer")
        def removeViewport(viewportRef):
            self.viewportWidgets.remove(viewportRef)
        self.viewportWidgets.append(weakref.ref(viewportWidget, removeViewport))

    def toggled(self, state):
        for viewportRef in self.viewportWidgets:
            viewport = viewportRef()
            if viewport:
                layer = viewport.getLayer(self.layerName)
                if layer:
                    layer.setOptionByName("allLightCones", FnAttribute.IntAttribute(state))

    _createdShortcuts = False

    def onTabCreated(self, tab):
        def findAction(menu, text):
            for i in menu.actions():
                if i.text() == text: return i
            return None
        name = "All Light Cones"
        shortcut = 'L'
        menu = findAction(tab.getMenuBar(), 'View').menu()
        action = QtWidgets.QAction(name + '\t' + shortcut, menu)
        action.setCheckable(True)
        action.setChecked(False)
        action.toggled[bool].connect(self.toggled)
        menu.insertAction(findAction(menu, 'HUD\t'), action)
        # use the shortcut manager to make the shortcut only work in viewer
        tab.allLightConesAction = action
        if self._createdShortcuts: return
        id = tab.getShortcutsContextName() + '.' + "Toggle " + name
        KeyboardShortcutManager.RegisterKeyEvent(
            tab.__class__.__name__,
            id,
            id,
            shortcut,
            lambda tab: tab.allLightConesAction.trigger())
        self.__class__._createdShortcuts = True

PluginRegistry = [
    ("ViewerPluginExtension", 1, "MoonrayLightViewerPluginExtension", MoonrayLightViewerPluginExtension)
]

def registerResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute
    
    # Light Inheritance resolver
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "ReferentialInheritanceResolve", FnAttribute.GroupAttribute(), addSystemArgs=True)

    # So light type information is passed on even if all values are default
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "MoonrayCookLightDAPs", FnAttribute.GroupAttribute(), addSystemArgs=True)

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "MoonrayLightFilterReferencesResolve", FnAttribute.GroupAttribute(), addSystemArgs=True)
    
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "MoonrayLightLookthroughAttributeSet", FnAttribute.GroupAttribute(), addSystemArgs=True)

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "MoonrayLightGeometry", FnAttribute.GroupAttribute(), addSystemArgs=True)

registerResolvers()
