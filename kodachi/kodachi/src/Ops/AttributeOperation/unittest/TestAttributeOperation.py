# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

from kodachi.test.harness import TestHarness
from kodachi import FnAttribute, FnGeolibServices

import math

# Run with "env GEOLIB_PLUGINS_PATH=%MOONRAY_FOLIO_PATH%/Resources/Ops/ nosetests %TEST%.py"

testLocation = '/root/world/geo/op'
testAttrName = 'testAttr'
    
def createTestLocation(txn, attr):
    op = txn.createOp()
    
    sscb = FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
        
    sscb.setAttrAtLocation(testLocation, 'type', FnAttribute.StringAttribute('polymesh'))
    sscb.setAttrAtLocation(testLocation, testAttrName, attr)
    
    txn.setOpArgs(op, 'StaticSceneCreate', sscb.build())
    
    return op

def addAttributeOperation(txn, operation, value = None, mode = 0):
    opAttributeOperation = txn.createOp()
         
    gb = FnAttribute.GroupBuilder()
    gb.set('CEL', FnAttribute.StringAttribute(testLocation))
    gb.set('attributeName', FnAttribute.StringAttribute(testAttrName))
    gb.set('operation', FnAttribute.StringAttribute(operation))
    gb.set('mode', FnAttribute.IntAttribute(mode))
    if value is not None:
        gb.set('value', value)
     
    txn.setOpArgs(opAttributeOperation, 'AttributeOperation', gb.build())
    
    return opAttributeOperation

def executeOperation(runtime, initialValue, operation, operationValue = None):
    txn = runtime.createTransaction()
    
    opSscb = createTestLocation(txn, initialValue)
    
    opAttributeOperation = addAttributeOperation(txn, operation, operationValue)
    
    txn.setOpInputs(opAttributeOperation, [opSscb])
    
    client = txn.createClient()
    txn.setClientOp(client, opAttributeOperation)
    
    runtime.commit([txn])
    
    attrs = client.cookLocation(testLocation).getAttrs()
    
    return attrs.getChildByName(testAttrName).getNearestSample(0)

class TestMoonrayFlattenMaterialOp(TestHarness):
    def testAdd(self):       
        result = executeOperation(self.runtime, 
                                  FnAttribute.IntAttribute([5, -3]), 
                                  'add', 
                                  FnAttribute.IntAttribute(2))
        
        self.assertEqual(result, [7, -1])
        
    def testSubtract(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.IntAttribute([5, -3]), 
                                  'subtract', 
                                  FnAttribute.IntAttribute(2))
        
        self.assertEqual(result, [3, -5])
    
    def testMultiply(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.IntAttribute([5, -3]), 
                                  'multiply', 
                                  FnAttribute.IntAttribute(2))
        
        self.assertEqual(result, [10, -6])
        
    def testDivide(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5.0, -3.0]), 
                                  'divide', 
                                  FnAttribute.IntAttribute(2))
        
        self.assertAlmostEqual(result[0], 2.5)
        self.assertAlmostEqual(result[1], -1.5)
        
    def testPow(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.IntAttribute([5, -3]), 
                                  'pow', 
                                  FnAttribute.IntAttribute(2))
        
        self.assertEqual(result, [25, 9])
        
    def testMin(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0.5, 1.5]), 
                                  'min', 
                                  FnAttribute.DoubleAttribute(1))
        
        self.assertAlmostEqual(result[0], 0.5)
        self.assertAlmostEqual(result[1], 1)
        
    def testMax(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0.5, 1.5]), 
                                  'max', 
                                  FnAttribute.DoubleAttribute(1))
        
        self.assertAlmostEqual(result[0], 1)
        self.assertAlmostEqual(result[1], 1.5)
        
    def testCopysign(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([-0.5, 1.5]), 
                                  'copysign', 
                                  FnAttribute.DoubleAttribute(-1))
        
        self.assertAlmostEqual(result[0], -0.5)
        self.assertAlmostEqual(result[1], -1.5)
        
    def testFmod(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([8.1, 6.0]), 
                                  'fmod', 
                                  FnAttribute.DoubleAttribute(3.0))
        
        self.assertAlmostEqual(result[0], 2.1)
        self.assertAlmostEqual(result[1], 0)                
        
    def testAbs(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.IntAttribute([5, -3]), 
                                  'abs')
        
        self.assertEqual(result, [5, 3])
        
    def testAcos(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([-1, 0]), 
                                  'acos')
        
        self.assertAlmostEqual(result[0], math.pi)
        self.assertAlmostEqual(result[1], math.pi / 2)
        
    def testAsin(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0, 1]), 
                                  'asin')
        
        self.assertAlmostEqual(result[0], 0.0)
        self.assertAlmostEqual(result[1], math.pi / 2)
        
    def testAtan(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0, 1]), 
                                  'atan')
        
        self.assertAlmostEqual(result[0], 0.0)
        self.assertAlmostEqual(result[1], math.pi / 4)
        
    def testCeil(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([4.5, -3.25]), 
                                  'ceil')
        
        self.assertEqual(result, [5, -3])
        
    def testCos(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0, math.pi]), 
                                  'cos')
        
        self.assertAlmostEqual(result[0], 1.0)
        self.assertAlmostEqual(result[1], -1.0)
        
    def testExp(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5, -3]), 
                                  'exp')
        
        self.assertAlmostEqual(result[0], math.exp(5))
        self.assertAlmostEqual(result[1], math.exp(-3))
        
    def testExpm1(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([1e-5, 1e-7]), 
                                  'expm1')
        
        self.assertAlmostEqual(result[0], math.expm1(1e-5))
        self.assertAlmostEqual(result[1], math.expm1(1e-7))
        
    def testExp2(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5, -3]), 
                                  'exp2')
        
        self.assertAlmostEqual(result[0], 2**5)
        self.assertAlmostEqual(result[1], 2**-3)
        
    def testFloor(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([4.5, -3.25]), 
                                  'floor')
        
        self.assertEqual(result, [4, -4])
        
    def testLog(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5, 27]), 
                                  'log')
        
        self.assertAlmostEqual(result[0], math.log(5))
        self.assertAlmostEqual(result[1], math.log(27))
        
    def testLog1p(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([1e-5, 1e-7]), 
                                  'log1p')
        
        self.assertAlmostEqual(result[0], math.log1p(1e-5))
        self.assertAlmostEqual(result[1], math.log1p(1e-7))
        
    def testLog2(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5, 27]), 
                                  'log2')
        
        self.assertAlmostEqual(result[0], math.log(5, 2))
        self.assertAlmostEqual(result[1], math.log(27, 2))
        
    def testLog10(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5, 27]), 
                                  'log10')
        
        self.assertAlmostEqual(result[0], math.log10(5))
        self.assertAlmostEqual(result[1], math.log10(27))
        
    def testNegate(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.IntAttribute([5, -3]), 
                                  'negate')
        
        self.assertEqual(result, [-5, 3])
        
    def testRound(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([2.3, 2.7, -2.5]), 
                                  'round')
        
        self.assertAlmostEqual(result[0], 2)
        self.assertAlmostEqual(result[1], 3)
        self.assertAlmostEqual(result[2], -3)      
        
    def testSin(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0, math.pi / 6]), 
                                  'sin')
        
        self.assertAlmostEqual(result[0], 0.0)
        self.assertAlmostEqual(result[1], 0.5)
        
    def testSqrt(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([5, 27]), 
                                  'sqrt')
        
        self.assertAlmostEqual(result[0], math.sqrt(5))
        self.assertAlmostEqual(result[1], math.sqrt(27))
        
    def testTrunc(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([2.3, 2.7, -2.5]), 
                                  'trunc')
        
        self.assertAlmostEqual(result[0], 2)
        self.assertAlmostEqual(result[1], 2)
        self.assertAlmostEqual(result[2], -2)   
        
    def testTan(self):
        result = executeOperation(self.runtime, 
                                  FnAttribute.DoubleAttribute([0, math.pi / 4]), 
                                  'tan')
        
        self.assertAlmostEqual(result[0], 0.0)
        self.assertAlmostEqual(result[1], 1.0)
        
    def testDeferred(self):
        txn = self.runtime.createTransaction()
        
        opSscb = createTestLocation(txn, FnAttribute.DoubleAttribute([5, -3]))
    
        opAttributeOperation1 = addAttributeOperation(txn, 'add', FnAttribute.DoubleAttribute(2), 1)
        txn.setOpInputs(opAttributeOperation1, [opSscb])
        
        opAttributeOperation2 = addAttributeOperation(txn, 'negate', None, 1)
        txn.setOpInputs(opAttributeOperation2, [opAttributeOperation1])
        
        opResolve = txn.createOp()
        txn.setOpArgs(opResolve, 'AttributeOperationResolve', FnAttribute.GroupAttribute())
        txn.setOpInputs(opResolve, [opAttributeOperation2])
        
        client = txn.createClient()
        txn.setClientOp(client, opResolve)
        
        self.runtime.commit([txn])
        
        attrs = client.cookLocation(testLocation).getAttrs()
        
        self.assertEqual(attrs.getChildByName(testAttrName).getNearestSample(0), [-7, 1])
