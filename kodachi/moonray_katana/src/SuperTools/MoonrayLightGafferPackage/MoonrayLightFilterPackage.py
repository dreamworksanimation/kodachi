# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Plugins
from PackageSuperToolAPI import Packages, NodeUtils as NU
import os
from GafferBase import GafferPackageBase

GafferThreeAPI = Plugins.GafferThreeAPI
LightFilterPackage = GafferThreeAPI.PackageClasses.LightFilterPackage
LightFilterEditPackage = GafferThreeAPI.PackageClasses.LightFilterEditPackage

_iconsDir = os.path.join(os.path.dirname(__file__), 'icons')

class MoonrayLightFilterPackage(GafferPackageBase, LightFilterPackage):
    #DISPLAY_ICON =  os.path.join(_iconsDir, 'light.svg')
    _shaderType = 'moonrayLightfilter'
    
    @classmethod
    def create(cls, enclosingNode, locationPath, shaderName):
        basePackage = cls.initPackage(LightFilterPackage, enclosingNode, locationPath, cls.__name__, shaderName)
        return cls(basePackage.getPackageNode())
    
class MoonrayLightFilterEditPackage(GafferPackageBase, LightFilterEditPackage):
    _shaderType = 'moonrayLightfilter'
    _isEditPackage = True
    
    @classmethod
    def create(cls, enclosingNode, locationPath):
        basePackage = cls.initPackage(LightFilterEditPackage, enclosingNode, locationPath, cls.__name__)
        return cls.createPackage(basePackage.getPackageNode())
    
    @classmethod
    def getAdoptableLocationTypes(cls):
        return set(('light filter',))
    
class MoonrayDecayLightFilterPackage(MoonrayLightFilterPackage):
    DISPLAY_NAME = "MoonrayDecayLightFilter"
    DEFAULT_NAME = "MoonrayDecayLightFilter"
    
    @classmethod
    def create(cls, enclosingNode, locationPath):
        result = super(MoonrayDecayLightFilterPackage, cls).create(enclosingNode, locationPath, 'DecayLightFilter')
        light = enclosingNode.getChild(enclosingNode.getParameterValue('node_create',0))
        centerOfInterest = light.getParameterValue('centerOfInterest', 20)
        material = result.getMaterialNode()
        material.setShader('moonrayLightfilter', 'DecayLightFilter')
        material.setShaderParam('moonrayLightfilter','falloff_near',True)
        material.setShaderParam('moonrayLightfilter','falloff_far',True)
        if centerOfInterest:
            material.setShaderParam('moonrayLightfilter','near_start', centerOfInterest * 0.4)
            material.setShaderParam('moonrayLightfilter','near_end', centerOfInterest * 0.5)
            material.setShaderParam('moonrayLightfilter','far_start', centerOfInterest * 2.0)
            material.setShaderParam('moonrayLightfilter','far_end', centerOfInterest * 2.2)
        return result

class MoonrayDecayLightFilterEditPackage(MoonrayLightFilterEditPackage):
    pass

class MoonrayBarnDoorsLightFilterPackage(MoonrayLightFilterPackage):
    DISPLAY_NAME = "MoonrayBarnDoorsLightFilter"
    DEFAULT_NAME = "MoonrayBarnDoorsLightFilter"
    
    @classmethod
    def create(cls, enclosingNode, locationPath):
        result = super(MoonrayBarnDoorsLightFilterPackage, cls).create(enclosingNode, locationPath, 'BarnDoorsLightFilter')
        light = enclosingNode.getChild(enclosingNode.getParameterValue('node_create',0))
        centerOfInterest = light.getParameterValue('centerOfInterest', 20)
        material = result.getMaterialNode()
        material.setShader('moonrayLightfilter', 'BarnDoorsLightFilter')
        if centerOfInterest:
            material.setShaderParam('moonrayLightfilter','distance_from_light',centerOfInterest/2)
        return result
    
class MoonrayBarnDoorsLightFilterEditPackage(MoonrayLightFilterEditPackage):
    pass

class MoonrayIntensityLightFilterPackage(MoonrayLightFilterPackage):
    DISPLAY_NAME = "MoonrayIntensityLightFilter"
    DEFAULT_NAME = "MoonrayIntensityLightFilter"
    
    @classmethod
    def create(cls, enclosingNode, locationPath):
        result = super(MoonrayIntensityLightFilterPackage, cls).create(enclosingNode, locationPath, 'IntensityLightFilter')
        return result

class MoonrayIntensityLightFilterEditPackage(MoonrayLightFilterEditPackage):
    pass
      
GafferThreeAPI.RegisterPackageClass(MoonrayDecayLightFilterPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayDecayLightFilterEditPackage)
MoonrayDecayLightFilterPackage.setEditPackageClass(MoonrayDecayLightFilterEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayBarnDoorsLightFilterPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayBarnDoorsLightFilterEditPackage)
MoonrayBarnDoorsLightFilterPackage.setEditPackageClass(MoonrayBarnDoorsLightFilterEditPackage)

GafferThreeAPI.RegisterPackageClass(MoonrayIntensityLightFilterPackage)
GafferThreeAPI.RegisterPackageClass(MoonrayIntensityLightFilterEditPackage)
MoonrayIntensityLightFilterPackage.setEditPackageClass(MoonrayIntensityLightFilterEditPackage)
