# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerAttributeOperationResolve():
    """
    Registers a new AttributeOperationResolve node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    def buildAttributeOperationResolveOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeOperationResolve}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        
        interface.appendOp('AttributeOperationResolve', FnAttribute.GroupBuilder().build())
        
    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('AttributeOperationResolve')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))
    
    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildAttributeOperationResolveOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the AttributeOperation resolver. This evaluated deferred attribute
# operations. Should be run after lookfiles and MaterialResolve have been applied
def RegisterAttributeOperationResolver(*args, **kwargs):
    import Nodes3DAPI
    from Katana import FnAttribute

    Nodes3DAPI.RegisterImplicitResolver(
        Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers,
        "AttributeOperationResolve", FnAttribute.GroupAttribute(), addSystemArgs=True)

#register the node
registerAttributeOperationResolve()

#register the implicit resolver
RegisterAttributeOperationResolver()


