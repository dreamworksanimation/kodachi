# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

#show the assigned the material.. (the attribute)
#popup window to assign material..
from Katana import QtCore, QtGui, QtWidgets, ScenegraphManager
from Katana import AssetAPI, Nodes3DAPI, QT4FormWidgets, RenderingAPI

from UI4.Widgets.SceneGraphView.ItemDelegates import BaseItemDelegate
from UI4.Widgets.SceneGraphView.ColumnDataType import RegisterDataType
from UI4.Widgets.SceneGraphView import SceneGraphViewIconManager
from UI4.Util import ScenegraphIconManager
from UI4.Widgets import FilterPopups
import re, os
RenderPlugins = RenderingAPI.RenderPlugins

MasterMaterialKey = 'master materials'

class ShaderFilterPopup(FilterPopups.ShaderFilterPopup):
    """
    Class implementing a shader filter popup widget used by the
    L{ShaderItemDelegate} when editing the value of a cell in a column that
    uses the item delegate.
    """

    class ItemDelegate(BaseItemDelegate):
        """
        Class implementing an item delegate used by the
        L{ShaderFilterPopup} for painting tree widget items that represent
        Master Materials in a different style.
        """
        MasterMaterialColor = QtGui.QColor(140, 190, 152)

        def initStyleOption(self, option, index):
            """
            Initializes the given style option with the values of the item
            with the given model index.

            @type option: C{QtWidgets.QStyleOptionViewItem}
            @type index: C{QtCore.QModelIndex}
            @param option: The style option to fill with values of the item
                with the given model index.
            @param index: The model index of the item whose values to use
                for initializing the given style option.
            """
            BaseItemDelegate.initStyleOption(self, option, index)
            treeWidgetItem = self._treeWidget.itemFromIndex(index)
            if treeWidgetItem is None:
                return
            else:
                if treeWidgetItem.meta == MasterMaterialKey:
                    color = self.__class__.MasterMaterialColor
                    palette = QtGui.QPalette(option.palette)
                    palette.setColor(palette.HighlightedText, color)
                    palette.setColor(palette.Text, color)
                    option.palette = palette
                return

    def __init__(self, valuePolicy, mmList, parent=None):
        """
        Initializes an instance of the class.

        @type valuePolicy: C{ValuePolicy}
        @type parent: C{QtWidgets.QWidget} or C{None}
        @param valuePolicy: The value policy to work with, representing a
            shader parameter and providing information about the types of
            shaders to show in the popup.
        @param parent: The parent widget to use for the popup widget, or
            C{None} to use the popup widget as a stand-alone widget.
        """
        FilterPopups.ShaderFilterPopup.__init__(self, valuePolicy, parent)
        self.treeWidget().setUniformRowHeights(True)
        #self.__masterMaterialsRequestCallback = None
        self.__locationPath = None
        self.mmList = mmList
        return


    '''
    def setMasterMaterialsRequestCallback(self, callback, locationPath):
        """
        Sets the callback to use for obtaining a list of paths of scene
        graph locations that represent Master Materials to the given
        callback.

        @type callback: C{callable}
        @type locationPath: C{str}
        @param callback: The callback to use for obtaining a list of paths
            of Master Material scene graph locations.
        @param locationPath: The path of a scene graph location to pass to
            the given callback when it is called.
        @raise ValueError: If the given callback object is not callable.
        """
        if callback is not None and not callable(callback):
            raise ValueError('Given Master Materials request callback is not callable.')
        self.__masterMaterialsRequestCallback = callback
        self.__locationPath = locationPath
        self.setSorting(True)
        treeWidget = self.treeWidget()
        #treeWidget.setItemDelegate(ShaderItemDelegate.ShaderFilterPopup.ItemDelegate(None, treeWidget))
        treeWidget.setItemDelegate(ShaderFilterPopup.ItemDelegate(None, treeWidget))
        return
    '''
    def _refreshContents(self):
        """
        Populates the shader filter popup with items representing shaders
        and Master Materials (if available).
        """
        FilterPopups.ShaderFilterPopup._refreshContents(self)
       
        #result = self.__internalControl.getAvailableMasterMaterials()
        for locationPath in self.mmList:
            self.addItem(locationPath, meta=MasterMaterialKey)

        #locationsOption = self.getOption('Locations')
        #print "locationsOption", locationsOption
        #if locationsOption:
        #    locationsOption.addItems([MasterMaterialKey])
          
    
    def popup(self, globalPos):
        if globalPos is None:
            globalPos = self.__popupPosition
        else:
            self.__popupPosition = globalPos
        self._refreshContents()
        self.applyDefaultSize()
        geometry = QtCore.QRect(self.__popupPosition.x(), self.__popupPosition.y(), self.width(), self.height())
        self.setGeometry(geometry)
        self.show()
        popupGeometry = self.geometry()
        screenGeometry = QtWidgets.QApplication.desktop().availableGeometry(popupGeometry.topLeft())
        if not screenGeometry.contains(popupGeometry):
            popupGeometry.translate(min(0, screenGeometry.right() - popupGeometry.right()), min(0, screenGeometry.bottom() - popupGeometry.bottom()))
            self.setGeometry(popupGeometry)
        return
        
class ShaderItemDelegateDWA(BaseItemDelegate):
    #DefaultColumnWidth = 200
    #MaximumColumnWidth = None

    def __init__(self, bridge, treeWidget, parent=None):
        BaseItemDelegate.__init__(self, bridge, treeWidget, parent)
        self.gafferEditor = self.parent().parent()
        self.__locationPath = None


    def setModelData(self, editor, model, index):
        """
        Updates the item at the given index in the given model with data from
        the given editor widget.
        """
        pass
        

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
        location = self.gafferEditor.getRelativePathToRoot(locationPath)

        attr = self._getAttribute(locationPath, "inherits")
        if attr:
            mm = attr.getValue()
            if mm == "../": # child light. do not show parent's material
                return
            mm = os.path.normpath(os.path.join(location, mm))
            option.text = mm
            return

        attr = self._getAttribute(locationPath, "material.moonrayLightShader")
        if attr: 
            option.text = attr.getValue()
            return
        attr = self._getAttribute(locationPath, "material.moonrayLightfilterShader")
        if attr:
            option.text = attr.getValue()


    def __getShaderSubTypeForLocation(self, locationPath):
        """
        @type locationPath: C{str}
        @rtype: C{str} or C{None}
        @param locationPath: Scene graph location whose shader sub-type should
            be guessed.
        @return: A guess at the possible shader sub-type for the scene graph
            location with the given path, based on the location type, or
            C{None} if the location type is "material" or could not be
            determined.
        """
        locationType = self._getLocationType(locationPath)
        if not locationType or locationType == 'material':
            return None
        return re.sub(' ', '', locationType)

    def createEditor(self, parent, option, index):
        locationPath = self._getLocationPath(index)
        self.__locationPath = locationPath
        locationType = self._getLocationType(self.__locationPath)
        # do not allow child light to edit its material
        attr = self._getAttribute(locationPath, "inherits")
        if attr:
            mm = attr.getValue()
            if mm == "../": # child light. do not show parent's material
                return

        rendererName = RenderingAPI.RenderPlugins.GetDefaultRendererPluginName()
        shaderSubType = self.__getShaderSubTypeForLocation(locationPath)
        if shaderSubType is None:
            shaderSubType = 'light'
        self.__rendererShaderType = '%s%s' % (rendererName,
            shaderSubType.capitalize())
        shaderParameterName = '%sShader' % self.__rendererShaderType
        valuePolicy = QT4FormWidgets.PythonValuePolicy('', {shaderParameterName: ''}).getChildByName(shaderParameterName)
        rendererInfoPluginName = RenderingAPI.RenderPlugins.GetInfoPluginName(rendererName)
        valuePolicy.getWidgetHints()['rendererInfoPluginName'] = rendererInfoPluginName
        valuePolicy.getWidgetHints()['shaderType'] = RenderingAPI.RendererInfo.kRendererObjectTypeShader
        valuePolicy.getWidgetHints()['shaderSubTypes'] = shaderSubType
        
        mmList = []
        if locationType == "light":
            mmList = self.gafferEditor.getInternalControl().getAvailableMasterMaterials()
    
        self.__shaderFilterPopup = ShaderFilterPopup(valuePolicy, mmList, parent=None)

        self.__shaderFilterPopup.itemChosen.connect(self.__on_shaderFilterPopup_itemChosen)
        globalPos = self._treeWidget.mapToGlobal(QtCore.QPoint(option.rect.left() + 3, option.rect.bottom() + 3 + self._treeWidget.header().height()))
        self.__shaderFilterPopup.popup(globalPos)
        return
   

    def __on_shaderFilterPopup_itemChosen(self, itemText, itemMeta):
        """
        Slot function that reacts to the C{itemChosen} signal of the shader
        filter popup widget.

        Updates the parameter policy that corresponds to the model index for
        which the shader filter popup widget was shown with the selected shader
        whose name is given in the C{itemText} parameter.

        @type itemText: C{str}
        @type itemMeta: C{str}
        @param itemText: The text of the item that was chosen in the popup.
        @param itemMeta: Metadata stored for the item that was chosen.
        """
        materialValue = str(itemText)
        #print itemMeta
        locationType = self._getLocationType(self.__locationPath)
        location = self.gafferEditor.getRelativePathToRoot(self.__locationPath)
        if itemMeta == MasterMaterialKey:
            materialValue = os.path.relpath(materialValue, location)
            # set master material..
        #    self.gafferEditor.getInternalControl().setMasterMaterialForLocation(locationType, materialValue, location)
        #else:
        # potentially support multiple location edit via gafferEditor...
        self.gafferEditor.getInternalControl().setMaterialForLocation(locationType, materialValue, location)

RegisterDataType('ShaderDWA', ShaderItemDelegateDWA)