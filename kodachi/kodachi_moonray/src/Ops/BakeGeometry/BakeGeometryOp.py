# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerMoonrayBakeGeometry():
    """
    Registers a new MoonrayBakeGeometry node type using the NodeTypeBuilder utility
    class.
    """
    from Katana import Nodes3DAPI, FnAttribute
    
    def buildBakeGeometryOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeOperation}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        from Katana import FnGeolibServices
        
        # Append op
        interface.appendOp('BakeGeometryOp')

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('MoonrayBakeGeometry')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("input",))

    # Node parameters determined by MoonrayMeshSettings settings

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildBakeGeometryOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
#registerMoonrayBakeGeometry()

# Register the BakeGeometry resolver.  This processes subdmesh locations
# baking them via Moonray.
def RegisterBakeGeometryResolvers(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.BeforeViewerResolvers,
        "BakedGeometryViewerOp", FnAttribute.GroupAttribute(), addSystemArgs=True)

RegisterBakeGeometryResolvers()

