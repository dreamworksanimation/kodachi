# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from kodachi.test.harness import TestHarness
from kodachi import FnAttribute

class TestTestHarness(TestHarness):
    def testCreate(self):
        txn = self.runtime.createTransaction()
        op = txn.createOp()
        txn.setOpArgs(op, 'AttributeSet', FnAttribute.GroupBuilder()
           .set('locationPaths', '/root')
           .set('setAttrs.i0.name', 'taco')
           .set('setAttrs.i0.attr', 1).build())

        client = txn.createClient()
        txn.setClientOp(client, op)

        self.runtime.commit([txn])
        xml = client.cookLocation('/root').getAttrs().getXML()
        expected = """<attr type="GroupAttr">
  <attr name="taco" tupleSize="1" type="IntAttr">
    <sample size="1" time="0" value="1 "/>
  </attr>
  <attr name="type" tupleSize="1" type="StringAttr">
    <sample size="1" time="0">
      <str value="group"/>
    </sample>
  </attr>
</attr>
"""
        self.assertEqual(xml, expected)

if __name__ == "__main__":
    import unittest
    unittest.main()

