# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtCore, QtGui, QtWidgets, ScenegraphManager

from UI4.Widgets.SceneGraphView.ItemDelegates import BaseItemDelegate
from UI4.Widgets.SceneGraphView.ColumnDataType import RegisterDataType
from UI4.Widgets.SceneGraphView import SceneGraphViewIconManager
from UI4.Util import ScenegraphIconManager


ICONS_SOLO = {'soloEmpty': 'muteEmpty', 
   'soloLocalEmpty': 'muteLocalEmpty', 
   'soloDefaultEmpty': 'muteDefaultEmpty', 
}
class MuteSoloItemDelegateDWA(BaseItemDelegate):
    FixedColumnWidth = 32

    def __init__(self, bridge, treeWidget, parent=None):
        BaseItemDelegate.__init__(self, bridge, treeWidget, parent)
        self.gafferEditor = self.parent().parent()


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
            state = str(attr.getValue())
            if state in ICONS_SOLO.keys():
                state = ICONS_SOLO.get(state)
            stateIcon=SceneGraphViewIconManager.GetIcon(state)
            if stateIcon:
                option.features = QtWidgets.QStyleOptionViewItem.HasDecoration
                option.icon = stateIcon
                option.text = ''

                option.decorationAlignment = QtCore.Qt.AlignCenter
                self._updateColumnSize()
                option.decorationPosition = QtWidgets.QStyleOptionViewItem.Top
    
    def editorEvent(self, event, itemModel, option, index):
        if not self._editable:
            return True
        if event.type() == QtCore.QEvent.MouseButtonDblClick:
            return True
        if event.type() == QtCore.QEvent.MouseButtonPress:
            locationPath = self._getLocationPath(index)
            if locationPath is None:
                return True
            locationType = self._getLocationType(locationPath)
            if locationType not in ["light", "rig"]:
                return True
            location = self.gafferEditor.getRelativePathToRoot(locationPath)
            attributeName = self.getAttributeName()
            if attributeName == "info.light.muteState":
                self.gafferEditor.getInternalControl().updateMuteForLocation(location, locationType)
            elif attributeName == "info.light.soloState":
                self.gafferEditor.getInternalControl().updateSoloForLocation(location, locationType)
            return True
        return False

    def _updateColumnSize(self):
        """
        Resizes the column to fit the current icon size.
        """
        if hasattr(self.__class__, 'FixedColumnWidth'):
            iconSize = self._treeWidget.style().pixelMetric(QtWidgets.QStyle.PM_SmallIconSize, widget=self._treeWidget)
            self.__class__.FixedColumnWidth = 2 * iconSize

RegisterDataType('MuteSoloDWA', MuteSoloItemDelegateDWA)