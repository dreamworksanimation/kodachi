# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerDeferredPrune():
    """
    Registers a new DeferredPrune node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    def buildDeferredPruneOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeOperation}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        CELParam       = node.getParameter('CEL')
        
        # Based on initial feedback from the KUG, we always want to cook the
        # daps. The op defaults to true, so for now we will do nothing
        #cookDapsParam = node.getParameter('cookDaps')
 
        CEL       = CELParam.getValue(0)

        gb = FnAttribute.GroupBuilder()

        interface.addOpSystemArgs(gb)
        gb.set('CEL', FnAttribute.StringAttribute(CEL))

        interface.appendOp('DeferredPrune', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('DeferredPrune')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute(''))

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('CEL', {'widget': 'cel'})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildDeferredPruneOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()


def registerDeferredPruneRestore():
    """
    Registers a new DeferredPruneRestore node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    def buildDeferredPruneRestoreOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.AttributeOperation}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """

        # Parse node parameters
        restorePathsParam       = node.getParameter('restorePaths')
        restorePaths            = [restorePathsParam.getChildByIndex(i).getValue(0)
                                   for i in xrange(restorePathsParam.getNumChildren())]

        gb = FnAttribute.GroupBuilder()

        interface.addOpSystemArgs(gb)
        gb.set('restorePaths', FnAttribute.StringAttribute(restorePaths))

        interface.appendOp('DeferredPruneRestore', gb.build())

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('DeferredPruneRestore')

    # Add input port
    nodeTypeBuilder.setInputPortNames(("in",))

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set('restorePaths', FnAttribute.StringAttribute(''))

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build(), forceArrayNames=['restorePaths'])

    # Set parameter hints
    nodeTypeBuilder.setHintsForParameter('restorePaths', {'widget': 'scenegraphLocationArray'})

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildDeferredPruneRestoreOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerDeferredPrune()
registerDeferredPruneRestore()
