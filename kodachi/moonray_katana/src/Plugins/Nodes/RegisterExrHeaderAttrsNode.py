# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def registerExrHeaderAttrsNode():
    from Katana import Nodes3DAPI, FnGeolibServices, FnAttribute
    
    def buildOpChain(self, interface):
        time = interface.getFrameTime()
        asb = FnGeolibServices.OpArgsBuilders.AttributeSet()
        asb.setLocationPaths(FnAttribute.StringAttribute('/root'))
        
        # Set value on exr_header_attributes for every render output listed
        name = self.getParameter('name').getValue(time)
        if name:
            outputs = self.getParameter('args.outputs.value').getValue(time).split(',')
            for output in outputs:
                if output:
                    type = self.getParameter('type').getValue(time)
                    asb.setAttr('renderSettings.outputs.{0}.rendererSettings.exr_header_attributes.{1}'.format(output, name),
                                interface.buildAttrFromParam(self.getParameter(type)))
                      
              
            gb = FnAttribute.GroupBuilder()
            gb.update(asb.build())
            interface.appendOp('AttributeSet', gb.build())
    
    def getScenegraphLocation(self, frameTime):
        return "/root"
    
    def appendToParametersOpChain(self, interface):
        frameTime = interface.getFrameTime()
        location = self.getScenegraphLocation(frameTime)

        uiscript = '''
outputsAttr = InterfaceUtils.CookDaps('renderSettings', '/root')
outputsAttr = outputsAttr:getChildByName('renderSettings.outputs')
outputs = {}
for i=1,outputsAttr:getNumberOfChildren() do
    outputs[i] = outputsAttr:getChildName(i-1)
end
Interface.SetAttr("__extraHints." ..
        Attribute.DelimiterEncode("__exrHeaderAttrs.outputs"),
                GroupBuilder()
                    :set('widget', StringAttribute('nonexclusiveCheckboxPopup'))
                    :set('options', StringAttribute(outputs))
                    :set('emptyLabel', StringAttribute('none'))
                    :build())
        '''
        sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate(True)
        sscb.addSubOpAtLocation(location, 'OpScript.Lua',
                FnAttribute.GroupBuilder()
                    .set('script', uiscript)
                    .build())
        interface.appendOp('StaticSceneCreate', sscb.build())
    
    nodeTypeBuilder = Nodes3DAPI.NodeTypeBuilder('ExrHeaderAttr')
    nodeTypeBuilder.setBuildOpChainFnc(buildOpChain)
    nodeTypeBuilder.setGetScenegraphLocationFnc(getScenegraphLocation)
    nodeTypeBuilder.setInputPortNames(("in",))
    nodeTypeBuilder.setGenericAssignRoots('args', '__exrHeaderAttrs')
    nodeTypeBuilder.setAppendToParametersOpChainFnc(appendToParametersOpChain)
    nodeTypeBuilder.setParametersTemplateAttr(FnAttribute.GroupBuilder()
          .set('args.outputs', FnAttribute.StringAttribute('primary'))
          .set('name', FnAttribute.StringAttribute(''))
          .set('type', FnAttribute.StringAttribute('string'))
          .set('string', FnAttribute.StringAttribute(''))
          .set('float', FnAttribute.FloatAttribute(0))
          .set('double', FnAttribute.DoubleAttribute(0))
          .set('int', FnAttribute.IntAttribute(0))
          .set('m33f', FnAttribute.FloatAttribute([0, 0, 0, 0, 0, 0, 0, 0, 0], 3))
          .set('m44f', FnAttribute.FloatAttribute([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0], 4))
          .set('v2i', FnAttribute.IntAttribute([0, 0]))
          .set('v3i', FnAttribute.IntAttribute([0, 0, 0]))
          .set('v2f', FnAttribute.FloatAttribute([0, 0]))
          .set('v3f', FnAttribute.FloatAttribute([0, 0, 0]))
          .build())
    
    # These are the only header types that Moonray currently supports
    options = ['string','float','double','int',
               'm33f','m44f','v2i','v3i','v2f','v3f']
    nodeTypeBuilder.setHintsForParameter('type', {
        'widget': 'popup',
        'options': options
    })

    # Only show a single 'value' field at a time depending on 'type'
    for option in options:
        nodeTypeBuilder.setHintsForParameter(option, {
            'conditionalVisOps': {
                'conditionalVisPath': '../type',
                'conditionalVisOp': 'equalTo',
                'conditionalVisValue': option
            }
        })

    # Build the new node type
    nodeTypeBuilder.build()

# Register the node
registerExrHeaderAttrsNode()
