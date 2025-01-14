# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from Katana import Callbacks

def registerContextSwitchNode():
    """
    Registers a new ContextSwitch node type using the NodeTypeBuilder utility
    class.
    """

    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute

    def buildContextSwitchSetupOpChain(node, interface):
        """
        Defines the callback function used to create the Ops chain for the
        node type being registered.

        @type node: C{Nodes3DAPI.NodeTypeBuilder.MultiContextSetup}
        @type interface: C{Nodes3DAPI.NodeTypeBuilder.BuildChainInterface}
        @param node: The node for which to define the Ops chain
        @param interface: The interface providing the functions needed to set
            up the Ops chain for the given node.
        """
        interface.setExplicitInputRequestsEnabled(True)       
        
        graphState = interface.getGraphState()
        
        contextIdEntry = graphState.getDynamicEntry('var:contextID')
        
        if contextIdEntry:
            contextId = contextIdEntry.getValue()
            interface.addInputRequest(contextId, graphState, Nodes3DAPI.NodeTypeBuilder.OpChainInterface.SKIP)
        else:
            inputNames = []
            for inputPort in node.getInputPorts():
                name = inputPort.getName()
                interface.addInputRequest(name, graphState)
                inputNames.append(name)
                
            gb = FnAttribute.GroupBuilder()
            gb.set('inputNames', FnAttribute.StringAttribute(inputNames))
            interface.appendOp('ContextSwitch', gb.build())
        
    def buildContextSwitchSetupParameters(self):
        # XXX, hack to get multi-input node shape
        if not hasattr(self.__class__, '_INIT'):
            self.__class__._INIT = True
            self.__class__.__name__ = 'MultiComposite'

    # Create a NodeTypeBuilder to register the new type
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('ContextSwitch')
    
    def getScenegraphLocation(self, frameTime):
        return "/root"

    nodeTypeBuilder.setGetScenegraphLocationFnc(getScenegraphLocation)
    
    
    def appendToParametersOpChain(self, interface):    
        frameTime = interface.getFrameTime()
        
        location = self.getScenegraphLocation(frameTime)

        uiscript = '''
            layoutNames = {}
            layoutsAttr = Interface.GetAttr("moonrayArrasSettings.multiContext.layouts")
            if layoutsAttr then
                for i=0, layoutsAttr:getNumberOfChildren()-1 do
                    layoutNames[#layoutNames+1] = layoutsAttr:getChildName(i)
                end
            end
                
            Interface.SetAttr("__extraHints." ..
                    Attribute.DelimiterEncode("__multiContextSetup.layout"),
                            GroupBuilder()
                                :set('widget', StringAttribute('popup'))
                                :set('options', StringAttribute(layoutNames))
                                :set('editable', IntAttribute(0))
                                :build())
        '''
        
        sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate(True)
        
        sscb.addSubOpAtLocation(location, 'OpScript.Lua',
                FnAttribute.GroupBuilder()
                    .set('script', uiscript)
                    .build())
        
        interface.appendOp('StaticSceneCreate', sscb.build())
    
    #nodeTypeBuilder.setAppendToParametersOpChainFnc(appendToParametersOpChain)

    #nodeTypeBuilder.setGenericAssignRoots('args', '__multiContextSetup')

    # Build the node's parameters
    gb = FnAttribute.GroupBuilder()
    gb.set("args.layout", "")

    # Set the parameters template
    nodeTypeBuilder.setParametersTemplateAttr(gb.build())
    
    nodeTypeBuilder.setBuildParametersFnc(buildContextSwitchSetupParameters)

    # Set the callback responsible to build the Ops chain
    nodeTypeBuilder.setBuildOpChainFnc(buildContextSwitchSetupOpChain)
    
    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerContextSwitchNode()