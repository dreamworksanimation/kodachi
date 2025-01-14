# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QT4Color, QtCore, QtGui, QtWidgets, ScenegraphManager, UI4

from UI4.Widgets.SceneGraphView.ItemDelegates import BaseItemDelegate
from UI4.Widgets.SceneGraphView.ColumnDataType import RegisterDataType
from UI4.Widgets.SceneGraphView import SceneGraphViewIconManager
from UI4.Util import ScenegraphIconManager
from UI4 import FormMaster
import os

class ColorItemDelegateDWA(BaseItemDelegate):
    """
    Class representing an item delegate for editing color component values.
    """
    ColorSwatchWidth = 50
    Padding = 2
    PaddingRight = 26
    DefaultColumnWidth = ColorSwatchWidth + PaddingRight
    MaximumColumnWidth = 150
    
    def __init__(self, bridge, treeWidget, parent=None):
        BaseItemDelegate.__init__(self, bridge, treeWidget, parent)
        self.gafferEditor = self.parent().parent()
        self.currentLocation = ""
        self.isDownstreamEdit = False
        self.__enableFilmlook = True

    def __getParameter(self, index):
        locationPath = self._getLocationPath(index)
        if locationPath is None:
            return 
        locationType = self._getLocationType(locationPath)
        location = self.gafferEditor.getRelativePathToRoot(locationPath)
        self.currentLocation = location
        paramName = 'shaders.moonrayLightParams.color'
        if self.gafferEditor.getInternalControl().locationExists(location, locationType):
            self.isDownstreamEdit = False
            self.gafferEditor.selectLocations([locationPath])
            self.gafferEditor.reloadMaterialTab()
            
            return self.gafferEditor.getMainNode().getProxyMaterialNode().getParameter(paramName)
        if self.gafferEditor.getInternalControl().locationAdopted(location):
            self.isDownstreamEdit = True
            self.gafferEditor.selectLocations([locationPath])
            self.gafferEditor.reloadMaterialTab()
            return self.gafferEditor.getMainNode().getProxyDownstreamMaterialNode().getParameter(paramName)
        return

    def createEditor(self, parent, option, index):
        param = self.__getParameter(index)
        if not param :
            return
        
        locationPath = self._getLocationPath(index)
        locationType = self._getLocationType(locationPath)
        if locationType!="light":
            return

        paramPolicy = UI4.FormMaster.CreateParameterPolicy(None, param)

        colorAccepted = UI4.Util.Color.ShowColorPicker(paramPolicy)
        if colorAccepted == QtWidgets.QDialog.Accepted:
            value = paramPolicy.getValue()
            #print value
            attrName = self.getAttributeName()
            editDict = {self.currentLocation: {attrName: {"type": "FloatAttr", "enable": 1, "value": value}}}
            self.gafferEditor.getMainNode().editAttributes(editDict, self.isDownstreamEdit)

        #discard = colorAccepted != QtWidgets.QDialog.Accepted
        
  
    def getParentAttrValue(self, locationPath, attributeName):
        parentPath = os.path.dirname(locationPath)
        if self._getLocationType(parentPath)!="light":
            return None
        attr = self._getAttribute(parentPath, attributeName)
        if attr:
            return attr.getNearestSample(0)


    def paint(self, painter, option, index):
        attributeName = self.getAttributeName()
        if not attributeName:
            return
        locationPath = self._getLocationPath(index)
        locationType = self._getLocationType(locationPath)
        if locationType!="light":
            return

        attr = self._getAttribute(locationPath, attributeName)
        values = [1, 1, 1]
        if attr:
            values = attr.getNearestSample(0)
        else:
            parentValue = self.getParentAttrValue(locationPath, attributeName)
            if parentValue!=None:
                values = parentValue
        
        # get state 
        # state = FormMaster.States.DEFAULT
        # state = FormMaster.States.INCOMING_LOCAL        
        state = FormMaster.States.DEFAULT
        _stateLetter, stateColor = UI4.FormMaster.States.GetStateAppearance(state)
        if stateColor is None:
            stateColor = option.palette.color(QtGui.QPalette.Midlight)

        if stateColor is not None:
            rect = QtCore.QRect(option.rect.left() + 4, option.rect.top() + option.rect.height() / 2 - 3, 6, 6)
            painter.save()
            painter.setBrush(QtGui.QBrush(stateColor))
            painter.setPen(QtGui.QPen(option.palette.color(QtGui.QPalette.Dark)))
            painter.drawRect(rect)
            painter.restore()
            stateBadgeOffset = rect.width() + 8
        else:
            stateBadgeOffset = 0

        swatchColors = QT4Color.Swatches.GetSwatchColors(values, enableFilmlook=self.__enableFilmlook, enableNoFilmlookColorSpace=True)
        if swatchColors:
            color = swatchColors[0]
        else:
            return

        rectWidth = option.rect.width()
        if rectWidth > ColorItemDelegateDWA.DefaultColumnWidth:
            rightPadding = ColorItemDelegateDWA.PaddingRight
        elif rectWidth > ColorItemDelegateDWA.ColorSwatchWidth + ColorItemDelegateDWA.Padding:
            rightPadding = rectWidth - ColorItemDelegateDWA.ColorSwatchWidth
        else:
            rightPadding = ColorItemDelegateDWA.Padding
        
        colorSwatchRect = option.rect.adjusted(ColorItemDelegateDWA.Padding + stateBadgeOffset, ColorItemDelegateDWA.Padding, -rightPadding, -ColorItemDelegateDWA.Padding)
        painter.fillRect(colorSwatchRect, color)


    
RegisterDataType('ColorDWA', ColorItemDelegateDWA)