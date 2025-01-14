# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtCore, QtGui, QtWidgets, ScenegraphManager, FnAttribute

from UI4.Widgets.SceneGraphView.ItemDelegates import BaseItemDelegate
from UI4.Widgets.SceneGraphView.ColumnDataType import RegisterDataType
from UI4.Widgets.SceneGraphView import SceneGraphViewIconManager
from UI4.Util import ScenegraphIconManager

class NameItemDelegateDWA(BaseItemDelegate):
    __greyColor = None
    __italicFont = None
    DefaultColumnWidth = 200
    MaximumColumnWidth = None
    PolishOpAttributeName = '__sceneGraphTab'

    def __init__(self, bridge, treeWidget, parent=None):
        BaseItemDelegate.__init__(self, bridge, treeWidget, parent)
        self.gafferEditor = self.parent().parent()


    def setModelData(self, editor, model, index):
        """
        Updates the item at the given index in the given model with data from
        the given editor widget.
        """
        locationPath = self._getLocationPath(index)
        locationType = self._getLocationType(locationPath)
        newName = str(editor.text())
        locationPath = self.gafferEditor.getRelativePathToRoot(locationPath)
        self.gafferEditor.getInternalControl().rename(locationPath, newName, locationType)


    def initStyleOption(self, option, index):
        """
        Initializes the given style option with the values of the item with the
        given model index.
        """
        BaseItemDelegate.initStyleOption(self, option, index)
        locationPath = self._getLocationPath(index)
        if locationPath is None:
            return
        locationType = self._getLocationType(locationPath)

        if locationType is not None:
            '''
            # todo apply light type icons..
            attr = self._getAttribute(locationPath, "material.moonrayLightShader")
            if attr: 
                lightType = attr.getValue()
                #print lightType
            '''
            self.__applySceneGraphLocationTypeIcon(option, locationType)
            location = self.gafferEditor.getRelativePathToRoot(locationPath)
            locationExists = self.gafferEditor.getInternalControl().locationExists(location, locationType)
            if not locationExists:
                locationAdopted = self.gafferEditor.getInternalControl().locationAdopted(location)
                if locationAdopted:
                    standardFont = option.font
                    italicFont = QtGui.QFont(standardFont)
                    italicFont.setItalic(True)
                    option.font = italicFont
                else:
                    option.palette.setColor(QtGui.QPalette.Text, self._getGreyColor())
        
        self._applyIconDecorations(option, index)


    def _getGreyColor(self):
        """
        From the current palette of the tree widget, calculate a color value
        mid-way between text color and base color, and store it as our grey
        value to use for displaying attribute data item values.

        @rtype: {QtGui.QColor}
        @return: A color which will be visible but low-contrast in the current
            palette.
        """
        if not self.__class__.__greyColor:
            palette = self._treeWidget.palette()
            dark = palette.base().color()
            light = palette.text().color()
            grey = QtGui.QColor((dark.red() + light.red()) / 2.0, (dark.green() + light.green()) / 2.0, (dark.blue() + light.blue()) / 2.0)
            self.__class__.__greyColor = grey
        return self.__class__.__greyColor

    def __applySceneGraphLocationTypeIcon(self, option, locationType):
        """
        Modifies the given style option to add an icon for the given scene
        graph location type with an appropriate icon size.

        @type option: C{QtWidgets.QStyleOptionViewItem}
        @type locationType: C{str}
        @param option: The style option to modify.
        @param locationType: The type of scene graph location for which to add
            an appropriate icon to the given style option.
        """
        iconSize = self._treeWidget.style().pixelMetric(QtWidgets.QStyle.PM_SmallIconSize, option)
        pixmap = ScenegraphIconManager.GetPixmap(locationType, iconSize)
        icon = QtGui.QIcon(pixmap)
        self.__applyIcon(option, icon, iconSize, iconSize)

    def _applyIconDecorations(self, option, index):
        """
        Sets an icon in the given style option to reflect the scene graph
        location's visibility, mute, and pinning state.

        Can be called in C{initStyleOption()} after C{option.icon} is set, in
        which case the existing icon will be decorated with additional
        graphical elements.

        @type option: C{QtWidgets.QStyleOptionViewItem}
        @type index: C{QtCore.QModelIndex}
        @param option: The style option to modify with icon decorations.
        @param index: The model index of the item for which to apply icon
            decorations.
        """
        locationPath = self._getLocationPath(index)
        locationType = self._getLocationType(locationPath)
        attrs = self._getAttribute(locationPath, "info.light")

        if attrs is not None:
            muteAttr = attrs.getChildByName('mute')
            if muteAttr and muteAttr.getValue() != 0:
                locationType = 'lightmute'
                self.__applySceneGraphLocationTypeIcon(option, locationType)


    def __applyIcon(self, option, icon, width, height):
        """
        Applies the given icon with the given width and height as a decoration
        to the given style option.

        @type option: C{QtWidgets.QStyleOptionViewItem}
        @type icon: C{QtGui.QIcon}
        @type width: C{int}
        @type height: C{int}
        @param option: The style option to apply the given icon to.
        @param icon: The icon to apply to the given style option.
        @param width: The width to use for the style option's decoration.
        @param height: The height to use for the style option's decoration.
        """
        option.features |= QtWidgets.QStyleOptionViewItem.HasDecoration
        option.icon = icon
        option.decorationSize = QtCore.QSize(width, height)

RegisterDataType('NameDWA', NameItemDelegateDWA)