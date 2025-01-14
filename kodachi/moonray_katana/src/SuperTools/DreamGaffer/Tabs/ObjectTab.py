# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import QtWidgets, UI4, Utils
from BaseTab import *


class ObjectTab(BaseTab):
    def __init__(self, parent, node):
        BaseTab.__init__(self, parent, node)
        self.lgtXformWidget = None
        self.rigXformWidget = None
        self.proxyXformWidget = None


    def hideAllWidgets(self):
        if self.lgtXformWidget:
            self.lgtXformWidget.hide()
        if self.rigXformWidget:
            self.rigXformWidget.hide()
        if self.proxyXformWidget:
            self.proxyXformWidget.hide()

    def reloadTabForCreatedLocation(self, fullpath, location, locationType, index):
        node = self.getNodeOfLocationType(locationType)
        if not node:
            return
        node.loadXformDataFromIndex(index)
        widget = self.getWidgetOfLocationType(locationType)
        widget.show()

    
    def reloadTabForAdoptedLocation(self, fullpath, location, locationType):
        node = self.node.getProxyXformNode(forceCreate=True)
        node.getParameter("path").setValue(fullpath, 0)
        widget = self.getDownstreamXformWidget()
        widget.show()
        self.loadEditsForLocation(location, locationType)

    def resetProxyWidget(self):
        if not self.currentLocation:
            return

        proxyNode = self.node.getProxyXformNode()
        editNode = self.node.getDownstreamEditNode()
        edits = editNode.getParamEditsForLocation(self.currentLocation, isXformParam=True)
        if edits:
            for param, info in edits.iteritems():
                if param.startswith("attributeEditor"):
                    continue
                param = param.replace("xform.interactive", "args.xform", 1)
                proxyNode.getParameter(param).getChild("enable").setValue(0, 0)

        
        sparseEdits = editNode.getSparseEditsForLocation(self.currentLocation)
        if sparseEdits:
            for edit in sparseEdits.getChildren():
                paramName = edit.getName().replace("_", ".")
                if paramName.startswith("material") or paramName == "location.dwa":
                    continue
                paramName = paramName.replace("xform.interactive", "args.xform", 1)
                param = proxyNode.getParameter(paramName+".value")
                param.setExpressionFlag(False)
                param.makeConstant(0)
        

    def loadEditsForLocation(self, location, locationType):
        proxyNode = self.node.getProxyXformNode()
        editNode = self.node.getDownstreamEditNode()
        
        # reset first..
        self.resetProxyWidget()
        
        edits = editNode.getParamEditsForLocation(location, isXformParam=True)

        if edits:
            for param, info in edits.iteritems():
                if param.startswith("attributeEditor"):
                    continue
                param = param.replace("xform.interactive", "args.xform", 1)
                #print "param:", param
                for childParam, value in info.iteritems():
                    if childParam == "type":
                        continue
                    proxyNode.getParameter(param).getChild(childParam).setValue(value, 0)
        
        
        # load sparse edits.. 
        sparseEdits = editNode.getSparseEditsForLocation(location)
        if sparseEdits:
            for srcParam in sparseEdits.getChildren():
                paramName = srcParam.getName().replace("_", ".")
                if paramName.startswith("material") or paramName == "location.dwa":
                    continue
                paramName = paramName.replace("xform.interactive", "args.xform", 1)
                dstParam = proxyNode.getParameter(paramName+".value")
                copyParamToParam(srcParam, dstParam)
             
        self.currentLocation = location
        self.currentLocationType = locationType

    def getDownstreamXformWidget(self):
        if not self.proxyXformWidget:
            self.createProxyXformWidget()
        return self.proxyXformWidget

    def getWidgetOfLocationType(self, locationType):
        if locationType == "light":
            if not self.lgtXformWidget:
                self.createLgtXformWidget()
            return  self.lgtXformWidget
        if locationType == "rig":
            if not self.rigXformWidget:
                self.createRigXformWidget()
            return self.rigXformWidget
    
    def getNodeOfLocationType(self, locationType):
        if locationType == "light":
            return self.node.getLightCreateNode()
        elif locationType == "rig":
            return self.node.getRigCreateNode()
        
        '''
        # To-do...
        elif locationType == "light filter":
            return self.node.getLightFilterCreateNode()
        '''

    def createLgtXformWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        lgtNode = self.node.getLightCreateNode()
        param = lgtNode.getParameter("transform")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.lgtXformWidget = factory.buildWidget(self, policy)
        self.mainPanel.layout().addWidget(self.lgtXformWidget, 0, 0)


    def createRigXformWidget(self):
        factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
        rigNode = self.node.getRigCreateNode()
        param = rigNode.getParameter("transform")
        policy = UI4.FormMaster.CreateParameterPolicy(None, param)
        self.rigXformWidget = factory.buildWidget(self, policy)
        self.mainPanel.layout().addWidget(self.rigXformWidget, 1, 0)
    
    def createProxyXformWidget(self):
        '''
        policy = QT4FormWidgets.PythonGroupPolicy('transform')
        childPolicy = QT4FormWidgets.ValuePolicyProxy(UI4.FormMaster.CreateParameterPolicy(policy, param))
        policy.addChildPolicy(childPolicy)
        '''
        try:
            factory = UI4.FormMaster.KatanaFactory.ParameterWidgetFactory
            node = self.node.getProxyXformNode()
            param = node.getParameter("args")
            if not param:
                node.checkDynamicParameters()
            policy = UI4.FormMaster.CreateParameterPolicy(None, param)
            self.proxyXformWidget = factory.buildWidget(self, policy)
            self.mainPanel.layout().addWidget(self.proxyXformWidget, 2, 0)
        except:
            import traceback
            traceback.print_exc()
        
    
        