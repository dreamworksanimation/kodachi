# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtWidgets, UI4, Utils
from BaseTab import *

class MaterialTab(BaseTab):
    def __init__(self, parent, node):
        BaseTab.__init__(self, parent, node)
        
        self.lightShaderWidget = None
        self.lightFilterWidget = None
        self.materialEditWidget = None

    def hideAllWidgets(self):
        if self.lightFilterWidget:
            self.lightFilterWidget.hide()
        if self.lightShaderWidget:
            self.lightShaderWidget.hide()
        if self.materialEditWidget:
            self.materialEditWidget.hide()



    def reloadTabForCreatedLocation(self, fullpath, location, locationType, index):
        shaderType = self.getShaderTypeOfLocationType(locationType)
        if not shaderType:
            return
    
        materialName = self.node.getCreateNodeForLocationType(locationType).getParameter("material.i"+str(index)).getValue(0)
        proxyNode = self.getProxyNodeOfLocationType(locationType)
        proxyNode.getParameter('edit.location').setValue(fullpath, 0)
        if materialName == "" or materialName.startswith("../"):
            # show parent's light material
            proxyNode.getParameter("shaders.%s.enable"%shaderType).setValue(0, 0)
        else:
            proxyNode.getParameter("shaders.%s.enable"%shaderType).setValue(1, 0)
            proxyNode.getParameter("shaders.%s.value"%shaderType).setValue(materialName, 0)
        
        widget = self.getWidgetOfLocationType(locationType)
        self.loadEditsForLocation(location, locationType)
        widget.show()
        #Utils.EventModule.ProcessAllEvents()

    def reloadTabForAdoptedLocation(self, fullpath, location, locationType):
        #print "reloadTabForAdoptedLocation"
        node = self.node.getProxyDownstreamMaterialNode()
        node.getParameter('edit.location').setValue(fullpath, 0)
        node.checkDynamicParameters()
        widget = self.getMaterialEditWidget()
        self.loadEditsForLocation(location, locationType, isAdopted=True)
        widget.show()
        #Utils.EventModule.ProcessAllEvents()
        #Utils.EventModule.ProcessAllEvents()



    def getWidgetOfLocationType(self, locationType):
        if locationType in ["light", "material"]:
            if not self.lightShaderWidget:
                self.createLightShaderWidget()
            return  self.lightShaderWidget
        if locationType == "light filter":
            if not self.lightFilterWidget:
                self.createLightFilterWidget()
            return self.lightFilterWidget
    
    def getMaterialEditWidget(self):
        if not self.materialEditWidget:
            self.createMaterialEditWidget()
        return self.materialEditWidget
    
    def getShaderTypeOfLocationType(self, locationType):
        if locationType in ["light", "material"]:
            return  "moonrayLightShader"
        if locationType == "light filter":
            return "moonrayLightfilterShader"
        
    def getProxyNodeOfLocationType(self, locationType):
        if locationType in ["light", "material"]:
            return  self.node.getProxyMaterialNode()
        if locationType == "light filter":
            return self.node.getProxyFilterNode()
        

    
    def resetProxyWidget(self, isAdopted):
        if not self.currentLocation:
            return
        proxyMaterialNode = self.getProxyNodeOfLocationType(self.currentLocationType)
        materialEditNode = self.node.getMaterialEditNode()
        if isAdopted:
            proxyMaterialNode = self.node.getProxyDownstreamMaterialNode()
            materialEditNode = self.node.getDownstreamEditNode()

        edits = materialEditNode.getParamEditsForLocation(self.currentLocation)
        if edits:
            for param, info in edits.iteritems():
                if param.startswith("attributeEditor"):
                    continue
                param = param.replace("material", "shaders", 1)
                enableParam = param + ".enable"
                if proxyMaterialNode.getParameter(enableParam):
                    proxyMaterialNode.getParameter(enableParam).setValue(0, 0)
               

        
        sparseEdits = materialEditNode.getSparseEditsForLocation(self.currentLocation)
        if sparseEdits:
            for edit in sparseEdits.getChildren():
                try:
                    paramName = edit.getName().replace("_", ".")
                    if paramName.startswith("xform") or paramName == "location.dwa":
                        continue
                    paramName = paramName.replace("material", "shaders", 1)
                    param = proxyMaterialNode.getParameter(paramName+".value")
                    param.setExpressionFlag(False)
                    param.makeConstant(0)
                except:
                    import traceback
                    traceback.print_exc()



    def loadEditsForLocation(self, location, locationType, isAdopted=False):
        proxyMaterialNode = self.getProxyNodeOfLocationType(locationType)
        materialEditNode = self.node.getMaterialEditNode()
        if isAdopted:
            proxyMaterialNode = self.node.getProxyDownstreamMaterialNode()
            materialEditNode = self.node.getDownstreamEditNode()
        
        # reset first..
        self.resetProxyWidget(isAdopted)
        edits = materialEditNode.getParamEditsForLocation(location)

        if edits:
            for param, info in edits.iteritems():
                if param.startswith("attributeEditor"):
                    continue
                param = param.replace("material", "shaders", 1)
                for childParam, value in info.iteritems():
                    try:
                        if childParam == "type":
                            continue
                        if childParam == "value":
                            #print childParam, value
                            if isinstance(value, list):
                                for i, v in enumerate(value):
                                    paramName = param + "." + childParam + ".i"+str(i)
                                    if proxyMaterialNode.getParameter(paramName):
                                        proxyMaterialNode.getParameter(paramName).setValue(v, 0)
                                continue
                        if not proxyMaterialNode.getParameter(param):
                            proxyMaterialNode.checkDynamicParameters()
                        if proxyMaterialNode.getParameter(param):
                            if proxyMaterialNode.getParameter(param).getChild(childParam):
                                proxyMaterialNode.getParameter(param).getChild(childParam).setValue(value, 0)
                    except:
                        import traceback
                        traceback.print_exc()
                        
        
        # load sparse edits.. 
        sparseEdits = materialEditNode.getSparseEditsForLocation(location)
        if sparseEdits:
            for srcParam in sparseEdits.getChildren():
                paramName = srcParam.getName().replace("_", ".")
                if paramName.startswith("xform") or paramName == "location.dwa":
                    continue
                paramName = paramName.replace("material", "shaders", 1)
                dstParam = proxyMaterialNode.getParameter(paramName+".value")
                copyParamToParam(srcParam, dstParam)
                
        self.currentLocation = location
        self.currentLocationType = locationType

    def createLightShaderWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        proxyMat = self.node.getProxyMaterialNode(forceCreate=True)
        param = proxyMat.getParameter("shaders")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.lightShaderWidget = factory.buildWidget(self, policy)
        self.mainPanel.layout().addWidget(self.lightShaderWidget, 0,0)


    def createLightFilterWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        proxyFilter = self.node.getProxyFilterNode(forceCreate=True)
        param = proxyFilter.getParameter("shaders")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.lightFilterWidget = factory.buildWidget(self, policy)
        self.mainPanel.layout().addWidget(self.lightFilterWidget, 1, 0) 
    
    def createMaterialEditWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        proxyNode = self.node.getProxyDownstreamMaterialNode()
        param = proxyNode.getParameter("shaders")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.materialEditWidget = factory.buildWidget(self, policy)
        self.mainPanel.layout().addWidget(self.materialEditWidget, 2, 0) 
