# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from PackageSuperToolAPI import UIDelegate, NodeUtils as NU, Packages
from Katana import Plugins
from MoonrayLightFilterPackage import *
from GafferBase import GafferUIDelegateBase

GafferThreeAPI = Plugins.GafferThreeAPI
LightFilterUIDelegate = UIDelegate.GetUIDelegateClassForPackageClass(
    GafferThreeAPI.PackageClasses.LightFilterPackage)
LightFilterEditUIDelegate = UIDelegate.GetUIDelegateClassForPackageClass(
    GafferThreeAPI.PackageClasses.LightFilterEditPackage)


class _LightFilterUIDelegate(LightFilterUIDelegate):
    DefaultShortcut = ''

class MoonrayLightFilterPackageUIDelegate(GafferUIDelegateBase, LightFilterUIDelegate):
    _shaderType = 'moonrayLightfilter'  
    DefaultShortcut = None 
    def getTabPolicy(self, tabName):
        if tabName == "Object":
            return None
        return self.initTabPolicy(self, LightFilterUIDelegate, tabName)

class MoonrayLightFilterEditPackageUIDelegate(GafferUIDelegateBase, LightFilterEditUIDelegate):
    _shaderType = 'moonrayLightfilter'
    def getTabPolicy(self, tabName):
        return self.initTabPolicy(self, LightFilterEditUIDelegate, tabName)
    
class MoonrayDecayLightFilterUIDelegate(MoonrayLightFilterPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayDecayLightFilter'
    DefaultShortcut = 'Ctrl+9'
    
class MoonrayBarnDoorsLightFilterUIDelegate(MoonrayLightFilterPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayBarnDoorsLightFilter'
    DefaultShortcut = 'Ctrl+0'
    
class MoonrayIntensityLightFilterUIDelegate(MoonrayLightFilterPackageUIDelegate):
    AddPackageActionHash = 'GafferThree-AddMoonrayIntensityLightFilter'
    DefaultShortcut = 'Ctrl+8'

UIDelegate.RegisterUIDelegateClass(GafferThreeAPI.PackageClasses.LightFilterPackage, _LightFilterUIDelegate)

UIDelegate.RegisterUIDelegateClass(MoonrayDecayLightFilterPackage, MoonrayDecayLightFilterUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayDecayLightFilterEditPackage, MoonrayLightFilterEditPackageUIDelegate)

UIDelegate.RegisterUIDelegateClass(MoonrayBarnDoorsLightFilterPackage, MoonrayBarnDoorsLightFilterUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayBarnDoorsLightFilterEditPackage, MoonrayLightFilterEditPackageUIDelegate)

UIDelegate.RegisterUIDelegateClass(MoonrayIntensityLightFilterPackage, MoonrayIntensityLightFilterUIDelegate)
UIDelegate.RegisterUIDelegateClass(MoonrayIntensityLightFilterEditPackage, MoonrayLightFilterEditPackageUIDelegate)
