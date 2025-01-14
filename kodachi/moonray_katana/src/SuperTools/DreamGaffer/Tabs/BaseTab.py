# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtWidgets, UI4, Utils

class BaseTab(QtWidgets.QWidget):
    def __init__(self, parent, node):
        QtWidgets.QWidget.__init__(self, parent)
        mainLayout = QtWidgets.QVBoxLayout()
        self.setLayout(mainLayout)
        self.node = node
        self.editor = parent
        self.dirty = False
        self.currentLocation = None
        self.currentLocationType = None
        sizePolicy = QtWidgets.QSizePolicy(QtWidgets.QSizePolicy.Expanding, QtWidgets.QSizePolicy.Expanding)

        self.mainPanel = QtWidgets.QWidget(self)
        self.mainPanel.setSizePolicy(sizePolicy)
        QtWidgets.QGridLayout(self.mainPanel)

        self.__panelScrollArea = UI4.Widgets.PanelScrollArea(self)
        self.__panelScrollArea.setWidget(self.mainPanel)
        
        self.__panelScrollArea.setMinimumHeight(600)
        self.__panelScrollArea.setSizePolicy(sizePolicy)
        self.layout().addWidget(self.__panelScrollArea)

        self.verticalSpacer = QtWidgets.QSpacerItem(20, 40, QtWidgets.QSizePolicy.Minimum, QtWidgets.QSizePolicy.Expanding)
        self.mainPanel.layout().addItem(self.verticalSpacer, 3, 0)

    def setDirty(self, state):
        self.dirty = state

    def reloadTab(self, fullpath, location, locationType):
        self.editor.systemSetValue(True)
        if self.dirty == True:
            self.hideAllWidgets()
            index = self.editor.getInternalControl().getIndexForLocation(location, locationType)
            if index != None:
                #print "reloadTabForCreatedLocation"
                self.reloadTabForCreatedLocation(fullpath, location, locationType, index)
            else:
                if self.editor.getInternalControl().locationAdopted(location):
                    self.reloadTabForAdoptedLocation(fullpath, location, locationType)
            self.dirty = False
            #Utils.EventModule.ProcessAllEvents()
        self.editor.systemSetValue(False)


    def reloadTabForCreatedLocation(self, fullpath, location, locationType, index):
        pass
    
    def reloadTabForAdoptedLocation(self, fullpath, location, locationType):
        pass
    
    
def copyParamToParam(srcParam, dstParam):
    if srcParam.isAnimated():
        dstParam.setCurve(srcParam.getCurve())
        dstParam.setAutoKey(srcParam.getAutoKey())
        
    elif srcParam.isExpression():
        expr = srcParam.getExpression()
        dstParam.setExpression(expr)