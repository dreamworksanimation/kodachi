# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks


# Register the filename resolver.  
def RegisterFilenameResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute
    
    opArgs = FnAttribute.GroupBuilder()
    opArgs.set("attrRoot", FnAttribute.StringAttribute("moonrayGlobalStatements"))
    opArgs.set("attrScope", FnAttribute.StringAttribute("//*"))

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeStandardResolvers,
        "FilenameResolve", opArgs.build(), addSystemArgs=True)

RegisterFilenameResolvers()


