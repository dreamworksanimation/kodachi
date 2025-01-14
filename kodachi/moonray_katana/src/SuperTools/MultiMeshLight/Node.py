# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import NodegraphAPI, Utils
import os
import ScriptActions as SA

def GetEditor():
    from Editor import MultiMeshLightEditor
    return MultiMeshLightEditor


class MultiMeshLightNode(NodegraphAPI.SuperTool):
    def __init__(self):
        self.hideNodegraphGroupControls()
        self.addInputPort("in")
        self.addOutputPort("out")
        param = self.getParameters().createChildString('meshLocations', "")
        param.setHintString('{"widget": "cel", "help": "Geometries for the meshlight."}')
        param = self.getParameters().createChildString("lightName", "/root/world/lgt/multiMeshLight")
        param.setHintString('{"widget": "scenegraphLocation",  "help": "Please give the full path for the meshlight that you would like to create."}')
        self.buildNodes()
        

    def buildNodes(self):
        meshCombine = NodegraphAPI.CreateNode("GenericOp", self)
        meshCombine.setName("MeshCombine")
        meshCombine.addInputPort("i0")
        meshCombine.getParameter("opType").setValue("MeshCombineOp", 0)
        cel = meshCombine.getParameter("opArgs").createChildString("CEL", "")
        cel.setExpression("=^/meshLocations")
        
        param = meshCombine.getParameter("opArgs").createChildGroup("arbitraryAttributes")
        param.setHintString('{"help": "The arbitrary attributes specified here will be added to the combined mesh. Please feel free to use the Drop Attributes Here to add new arbitrary attribute."}')
        name = meshCombine.getParameter("opArgs").createChildString("name", "multiMeshLight_combinedMesh")

        meshCombine.getParameter("opArgs").createChildNumber("visibility", 0)
        meshCombine.getParameter("applyWhere").setValue("at specific location", 0)
        meshCombine.getParameter("location").setValue("/root/world/lgt/", 0)

        
        SA.AddNodeReferenceParam(self, "meshCombine", meshCombine)

        lightCreate = NodegraphAPI.CreateNode("LightCreate", self)
        lightCreate.setName("MultiMeshLightCreate")
        lightCreate.getParameter("name").setExpression("=^/lightName")
        SA.AddNodeReferenceParam(self, "lightCreate", lightCreate)

        material = NodegraphAPI.CreateNode("Material", self)
        material.setName("MultiMeshLightMaterial")
        material.getParameter("action").setValue("edit material", 0)
        material.checkDynamicParameters()

        material.getParameter("edit.location").setExpression("=^/lightName")
        material.addShaderType("moonrayLight")
        material.checkDynamicParameters()
        material.getParameter("shaders.moonrayLightShader.value").setValue("MeshLight", 0)
        material.getParameter("shaders.moonrayLightShader.enable").setValue(1, 0)
        material.checkDynamicParameters()
        #material.checkDynamicParameters()
        #Utils.EventModule.ProcessAllEvents() # still couldnt find the parameter "shaders.moonrayLightParams.geometry"

        SA.AddNodeReferenceParam(self, "lightMaterial", material)
        
        lightCreate.getOutputPortByIndex(0).connect(material.getInputPortByIndex(0))

        mergeNode = NodegraphAPI.CreateNode("Merge", self)
        mergeNode.addInputPort("i0")
        mergeNode.addInputPort("i1")
        mergeNode.getInputPortByIndex(0).connect(meshCombine.getOutputPortByIndex(0))
        mergeNode.getInputPortByIndex(1).connect(material.getOutputPortByIndex(0))

        self.getSendPort("in").connect(meshCombine.getInputPortByIndex(0))
        self.getReturnPort("out").connect(mergeNode.getOutputPortByIndex(0))

        NodegraphAPI.SetNodePosition(meshCombine, (-100, 100))
        NodegraphAPI.SetNodePosition(lightCreate, (100, 100))
        NodegraphAPI.SetNodePosition(material, (100, 0))
        NodegraphAPI.SetNodePosition(mergeNode, (0, -100))

    def getMeshCombineNode(self):
        return SA.GetRefNode(self, "meshCombine")
    
    def getMaterialNode(self):
        return SA.GetRefNode(self, "lightMaterial")
    
    def getLightCreateNode(self):
        return SA.GetRefNode(self,  "lightCreate")
    
    def updateMeshLocation(self):
        lightName = self.getParameter("lightName").getValue(0)
        parent = os.path.dirname(lightName)
        meshName = os.path.basename(lightName) + "_combinedMesh"
        self.getMeshCombineNode().getParameter("location").setValue(parent, 0)
        self.getMeshCombineNode().getParameter("opArgs.name").setValue(meshName, 0)
        self.getMaterialNode().getParameter("shaders.moonrayLightParams.geometry.value").setValue(parent+"/"+meshName, 0)
        self.getMaterialNode().getParameter("shaders.moonrayLightParams.geometry.enable").setValue(1, 0)

 
        
