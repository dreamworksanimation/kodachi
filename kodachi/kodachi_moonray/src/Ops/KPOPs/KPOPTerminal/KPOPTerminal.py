# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# Register implicit resolvers defined in KPOPTerminal.cc

def RegisterLightInputResolve(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute

    # this must be done before the MaterialResolve
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeStandardResolvers,
        "LightInputResolve", FnAttribute.GroupAttribute(), addSystemArgs=True)

RegisterLightInputResolve()
del RegisterLightInputResolve

