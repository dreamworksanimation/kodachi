# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# Node to execute the MoonrayMeshLightPostMerge op in kodachi_moonray
# This copies info from the MeshLight to the lightList so MoonrayMeshLightResolve can
# identify the mesh and modify it.

def registerMeshLightPostMerge():
    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    nb = Nodes3DAPI.NodeTypeBuilder('MoonrayMeshLightPostMerge')
    nb.setIsHiddenFromMenus(True)

    nb.setInputPortNames(('in',))

    nb.setParametersTemplateAttr(FnAttribute.GroupBuilder()
                                 .set('path', FnAttribute.StringAttribute(''))
                                 .set('geometry', FnAttribute.StringAttribute(''))
                                 .build())

    def buildOpChain(self, interface):

        time = interface.getFrameTime()
        path = self.getParameter('path').getValue(time)
        if not path: return
        geometry = self.getParameter('geometry').getValue(time)
        if not geometry: return

        gb = FnAttribute.GroupBuilder()
        gb.set('path', FnAttribute.StringAttribute(path))
        gb.set('geometry', FnAttribute.StringAttribute(geometry))
        interface.appendOp('MoonrayMeshLightPostMerge', gb.build())

    nb.setBuildOpChainFnc(buildOpChain)

    nb.build()

registerMeshLightPostMerge()
del registerMeshLightPostMerge
