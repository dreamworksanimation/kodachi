# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtCore, QtGui, QtWidgets, ScenegraphManager

from UI4.Widgets.SceneGraphView.ItemDelegates import BaseItemDelegate
from UI4.Widgets.SceneGraphView.ColumnDataType import RegisterDataType
from UI4.Widgets.SceneGraphView import SceneGraphViewIconManager
from UI4.Util import ScenegraphIconManager
from UI4.Widgets.DoubleSpinBox import DoubleSpinBox
from Nodes3DAPI import UpdateModes
import os

class NumberItemDelegateDWA(BaseItemDelegate):
    Precision = 2
    DefaultColumnWidth = 60

    @staticmethod
    def __formatFloat(value, precision):
        """
        Formats a floating value with two digits after the decimal point.
        """
        return str('%.*f' % (precision, value))

    def __init__(self, bridge, treeWidget, parent=None):
        BaseItemDelegate.__init__(self, bridge, treeWidget, parent)
        self.currentLocation = ""
        self.isDownstreamEdit = False

        self.gafferEditor = self.parent().parent()
        self.closeEditor.connect(self.__on_closeEditor)

    def setModelData(self, editor, model, index):
        pass


    @classmethod
    def isPropertyValid(cls, propertyName):
        """
        Determines if the given property name is a valid, known property on
        the item delegate.

        @type propertyName: C{str}
        @rtype: C{bool}
        @param propertyName: The name of the property to validate.
        @return: C{True} if the property is valid, C{False} otherwise.
        """
        return True

    def initStyleOption(self, option, index):
        attributeName = self.getAttributeName()
        if not attributeName:
            return
        BaseItemDelegate.initStyleOption(self, option, index)
        locationPath = self._getLocationPath(index)

        if locationPath is None:
            return
        
        locationType = self._getLocationType(locationPath)
        location = self.gafferEditor.getRelativePathToRoot(locationPath)
            
        attr = self._getAttribute(locationPath, attributeName)
        if attr:
            option.text = ' %s' % self.__formatFloat(float(attr.getValue()), NumberItemDelegateDWA.Precision)
            if self.isLocationEditable(location, locationType):
                option.palette.setColor(QtGui.QPalette.Text, QtGui.QColor(255, 200, 0)) # Local yellow
            else:
                option.palette.setColor(QtGui.QPalette.Text, QtGui.QColor(135, 118, 54)) # incoming_local
        else:
            value = self.getParentAttrValue(locationPath, attributeName, locationType)
            if value != None:
                option.text = ' %s' % self.__formatFloat(float(value), NumberItemDelegateDWA.Precision)
                option.palette.setColor(QtGui.QPalette.Text, QtGui.QColor(135, 118, 54)) # incoming_local
            else:
                default = self.getProperty("defaultValue")
                if locationType == "light":
                    if default!=None:
                        option.text = ' %s' % self.__formatFloat(float(default), NumberItemDelegateDWA.Precision)
    
    def getParentAttrValue(self, locationPath, attributeName, locationType):
        if locationType!="light":
            return None
        parentPath = os.path.dirname(locationPath)
        if self._getLocationType(parentPath)!="light":
            return None
        attr = self._getAttribute(parentPath, attributeName)
        if attr:
            return attr.getValue()

    def isLocationEditable(self, location, locationType):
        if self.gafferEditor.getInternalControl().locationExists(location, locationType):
            self.isDownstreamEdit = False
            return True
        if self.gafferEditor.getInternalControl().locationAdopted(location):
            self.isDownstreamEdit = True
            return True
        return False

    def createEditor(self, parent, option, index):
        if not self._editable:
            return
        
        locationPath = self._getLocationPath(index)

        if locationPath is None:
            return 
        
        locationType = self._getLocationType(locationPath)
        if locationType != "light":
            return
        location = self.gafferEditor.getRelativePathToRoot(locationPath)
        
        if not self.isLocationEditable(location, locationType):
            return
                    
        self.currentLocation = location
        

        editorClass = self.getEditorClass()
        if editorClass is not None:
            result = editorClass(parent)
        return result

    def getEditorClass(self):
        return DoubleSpinBox

    def __on_closeEditor(self, editor, endEditHint):
        """
        Is called by a slot that is connected to the C{closeEditor()} signal
        when the given editor widget of an item has been closed, meaning that
        editing has been finished, and the value is to be submitted or
        discarded. Allows derived classes to customize behavior depending on
        the data type of the respective item delegate.

        The default implementation closes any pop-up widget, if one is
        currently displayed.

        @type editor: C{QtWidgets.QWidget}
        @type endEditHint: C{QtWidgets.QAbstractItemDelegate.EndEditHint}
        @param editor: The editor widget that was closed.
        @param endEditHint: Hint that provides a way for the delegate to
            influence how the model and view behave after editing is completed.
        """
        value = float(editor.text())

        attrName = self.getAttributeName()
        editDict = {self.currentLocation: {attrName: {"type": "FloatAttr", "enable": 1, "value": value}}}
        self.gafferEditor.getMainNode().editAttributes(editDict, self.isDownstreamEdit)


RegisterDataType('NumberDWA', NumberItemDelegateDWA)
