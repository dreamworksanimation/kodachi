# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import PackageSuperToolAPI
from PackageSuperToolAPI import Packages, NodeUtils as NU
from Katana import NodegraphAPI
from types import MethodType

def falseFunc(self):
    return False

# Base package class for lights, light filters, and their edit classes
# Subclasses should set _shaderType and _isEditPackage when appropriate
class GafferPackageBase:
    _shaderType = 'moonrayLight'
    _isEditPackage = False
    
    @classmethod
    def initPackage(cls, gaffercls, enclosingNode, locationPath, nodeName, shaderName = None):
        basePackage = gaffercls.create(enclosingNode, locationPath)
        NU.AddPackageTypeAndPath(basePackage.getPackageNode(), nodeName, locationPath)
        
        if not cls._isEditPackage:
            # Edit package subclasses will not be used if this is not set
            basePackage.getCreateNode().getParameter(
                    'extraAttrs.info.gaffer.packageClass').setExpression(
                            '=^/__gaffer.packageType')
                    
        cls.initMaterialNode(basePackage, shaderName)
        
        return basePackage
    
    @classmethod
    def getLocationExpression(cls):
        return '=^/%s' % NU.GetPackageLocationParameterPath()
    
    @classmethod
    def initMaterialNode(cls, basePackage, shaderName = None):
        materialNode = basePackage.getMaterialNode()
        
        if not cls._isEditPackage:
            materialNode.addShaderType(cls._shaderType)
            materialNode.getParameter('shaders.{0}Shader.enable'.format(cls._shaderType)).setValue(1, 0)
            materialNode.getParameter('shaders.{0}Shader.value'.format(cls._shaderType)).setValue(shaderName, 0)
            
        NU.AddNodeRef(basePackage.getPackageNode(), 'material', materialNode)

    @classmethod
    def addInheritanceOpScript(cls, basePackage):
        inheritanceOpScript = NodegraphAPI.CreateNode('OpScript', basePackage.getPackageNode())
        inheritanceOpScript.getParameter('CEL').setExpression(cls.getLocationExpression())
        lua = ("local inheritsAttribute = Interface.GetAttr('inherits')\n"
               "local lightCreateAttribute = Interface.GetAttr('attributeEditor.exclusiveTo')\n"
               "if inheritsAttribute and lightCreateAttribute then\n"
               "\tInterface.SetAttr('attributeEditor.xform.exclusiveTo', lightCreateAttribute)\n"
               "end")
        inheritanceOpScript.getParameter('script.lua').setValue(lua, 0)
        NU.AddNodeRef(basePackage.getPackageNode(), 'inheritanceOpScript',
                      inheritanceOpScript)
        NU.PrependNode(basePackage.getMaterialNode(), inheritanceOpScript)

    @classmethod
    def addInheritanceNode(cls, basePackage):
        inheritsSetNode = NodegraphAPI.CreateNode('_InheritsAttributeSet', basePackage.getPackageNode())
        inheritsSetNode.getParameter('location').setExpression(cls.getLocationExpression())
        NU.AddNodeRef(basePackage.getPackageNode(), 'inherits', inheritsSetNode)
        NU.PrependNode(basePackage.getMaterialNode(), inheritsSetNode)
        
if PackageSuperToolAPI.IsUIMode():
    from Katana import QT4FormWidgets, FormMaster
    # Base UI class of lights, light filters, and their edit classes
    # Subclasses should set _shaderType and _isEditPackage when appropriate
    class GafferUIDelegateBase:
        _shaderType = 'moonrayLight'
        _isEditPackage = False
        
        @classmethod
        def initTabPolicy(cls, package, delegateClass, tabName):
            if tabName == "Object":
                policy = cls.getObjectPolicy()
            elif tabName == "Material":
                policy = cls.getMaterialPolicy(package)
            else:
                policy = delegateClass.getTabPolicy(package, tabName)
                
            return policy
        
        @classmethod
        def getObjectPolicy(cls):
            objectPolicy = QT4FormWidgets.PythonGroupPolicy('object')
            objectPolicy.getWidgetHints()['open'] = True
            objectPolicy.getWidgetHints()['hideTitle'] = True
            return objectPolicy
        
        @classmethod
        def getMaterialPolicy(cls, package):
            packageNode = package.getPackageNode()
            materialNode = NU.GetRefNode(packageNode, "material")
            if materialNode is not None:
                # Light filters don't populate themselves automatically
                # like lights do, for some reason.
                materialNode.checkDynamicParameters()
                
                materialPolicy = QT4FormWidgets.PythonGroupPolicy('material')
                materialPolicy.getWidgetHints()['hideTitle'] = True
                
                # Add the field to allow material inheritance
                attrSetNode = NU.GetRefNode(packageNode, "inherits")
                if attrSetNode is not None:
                    # For ui aesthetics, put the widget in a hidden group.
                    # This makes the alignment pretty
                    materialInheritanceGroup = QT4FormWidgets.PythonGroupPolicy('material inheritance')
                    materialInheritanceGroup.getWidgetHints()['hideTitle'] = True
    
                    inheritsPolicy = FormMaster.CreateParameterPolicy(materialPolicy,
                        attrSetNode.getParameter('args.inherits'))
                    inheritsPolicy = QT4FormWidgets.ValuePolicyProxy(inheritsPolicy)
    
                    materialInheritanceGroup.addChildPolicy(inheritsPolicy)
                    materialInheritanceGroup.getWidgetHints()['open'] = True
                    materialPolicy.addChildPolicy(materialInheritanceGroup)
                    
                shaderPolicy = FormMaster.CreateParameterPolicy(materialPolicy,
                    materialNode.getParameter('shaders.{0}Shader'.format(cls._shaderType)))
                shaderPolicy = QT4FormWidgets.ValuePolicyProxy(shaderPolicy)
                materialPolicy.addChildPolicy(shaderPolicy)
                
                paramsPolicy = FormMaster.CreateParameterPolicy(materialPolicy,
                    materialNode.getParameter('shaders.{0}Params'.format(cls._shaderType)))
                # Prevent the user from deleting a light's material simply by
                # hiding the "X" close button widget.  Set this on the parameters,
                # and not the shader itself
                paramsPolicy.shouldDisplayClose = MethodType(falseFunc, paramsPolicy, paramsPolicy.__class__)
                paramsPolicy.getWidgetHints()['open'] = True
                materialPolicy.addChildPolicy(paramsPolicy)
                
            return materialPolicy
