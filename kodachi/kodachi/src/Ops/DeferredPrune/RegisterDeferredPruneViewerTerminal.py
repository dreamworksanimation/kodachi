# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# Register the AttributeOperation resolver. This evaluated deferred attribute
# operations. Should be run after lookfiles and MaterialResolve have been applied
def RegisterDeferredPruneResolver(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "DeferredPruneViewerTerminal", FnAttribute.GroupAttribute(), addSystemArgs=True)

#register the implicit resolver
RegisterDeferredPruneResolver()


