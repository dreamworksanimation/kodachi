# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import NodegraphAPI
import ScriptActions as SA
#from InternalControl import InternalControl
from InternalControl import InternalControl


SYNC_SELECTION_OFF = 0.0
SYNC_SELECTION_OUT = 1.0
SYNC_SELECTION_IN_OUT = 2.0

def GetEditor():
    from Editor import DreamGafferEditor
    return DreamGafferEditor

class DreamGafferNode(NodegraphAPI.SuperTool):
    def __init__(self):
        self.hideNodegraphGroupControls()
        self.addInputPort("in")
        self.addOutputPort("out")

        param = self.getParameters().createChildString('rootLocation', "/root/world/lgt")
        param.setHintString('{"widget": "scenegraphLocation"}')
        param = self.getParameters().createChildNumber('showIncomingScene', 0)
        param.setHintString('{"widget": "checkBox"}')
        
        self.getParameters().createChildNumber('syncSelection', SYNC_SELECTION_OFF)
        self.buildNodes()


    def buildInternalControl(self):
        if not hasattr(self, "internalControl"):
            self.internalControl = InternalControl(self)
        return self.internalControl

    def getInterface(self):
        if hasattr(self, "internalControl"):
            return self.internalControl

    def buildNodes(self):
        mergeNew = NodegraphAPI.CreateNode("Merge", self)
        mergeNew.setName("MergeNew")
        mergeNew.addInputPort("rig") 
        mergeNew.addInputPort("light")
        mergeNew.addInputPort("filter")
        mergeNew.addInputPort("material")
        SA.AddNodeReferenceParam(self, "mergeNew", mergeNew)

        mergeIncoming = NodegraphAPI.CreateNode("Merge", self)
        mergeIncoming.setName("MergeIncoming")
        mergeIncoming.addInputPort("i0")
        mergeIncoming.addInputPort("i1")
        SA.AddNodeReferenceParam(self, "mergeIncoming", mergeIncoming)

        isolate = NodegraphAPI.CreateNode("Isolate", self)
        isolate.enableSecondaryOutput(True)
        isolate.getParameter("isolateFrom").setValue("/root/world", 0)
        isolate.getParameter("isolateLocations").resizeArray(1)
        isolate.getParameter("isolateLocations.i0").setExpression("=^/rootLocation")


        mergeAll = NodegraphAPI.CreateNode("Merge", self)
        mergeAll.setName("MergeAll")
        mergeAll.addInputPort("i0")
        mergeAll.addInputPort("i1")
        SA.AddNodeReferenceParam(self, "mergeAll", mergeAll)

        isolate.getOutputPortByIndex(0).connect(mergeIncoming.getInputPortByIndex(0)) # the out port 
        
        viewNewCreated = NodegraphAPI.CreateNode("Dot", self)
        viewNewCreated.getOutputPortByIndex(0).connect(mergeIncoming.getInputPortByIndex(1))
        SA.AddNodeReferenceParam(self, "viewNew", viewNewCreated)

        mergeNew.getOutputPortByIndex(0).connect(viewNewCreated.getInputPortByIndex(0))


        isolate.getOutputPortByIndex(1).connect(mergeAll.getInputPortByIndex(1)) 

        viewIncoming = NodegraphAPI.CreateNode("Dot", self)
        viewIncoming.getInputPortByIndex(0).connect(mergeIncoming.getOutputPortByIndex(0))
        viewIncoming.getOutputPortByIndex(0).connect(mergeAll.getInputPortByIndex(0))
        SA.AddNodeReferenceParam(self,  "viewIncoming", viewIncoming)

        self.getSendPort("in").connect(isolate.getInputPortByIndex(0))
        self.getReturnPort("out").connect(mergeAll.getOutputPortByIndex(0))
        
        NodegraphAPI.SetNodePosition(isolate, (-100, 300))
        NodegraphAPI.SetNodePosition(mergeNew, (200, 300))
        NodegraphAPI.SetNodePosition(viewNewCreated, (200, 120))
        NodegraphAPI.SetNodePosition(mergeIncoming, (0, 100))
        NodegraphAPI.SetNodePosition(viewIncoming, (0, -100))
        NodegraphAPI.SetNodePosition(mergeAll, (0, -300))     

    def createProxyLightShaderNode(self):
        proxyMatNode = NodegraphAPI.CreateNode("Material", self)
        proxyMatNode.addShaderType("moonrayLight")
        proxyMatNode.checkDynamicParameters()
        proxyMatNode.getParameter("action").setValue("edit material", 0)
        SA.AddNodeReferenceParam(self, "proxyLightShader", proxyMatNode)
        #self.getMaterialEditNode().getOutputPortByIndex(0).connect(proxyMatNode.getInputPortByIndex(0))
        self.getMergeIncomingNode().getOutputPortByIndex(0).connect(proxyMatNode.getInputPortByIndex(0))

        NodegraphAPI.SetNodePosition(proxyMatNode, (400, 300))
        proxyMatNode.setName("MaterialEdit")
        return proxyMatNode
    
    def createProxyLightFilterNode(self):
        proxyFilterNode = NodegraphAPI.CreateNode("Material", self)
        proxyFilterNode.addShaderType("moonrayLightfilter")
        proxyFilterNode.checkDynamicParameters()
        proxyFilterNode.getParameter("action").setValue("edit material", 0)
        SA.AddNodeReferenceParam(self, "proxyLightFilter", proxyFilterNode)
        self.getMaterialEditNode().getOutputPortByIndex(0).connect(proxyFilterNode.getInputPortByIndex(0))
        #self.getMergeNewNode().getOutputPortByIndex(0).connect(proxyFilterNode.getInputPortByIndex(0))
        NodegraphAPI.SetNodePosition(proxyFilterNode, (400, 280))
        proxyFilterNode.setName("LightFilterEdit")

        return proxyFilterNode

    def createProxyXformNode(self):
        node = NodegraphAPI.CreateNode("TransformEdit", self)
        #node.getParameter("action").setValue("override interactive transform", 0)
        node.getParameter("action").setValue("replace global transform", 0)
        #node.setName("DownstreamXformEdit")
        SA.AddNodeReferenceParam(self, "proxyTransform", node)

        self.getMergeIncomingNode().getOutputPortByIndex(0).connect(node.getInputPortByIndex(0))
        NodegraphAPI.SetNodePosition(node, (300, 60))


    def createProxyMaterialEdit(self):
        node = NodegraphAPI.CreateNode("Material", self)
        node.getParameter("action").setValue("edit material", 0)
        SA.AddNodeReferenceParam(self, "proxyMaterialEdit", node)
        self.getMergeIncomingNode().getOutputPortByIndex(0).connect(node.getInputPortByIndex(0))
        NodegraphAPI.SetNodePosition(node, (300, 80))

    def createLightCreateNode(self):
        node = NodegraphAPI.CreateNode("MultiLightCreate", self)
        node.getParameter("topLocation").setExpression("=^/rootLocation")
        SA.AddNodeReferenceParam(self, "lightCreate", node)
        node.getOutputPortByIndex(0).connect(self.getMergeNewNode().getInputPortByIndex(1))
        NodegraphAPI.SetNodePosition(node, (300, 400))

        self.getMaterialEditNode(forceCreate=True)
        return node
    
    def createRigCreateNode(self):
        node = NodegraphAPI.CreateNode("MultiRigCreate", self)
        node.getParameter("topLocation").setExpression("=^/rootLocation")
        SA.AddNodeReferenceParam(self, "rigCreate", node)
        node.getOutputPortByIndex(0).connect(self.getMergeNewNode().getInputPortByIndex(0))
        NodegraphAPI.SetNodePosition(node, (0, 400))
        return node
    
    def createMaterialCreateNode(self):
        node = NodegraphAPI.CreateNode("MultiMaterialCreate", self)
        node.getParameter("topLocation").setExpression("=^/rootLocation")
        SA.AddNodeReferenceParam(self, "materialCreate", node)
        node.getOutputPortByIndex(0).connect(self.getMergeNewNode().getInputPortByIndex(2))
        NodegraphAPI.SetNodePosition(node, (400, 400))
        self.getMaterialEditNode(forceCreate=True)

        return node

    def createLgtFilterCreateNode(self):
        node = NodegraphAPI.CreateNode("MultiLightFilterCreate", self)
        node.getParameter("topLocation").setExpression("=^/rootLocation")
        SA.AddNodeReferenceParam(self, "lgtFilterCreate", node)
        node.getOutputPortByIndex(0).connect(self.getMergeNewNode().getInputPortByIndex(3))
        NodegraphAPI.SetNodePosition(node, (600, 400))
        self.getMaterialEditNode(forceCreate=True)
        self.getProxyFilterNode(forceCreate=True)
        return node

    def createMaterialEditNode(self):
        node = NodegraphAPI.CreateNode("MultiAttributesEdit", self)
        node.getParameter("topLocation").setExpression("=^/rootLocation")
        SA.AddNodeReferenceParam(self, "materialEdit", node)
        inputport = self.getViewNewNode().getInputPortByIndex(0).getConnectedPort(0)
        node.getOutputPortByIndex(0).connect(self.getViewNewNode().getInputPortByIndex(0))
        node.getInputPortByIndex(0).connect(inputport)

        NodegraphAPI.SetNodePosition(node, (200, 200))

        self.getProxyMaterialNode(forceCreate=True)
        return node
    
    def createDownstreamEditNode(self):
        node = NodegraphAPI.CreateNode("MultiAttributesEdit", self)
        node.setName("MultiLocationEdit")
        node.getParameter("topLocation").setExpression("=^/rootLocation")
        SA.AddNodeReferenceParam(self, "downstreamEdit", node)

        node.getInputPortByIndex(0).connect(self.getMergeIncomingNode().getOutputPortByIndex(0))
        node.getOutputPortByIndex(0).connect(self.getViewIncomingNode().getInputPortByIndex(0))

        NodegraphAPI.SetNodePosition(node, (0, 0))

        self.getProxyXformNode(forceCreate=True)
        self.getProxyDownstreamMaterialNode(forceCreate=True)
        
        return node

    def getCreateNodeForLocationType(self, locationType, forceCreate=False):
        if locationType == "light":
            return self.getLightCreateNode(forceCreate)
        elif locationType == "material":
            return self.getMaterialCreateNode(forceCreate)
        elif locationType == "rig":
            return self.getRigCreateNode(forceCreate)
        elif locationType == "light filter":
            return self.getLightFilterCreateNode(forceCreate)


    def getLightCreateNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "lightCreate")
        if not node and forceCreate:

            node = self.createLightCreateNode()
        return node
    
    def getRigCreateNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "rigCreate")
        if not node and forceCreate:
            node = self.createRigCreateNode()
        return node

    def getMaterialCreateNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "materialCreate")
        if not node and forceCreate:
            node = self.createMaterialCreateNode()
        return node

    def getLightFilterCreateNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "lgtFilterCreate")
        if not node and forceCreate:
            node = self.createLgtFilterCreateNode()
        return node
    
    def getMaterialEditNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "materialEdit")
        if not node and forceCreate:
            node = self.createMaterialEditNode()
        return node

    def getDownstreamEditNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "downstreamEdit")
        if not node and forceCreate:
            node = self.createDownstreamEditNode()
        return node

    def getProxyMaterialNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "proxyLightShader")
        if not node and forceCreate:
            node = self.createProxyLightShaderNode()
        return node 

    def getProxyFilterNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "proxyLightFilter")
        if not node and forceCreate:
            node = self.createProxyLightFilterNode()
        return node
    
    def getProxyXformNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "proxyTransform")
        if not node and forceCreate:
            node = self.createProxyXformNode()
        return node

    def getProxyDownstreamMaterialNode(self, forceCreate=False):
        node = SA.GetRefNode(self, "proxyMaterialEdit")
        if not node and forceCreate:
            node = self.createProxyMaterialEdit()
        return node

    def getMergeIncomingNode(self):
        return SA.GetRefNode(self, "mergeIncoming")
    
    def getViewIncomingNode(self):
        return SA.GetRefNode(self, "viewIncoming")

    def getMergeNewNode(self):
        return SA.GetRefNode(self, "mergeNew")
    
    def getViewNewNode(self):
        return SA.GetRefNode(self, "viewNew")

    def getRootLocation(self):
        return self.getParameter("rootLocation").getValue(0)
    
    def getShowIncomingScene(self):
        return self.getParameter("showIncomingScene").getValue(0)

    def getSyncSelection(self):
        return self.getParameter('syncSelection').getValue(0)

    def setSyncSelection(self, syncSelection):
        if syncSelection == self.getSyncSelection():
            return
        self.getParameter('syncSelection').setValue(syncSelection, 0)
    
    def editAttributes(self, editDict, isDownstreamEdit=False):
        if isDownstreamEdit:
            self.getDownstreamEditNode().editAttributes(editDict)
        else:
            self.getMaterialEditNode().editAttributes(editDict)
        