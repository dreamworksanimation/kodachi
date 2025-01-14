# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtWidgets, UI4, QT4FormWidgets, Widgets, QtCore, NodegraphAPI, Utils

class MultiMeshLightEditor(QtWidgets.QWidget):
    def __init__(self, parent, node):
        try:
            super(MultiMeshLightEditor, self).__init__(parent)
            self.__node = node
            QtWidgets.QVBoxLayout(self)
                
            factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
            for paramName  in ["meshLocations", "lightName"]:
                param = self.__node.getParameter(paramName)
                policy = UI4.FormMaster.CreateParameterPolicy(None, param)
                if paramName == "lightName":
                    policy.addCallback(self.__lightNameChanged)
                widget = factory.buildWidget(self, policy)
                self.layout().addWidget(widget)

            self.createLightShaderWidget()

            param = self.__node.getMeshCombineNode().getParameter("opArgs.arbitraryAttributes")
            widget = factory.buildWidget(self, UI4.FormMaster.CreateParameterPolicy(None, param))
            self.layout().addWidget(widget)

            self.__dropPanel = UI4.Widgets.AttributeDropLabel(self)
            self.__dropPanel.attrDropped.connect(self.__attrDropped)

            self.layout().addWidget(self.__dropPanel)
        except:
            import traceback
            traceback.print_exc()


    def __lightNameChanged(self, *args, **kwds):
        self.__node.updateMeshLocation()

    def __attrDropped(self, attr, name):
        param = self.__node.getMeshCombineNode().getParameter("opArgs.arbitraryAttributes")
        values = [c.getValue(0) for c in param.getChildren()]
        if name not in values:
            param.createChildString("attrName", name)

    def createLightShaderWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        mat = self.__node.getMaterialNode()
        param = mat.getParameter("shaders")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.lightShaderWidget = factory.buildWidget(self, policy)
        self.layout().addWidget(self.lightShaderWidget)



