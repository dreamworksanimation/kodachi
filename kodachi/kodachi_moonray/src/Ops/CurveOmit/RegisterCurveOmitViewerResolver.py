# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def RegisterCurveOmitViewerResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute
        
    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterViewerResolvers,
        "CurveOmit", FnAttribute.GroupAttribute(), addSystemArgs=True)

RegisterCurveOmitViewerResolvers()

