# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# Register the UDIMPaths location resolver.
def RegisterUDIMPathsResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute
 
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeStandardResolvers,
        "UDIMPathsResolver", FnAttribute.GroupAttribute(), addSystemArgs=False)
 
RegisterUDIMPathsResolvers()
del RegisterUDIMPathsResolvers

