# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import NodegraphAPI, Plugins, Nodes3DAPI
from PackageSuperToolAPI import Packages, NodeUtils as NU
import os
from GafferBase import GafferPackageBase

GafferThreeAPI = Plugins.GafferThreeAPI
LightPackage = GafferThreeAPI.PackageClasses.LightPackage
LightEditPackage = GafferThreeAPI.PackageClasses.LightEditPackage

_iconsDir = os.path.join(os.path.dirname(__file__), 'icons')

class MoonrayLightPackage(GafferPackageBase, LightPackage):
    DISPLAY_ICON =  os.path.join(_iconsDir, 'light.svg')
    _shaderName = ''
    
    @classmethod
    def create(cls, enclosingNode, locationPath):
        basePackage = cls.initPackage(LightPackage, enclosingNode, locationPath, cls.__name__, cls._shaderName)
        cls.addInheritanceNode(basePackage)
        cls.addInheritanceOpScript(basePackage)
        
        result = cls(basePackage.getPackageNode())
        
        # Create a PostMergeStackNode to get the gaffer to actually show
        # inherited parameters
        postMergeNode = result.createPostMergeStackNode()
        Nodes3DAPI.SetExtraParameterValueSourceNodes(basePackage.getMaterialNode(), [postMergeNode])
        
        return result
    
class MoonrayLightEditPackage(GafferPackageBase, LightEditPackage):
    DISPLAY_ICON =  os.path.join(_iconsDir, 'light.svg')
    
    _isEditPackage = True

    @classmethod
    def create(cls, enclosingNode, locationPath):
        basePackage = cls.initPackage(LightEditPackage, enclosingNode, locationPath, cls.__name__)
        cls.addInheritanceNode(basePackage)
        return cls.createPackage(basePackage.getPackageNode())
    
    @classmethod
    def getAdoptableLocationTypes(cls):
        return set(('light',))
    
class MoonrayCylinderLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonrayCylinderLight"
    DEFAULT_NAME = "MoonrayCylinderLight"
    _shaderName = 'CylinderLight'
    
class MoonrayCylinderLightEditPackage(MoonrayLightEditPackage):
    pass

class MoonrayDiskLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonrayDiskLight"
    DEFAULT_NAME = "MoonrayDiskLight"
    _shaderName = 'DiskLight'
    
class MoonrayDiskLightEditPackage(MoonrayLightEditPackage):
    pass

class MoonrayDistantLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonrayDistantLight"
    DEFAULT_NAME = "MoonrayDistantLight"
    DISPLAY_ICON = os.path.join(_iconsDir, 'distant_light.svg')
    _shaderName = 'DistantLight'
    
class MoonrayDistantLightEditPackage(MoonrayLightEditPackage):
    DISPLAY_ICON = os.path.join(_iconsDir, 'distant_light.svg')
    pass

class MoonrayEnvLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonrayEnvLight"
    DEFAULT_NAME = "MoonrayEnvLight"
    DISPLAY_ICON = os.path.join(_iconsDir, 'env_light.svg')
    _shaderName = 'EnvLight'
    
class MoonrayEnvLightEditPackage(MoonrayLightEditPackage):
    DISPLAY_ICON = os.path.join(_iconsDir, 'env_light.svg')
    pass

class MoonrayRectLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonrayRectLight"
    DEFAULT_NAME = "MoonrayRectLight"
    DISPLAY_ICON = os.path.join(_iconsDir, 'rect_light.svg')
    _shaderName = 'RectLight'
    
class MoonrayRectLightEditPackage(MoonrayLightEditPackage):
    DISPLAY_ICON = os.path.join(_iconsDir, 'rect_light.svg')
    pass

class MoonraySphereLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonraySphereLight"
    DEFAULT_NAME = "MoonraySphereLight"
    DISPLAY_ICON = os.path.join(_iconsDir, 'sphere_light.svg')
    _shaderName = 'SphereLight'
    
class MoonraySphereLightEditPackage(MoonrayLightEditPackage):
    DISPLAY_ICON = os.path.join(_iconsDir, 'sphere_light.svg')
    pass

class MoonraySpotLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = "MoonraySpotLight"
    DEFAULT_NAME = "MoonraySpotLight"
    DISPLAY_ICON = os.path.join(_iconsDir, 'spot_light.svg')
    _shaderName = 'SpotLight'
    
class MoonraySpotLightEditPackage(MoonrayLightEditPackage):
    DISPLAY_ICON = os.path.join(_iconsDir, 'spot_light.svg')
    pass

def addMeshLightPostMerge(package):
    # Add node that modifies shadow linking and visibility of source mesh
    postMergeNode = package.createPostMergeStackNode()
    node = NodegraphAPI.CreateNode('MoonrayMeshLightPostMerge', postMergeNode)
    NU.WireInlineNodes(postMergeNode, (node,))
    node.getParameter('path').setExpression(
        "getParam(getParent().node_package + '.__gaffer.location')");
    node.getParameter('geometry').setExpression(
        "getParam(getParam(getParent().node_package + '.node_material') + "
        "'.shaders.moonrayLightParams.geometry.value')");

class MoonrayMeshLightPackage(MoonrayLightPackage):
    DISPLAY_NAME = DEFAULT_NAME = "MoonrayMeshLight"
    _shaderName = 'MeshLight'

    @classmethod
    def create(cls, enclosingNode, locationPath):
        package = super(MoonrayMeshLightPackage, cls).create(enclosingNode, locationPath)
        addMeshLightPostMerge(package)
        return package

class MoonrayMeshLightEditPackage(MoonrayLightEditPackage):
    @classmethod
    def create(cls, enclosingNode, locationPath):
        package = super(MoonrayMeshLightEditPackage, cls).create(enclosingNode, locationPath)
        return package

GafferThreeAPI.RegisterPackageClass(MoonrayCylinderLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayCylinderLightEditPackage)
MoonrayCylinderLightPackage.setEditPackageClass(MoonrayCylinderLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayDiskLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayDiskLightEditPackage)
MoonrayDiskLightPackage.setEditPackageClass(MoonrayDiskLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayDistantLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayDistantLightEditPackage)
MoonrayDistantLightPackage.setEditPackageClass(MoonrayDistantLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayEnvLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayEnvLightEditPackage)
MoonrayEnvLightPackage.setEditPackageClass(MoonrayEnvLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayRectLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayRectLightEditPackage)
MoonrayRectLightPackage.setEditPackageClass(MoonrayRectLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonraySphereLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonraySphereLightEditPackage)
MoonraySphereLightPackage.setEditPackageClass(MoonraySphereLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonraySpotLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonraySpotLightEditPackage)
MoonraySpotLightPackage.setEditPackageClass(MoonraySpotLightEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayMeshLightPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayMeshLightEditPackage)
MoonrayMeshLightPackage.setEditPackageClass(MoonrayMeshLightEditPackage)
