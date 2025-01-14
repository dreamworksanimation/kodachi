# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from kodachi.test.harness import TestHarness
from kodachi import FnAttribute, FnGeolibServices

# Run with "env GEOLIB_PLUGINS_PATH=%MOONRAY_FOLIO_PATH%/Resources/Ops/ nosetests %TEST%.py"

class TestMoonrayFlattenMaterialOp(TestHarness):
    # Test creating 3 non-network materials, then running the
    # MaterialToNetworkMaterial op before the MoonrayFlattenMaterial op
	def testNonNetworkMaterial(self):
		txn = self.runtime.createTransaction()
		
		# Setup the scene
		op = txn.createOp()
		
		mat1 = FnAttribute.StringAttribute("/root/world/mat/matWorld")
		mat2 = FnAttribute.StringAttribute("/root/world/mat/matGeo")
		mat3 = FnAttribute.StringAttribute("/root/world/mat/matBox")
		
		sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()

		sscb.setAttrAtLocation("/root/world/geo/box", "type", FnAttribute.StringAttribute("polymesh"))
		
		# /root/world material - Material (will be overridden and unused)
		sscb.setAttrAtLocation(mat1.getValue(), "material.moonrayMaterialParams.roughness", FnAttribute.IntAttribute(5))
		sscb.setAttrAtLocation(mat1.getValue(), "material.moonrayMaterialShader", FnAttribute.StringAttribute("DwaSolidDielectricMaterial"))
		sscb.setAttrAtLocation(mat1.getValue(), "type", FnAttribute.StringAttribute("material"))
		
		# /root/world/geo material - Displacement
		sscb.setAttrAtLocation(mat2.getValue(), "material.moonrayDisplacementParams.height", FnAttribute.IntAttribute(4))
		sscb.setAttrAtLocation(mat2.getValue(), "material.moonrayDisplacementShader", FnAttribute.StringAttribute("NormalDisplacement"))
		sscb.setAttrAtLocation(mat2.getValue(), "type", FnAttribute.StringAttribute("material"))
		
		# /root/world/geo/box material - Material
		sscb.setAttrAtLocation(mat3.getValue(), "material.moonrayMaterialParams.metallic", FnAttribute.IntAttribute(1))
		sscb.setAttrAtLocation(mat3.getValue(), "material.moonrayMaterialShader", FnAttribute.StringAttribute("DwaBaseMaterial"))
		sscb.setAttrAtLocation(mat3.getValue(), "type", FnAttribute.StringAttribute("material"))
		
		sscb.setAttrAtLocation("/root/world", "materialAssign", mat1)
		sscb.setAttrAtLocation("/root/world/geo", "materialAssign", mat2)
		sscb.setAttrAtLocation("/root/world/geo/box", "materialAssign", mat3)
		
		# Use the StaticSceneCreate to set the scene
		txn.setOpArgs(op, 'StaticSceneCreate', sscb.build())
		
		opMatResolve = txn.createOp()
		txn.setOpArgs(opMatResolve, 'MaterialResolve', FnAttribute.GroupAttribute())
		txn.setOpInputs(opMatResolve, [op])
		
		# Create and connect our ops to test
		opMatToNetworkMat = txn.createOp()
		txn.setOpArgs(opMatToNetworkMat, 'MaterialToNetworkMaterial', FnAttribute.GroupAttribute())
		txn.setOpInputs(opMatToNetworkMat, [opMatResolve])
		
		opCookMat = txn.createOp()
		txn.setOpArgs(opCookMat, 'MoonrayCookMaterialInterface', FnAttribute.GroupAttribute())
		txn.setOpInputs(opCookMat, [opMatToNetworkMat])
		
		opFlattenMat = txn.createOp()
		txn.setOpArgs(opFlattenMat, 'MoonrayFlattenMaterial', FnAttribute.GroupAttribute())
		txn.setOpInputs(opFlattenMat, [opCookMat])

		# Commit the transaction
		client = txn.createClient()
		txn.setClientOp(client, opFlattenMat)

		self.runtime.commit([txn])
		
		# Check the expected attrs
		attrs = client.cookLocation('/root/world/geo/box').getAttrs()

		# Final output is a network material with a material and displacement
		# metallic (but not roughness) will be on the material
		# height will be on the displacement
		# Since the material is "more local", it will receive index 0,
		# while the displacement will receive index 1.
		self.assertEqual(attrs.getChildByName('material.terminals.moonrayMaterial').getValue(), 'Node_moonrayMaterial:0')
		self.assertEqual(attrs.getChildByName('material.terminals.moonrayDisplacement').getValue(), 'Node_moonrayDisplacement:1')
		self.assertEqual(attrs.getChildByName('material.nodes.Node_moonrayMaterial:0.parameters.metallic').getValue(), 1)
		self.assertEqual(attrs.getChildByName('material.nodes.Node_moonrayDisplacement:1.parameters.height').getValue(), 4)
		self.assertEqual(attrs.getChildByName('material.nodes.Node_moonrayMaterial:0.parameters.roughness'), None)
		
    # Test creating 3 network materials and then running MoonrayFlattenMaterial
    # op. Check a special case for when a node's connection references a node
    # defined after it in the nodes sub-attr, to ensure all names are
    # remapping properly regardless of node order.
	def testNetworkMaterial(self):
		txn = self.runtime.createTransaction()
		
		# Setup the scene
		op = txn.createOp()
		
		mat1 = FnAttribute.StringAttribute("/root/world/mat/matWorld")
		mat2 = FnAttribute.StringAttribute("/root/world/mat/matGeo")
		mat3 = FnAttribute.StringAttribute("/root/world/mat/matBox")
		
		sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()

		sscb.setAttrAtLocation("/root/world/geo/box", "type", FnAttribute.StringAttribute("polymesh"))
		
		# /root/world material - Material (will be overridden and unused)
		sscb.setAttrAtLocation(mat1.getValue(), "material.terminals.moonrayMaterial", FnAttribute.StringAttribute("mat1"))
		sscb.setAttrAtLocation(mat1.getValue(), "material.nodes.mat1.parameters.roughness", FnAttribute.IntAttribute(5))
		sscb.setAttrAtLocation(mat1.getValue(), "material.nodes.mat1.name", FnAttribute.StringAttribute("mat1"))
		sscb.setAttrAtLocation(mat1.getValue(), "type", FnAttribute.StringAttribute("material"))
		
		# /root/world/geo material - Displacement
		sscb.setAttrAtLocation(mat2.getValue(), "material.terminals.moonrayDisplacement", FnAttribute.StringAttribute("mat2"))
		sscb.setAttrAtLocation(mat2.getValue(), "material.nodes.mat2.parameters.height", FnAttribute.IntAttribute(4))
		sscb.setAttrAtLocation(mat2.getValue(), "material.nodes.mat2.connections.testConn", FnAttribute.StringAttribute("out@zzz"))
		sscb.setAttrAtLocation(mat2.getValue(), "material.nodes.mat2.name", FnAttribute.StringAttribute("mat2"))
		sscb.setAttrAtLocation(mat2.getValue(), "material.nodes.zzz.parameters.test", FnAttribute.IntAttribute(10))
		sscb.setAttrAtLocation(mat2.getValue(), "material.nodes.zzz.name", FnAttribute.StringAttribute("zzz"))
		sscb.setAttrAtLocation(mat2.getValue(), "type", FnAttribute.StringAttribute("material"))
		
		# /root/world/geo/box material - Material
		sscb.setAttrAtLocation(mat3.getValue(), "material.terminals.moonrayMaterial", FnAttribute.StringAttribute("mat3"))
		sscb.setAttrAtLocation(mat3.getValue(), "material.nodes.mat3.parameters.metallic", FnAttribute.IntAttribute(1))
		sscb.setAttrAtLocation(mat3.getValue(), "material.nodes.mat3.name", FnAttribute.StringAttribute("mat3"))
		sscb.setAttrAtLocation(mat3.getValue(), "type", FnAttribute.StringAttribute("material"))
		
		sscb.setAttrAtLocation("/root/world", "materialAssign", mat1)
		sscb.setAttrAtLocation("/root/world/geo", "materialAssign", mat2)
		sscb.setAttrAtLocation("/root/world/geo/box", "materialAssign", mat3)
		
		# Use the StaticSceneCreate to set the scene
		txn.setOpArgs(op, 'StaticSceneCreate', sscb.build())
		
		opMatResolve = txn.createOp()
		txn.setOpArgs(opMatResolve, 'MaterialResolve', FnAttribute.GroupAttribute())
		txn.setOpInputs(opMatResolve, [op])
		
		# Create and connect our ops to test
		opCookMat = txn.createOp()
		txn.setOpArgs(opCookMat, 'MoonrayCookMaterialInterface', FnAttribute.GroupAttribute())
		txn.setOpInputs(opCookMat, [opMatResolve])
		
		opFlattenMat = txn.createOp()
		txn.setOpArgs(opFlattenMat, 'MoonrayFlattenMaterial', FnAttribute.GroupAttribute())
		txn.setOpInputs(opFlattenMat, [opCookMat])

		# Commit the transaction
		client = txn.createClient()
		txn.setClientOp(client, opFlattenMat)

		self.runtime.commit([txn])
		
		# Check the expected attrs
		attrs = client.cookLocation('/root/world/geo/box').getAttrs()

		# Final output is a network material with a material and displacement
		# metallic (but not roughness) will be on the material
		# height will be on the displacement
		# Since the material is "more local", it will receive index 0,
		# while the displacement will receive index 1.
		self.assertEqual(attrs.getChildByName('material.terminals.moonrayMaterial').getValue(), 'mat3:0')
		self.assertEqual(attrs.getChildByName('material.terminals.moonrayDisplacement').getValue(), 'mat2:1')
		self.assertEqual(attrs.getChildByName('material.nodes.mat3:0.parameters.metallic').getValue(), 1)
		self.assertEqual(attrs.getChildByName('material.nodes.mat2:1.parameters.height').getValue(), 4)
		self.assertEqual(attrs.getChildByName('material.nodes.mat3:0.parameters.roughness'), None)
		# Make sure the connection is still a valid node, and not 'out@'
		self.assertEqual(attrs.getChildByName('material.nodes.mat2:1.connections.testConn').getValue(), 'out@zzz:2')

