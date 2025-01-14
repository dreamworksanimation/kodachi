# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtWidgets, UI4
from BaseTab import *

class FilterTab(BaseTab):
    def __init__(self, parent, node):
        BaseTab.__init__(self, parent, node)

        self.lightFilterWidget = None

    def reloadTab(self, fullpath, location, locationType):
        if not self.dirty:
            return
        if locationType in ["light filter"]:
            index = self.editor.getInternalControl().getIndexForLocation(location, locationType)
            material = self.node.getCreateNodeForLocationType(locationType).getParameter("material.i"+str(index)).getValue(0)
            self.node.getProxyFilterNode().getParameter('edit.location').setValue(fullpath, 0)
            if material == "":
                # show parent's light material
                self.node.getProxyFilterNode().getParameter("shaders.moonrayLightFilterShader.enable").setValue(0, 0)
            else:
                self.node.getProxyFilterNode().getParameter("shaders.moonrayLightFilterShader.enable").setValue(1, 0)
                self.node.getProxyFilterNode().getParameter("shaders.moonrayLightFilterShader.value").setValue(material, 0)
            self.loadLightFilterWidget()
        elif locationType == "light":
            ""
            # show assigned filters..
            # filter to edit? 
        elif locationType == "rig":
            self.hideAllWidgets()

    def hideAllWidgets(self):
        if self.lightFilterWidget:
            self.lightFilterWidget.hide()


    def loadLightFilterWidget(self):
        if not self.lightFilterWidget:
            self.createLightFilterWidget()
        self.lightFilterWidget.show()


    def createLightFilterWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        proxyFilter = self.node.getProxyFilterNode()
        param = proxyFilter.getParameter("shaders")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.lightFilterWidget = factory.buildWidget(self, policy)
        self.mainPanel.layout().addWidget(self.lightFilterWidget, 0, 0) 