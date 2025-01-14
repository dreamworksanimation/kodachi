# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def RegisterOpSchemaResolve(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "OpSchemaResolve", FnAttribute.GroupAttribute(), addSystemArgs=True)

RegisterOpSchemaResolve()

