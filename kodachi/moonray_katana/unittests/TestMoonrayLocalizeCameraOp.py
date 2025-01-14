# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from kodachi.test.harness import TestHarness
from kodachi import FnAttribute, FnGeolibServices

# Run with "env GEOLIB_PLUGINS_PATH=/rel/test/moonray_katana_dev/moonray_katana/0.24.0.1/katana/2.6v1/Resources/Ops/ nosetests test.py" for example

class TestMoonrayLocalizeCameraOp(TestHarness):
    def testCreate(self):
        txn = self.runtime.createTransaction()
        
        # Setup the scene
        op = txn.createOp()
        
        cameraName = "/root/world/cam/main"
        cameraNameAttr = FnAttribute.StringAttribute(cameraName)
           
        sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()

        # First create the camera attrs and save for later comparison
        fovAttr = FnAttribute.DoubleAttribute(45)
        gb = FnAttribute.GroupBuilder()
        gb.set("type", FnAttribute.StringAttribute("camera"))
        gb.set("geometry.fov", fovAttr)
        cameraAttrs = gb.build()
        
        # Next create the camera location
        sscb.setAttrAtLocation(cameraName, "geometry.fov", fovAttr)
        sscb.setAttrAtLocation(cameraName, "type", FnAttribute.StringAttribute("camera"))
        
        # Use the StaticSceneCreate to set the scene
        sscb.setAttrAtLocation("", "renderSettings.cameraName", cameraNameAttr)
        txn.setOpArgs(op, 'StaticSceneCreate', sscb.build())
        
        # Create and connect our op to test
        op2 = txn.createOp()
        txn.setOpArgs(op2, 'MoonrayLocalizeCamera', FnAttribute.GroupAttribute())
        txn.setOpInputs(op2, [op])

        # Commit the transaction
        client = txn.createClient()
        txn.setClientOp(client, op2)

        self.runtime.commit([txn])
        
        # Check the expected attrs
        rootAttrs = client.cookLocation('/root').getAttrs()
        expected = FnAttribute.GroupBuilder().set(
            "renderSettings.cameraName", cameraNameAttr).set(
            "type", "group").set(
            "renderSettings.cameraAttrs", cameraAttrs).build()

        self.assertEqual(rootAttrs, expected, rootAttrs.getXML())
