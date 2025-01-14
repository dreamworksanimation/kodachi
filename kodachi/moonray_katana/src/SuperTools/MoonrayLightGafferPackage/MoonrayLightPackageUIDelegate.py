# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from PackageSuperToolAPI import UIDelegate, NodeUtils as NU, Packages
from Katana import QT4FormWidgets, FormMaster, Plugins
from MoonrayLightPackage import *
from GafferBase import GafferUIDelegateBase

GafferThreeAPI = Plugins.GafferThreeAPI
LightUIDelegate = UIDelegate.GetUIDelegateClassForPackageClass(
    GafferThreeAPI.PackageClasses.LightPackage)
LightEditUIDelegate = UIDelegate.GetUIDelegateClassForPackageClass(
    GafferThreeAPI.PackageClasses.LightEditPackage)

class MoonrayLightPackageUIDelegate(GafferUIDelegateBase, LightUIDelegate):   
    def getTabPolicy(self, tabName):
        policy = self.initTabPolicy(self, LightUIDelegate, tabName)
        
        if tabName == "Object":
            transformPolicy = LightUIDelegate.getTabPolicy(self, tabName).getChildByName('transform')
            policy.addChildPolicy(transformPolicy)
                
        return policy
    
class MoonrayLightEditPackageUIDelegate(GafferUIDelegateBase, LightEditUIDelegate):
    def getTabPolicy(self, tabName):
        policy = self.initTabPolicy(self, LightEditUIDelegate, tabName)
        if tabName == "Object":
            transformPolicy = LightEditUIDelegate.getTabPolicy(self, tabName).getChildByName('transform')
            policy.addChildPolicy(transformPolicy)
        return policy
    
class MoonrayCylinderLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayCylinderLight'
    DefaultShortcut = 'Alt+1'
    
class MoonrayDiskLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayDiskLight'
    DefaultShortcut = 'Alt+2'
    
class MoonrayDistantLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayDistantLight'
    DefaultShortcut = 'Alt+3'
    
class MoonrayEnvLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayEnvLight'
    DefaultShortcut = 'Alt+4'
    
class MoonrayRectLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayRectLight'
    DefaultShortcut = 'Alt+5'
    
class MoonraySphereLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonraySphereLight'
    DefaultShortcut = 'Alt+6'
    
class MoonraySpotLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonraySpotLight'
    DefaultShortcut = 'Alt+7'

class MoonrayMeshLightUIDelegate(MoonrayLightPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayMeshLight'
    DefaultShortcut = 'Alt+8'

UIDelegate.RegisterUIDelegateClass(MoonrayCylinderLightPackage, MoonrayCylinderLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayCylinderLightEditPackage, MoonrayLightEditPackageUIDelegate)
 
UIDelegate.RegisterUIDelegateClass(MoonrayDiskLightPackage, MoonrayDiskLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayDiskLightEditPackage, MoonrayLightEditPackageUIDelegate)
 
UIDelegate.RegisterUIDelegateClass(MoonrayDistantLightPackage, MoonrayDistantLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayDistantLightEditPackage, MoonrayLightEditPackageUIDelegate)
 
UIDelegate.RegisterUIDelegateClass(MoonrayEnvLightPackage, MoonrayEnvLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayEnvLightEditPackage, MoonrayLightEditPackageUIDelegate)
 
UIDelegate.RegisterUIDelegateClass(MoonrayRectLightPackage, MoonrayRectLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayRectLightEditPackage, MoonrayLightEditPackageUIDelegate)
 
UIDelegate.RegisterUIDelegateClass(MoonraySphereLightPackage, MoonraySphereLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonraySphereLightEditPackage, MoonrayLightEditPackageUIDelegate)
 
UIDelegate.RegisterUIDelegateClass(MoonraySpotLightPackage, MoonraySpotLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonraySpotLightEditPackage, MoonrayLightEditPackageUIDelegate)

UIDelegate.RegisterUIDelegateClass(MoonrayMeshLightPackage, MoonrayMeshLightUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayMeshLightEditPackage, MoonrayLightEditPackageUIDelegate)
