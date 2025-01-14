# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

#-------------------------------------------------------------

import os
import sys
import PyFnGeolib as FnGeolib
import PyFnAttribute as FnAttribute
import Nodes3DAPI
import NodegraphAPI

#-------------------------------------
# Insert ops of type "no-op" to help distinguish between different stages:
#-------------------------------------

Nodes3DAPI.RegisterImplicitResolver(Nodes3DAPI.ImplicitResolverStage.BeforeStandardResolvers, "no-op", FnAttribute.GroupAttribute(), addSystemArgs=False)
Nodes3DAPI.RegisterImplicitResolver(Nodes3DAPI.ImplicitResolverStage.AfterStandardResolvers, "no-op", FnAttribute.GroupAttribute(), addSystemArgs=False)

#-------------------------------------
# Get the implicit resolver op chain from Katana
#-------------------------------------

rt = FnGeolib.GetRegisteredRuntimeInstance()

txn = rt.createTransaction()
dummyOp = txn.createOp()
txn.setOpArgs(dummyOp, FnGeolib.NO_OP, FnAttribute.GroupAttribute())
op = Nodes3DAPI.ApplyImplicitResolverOps(txn, dummyOp, None, NodegraphAPI.GetCurrentGraphState())
rt.commit(txn)

opChain = []

while op:
    opChain.append(op.getOpArgs())
    opInputs = op.getInputs()
    if opInputs:
        op = opInputs[0]
    else:
        break

del dummyOp, op
del opChain[-1]
opChain.reverse()

#-------------------------------------

package_name = 'kodachi'
attr_name = package_name + '_implicit_resolver'

gb = FnAttribute.GroupBuilder()

#-------------------------------------
# NOTE: the very first implicit resolver is an op of type AttibuteSet,
#       that sets "s0.renderSettings.renderThreads" attribute on root 
#       location.
#       Although it appears to be in "Before Preprocess" stage, it does
#       NOT belong to any of the 7 stages we are interested in.
#       For now we are going to add this separately with priority -1, so
#       it comes before the first stage (before "Before Preprocess").
#-------------------------------------

if opChain[0][0] == "AttributeSet":
    opArgs = opChain[0][1]
            
    op_gb = FnAttribute.GroupBuilder()

    #-------------------------
    # Set "priority" to -1

    op_gb.set('priority', FnAttribute.IntAttribute( -1 ))

    #-------------------------
    # Set "opType"

    op_gb.set("opType", FnAttribute.StringAttribute( "AttributeSet" ))

    #-------------------------
    # Add "ignore" attr

    op_gb.set("ignore", FnAttribute.StringAttribute( "katana" ))

    #-------------------------
    # Add "addSystemOpArgs" attr to opArgs

    op_gb.set("addSystemOpArgs", FnAttribute.IntAttribute( int(False) ))

    #-------------------------
    # Set opArgs

    op_gb.set("opArgs", opArgs)

    #-------------------------

    gb.setWithUniqueName(attr_name, op_gb.build())

    #-------------------------
    # Now remove it from the list
    
    del opChain[0]

#-------------------------------------
# Now the opChain list should only contain default implicit
# resolvers at these stages:
#       1) Preprocess, priority 100 - 199
#       2) Standard, priority 300 - 399
#       3) Postprocess, priority 500 - 599
#
# NOTE: For reference, here are the priorities we have assigned to  
#       implicit resolver stages in Katana:
#
#   0 -  99 : Before Preprocess resolvers
# 
# 100 - 199 : Preprocess resolvers
# 
# 200 - 299 : Before Standard Resolvers
# 
# 300 - 399 : Standard Resolvers
# 
# 400 - 499 : After Standard Resolvers
# 
# 500 - 599 : Postprocess Resolvers
# 
# 600 - 699 : After Post Process Resolvers
#-------------------------------------

current_priority = 100

for op in opChain:    
    opType = op[0]
    
    # no-op means end of current stage => priority changes
    if opType == "no-op":
        current_priority += 200
        continue
    else:        
        opArgs = op[1]
                
        op_gb = FnAttribute.GroupBuilder()
        
        #-------------------------
        # Set "priority"
        
        op_gb.set('priority', FnAttribute.IntAttribute(current_priority))
    
        #-------------------------
        # Set "opType"
    
        op_gb.set("opType", FnAttribute.StringAttribute( opType ))
    
        #-------------------------
        # Add "ignore" attr
        
        op_gb.set("ignore", FnAttribute.StringAttribute( "katana" ))
    
        #-------------------------
        # Add "addSystemOpArgs" attr to opArgs
    
        addSystemOpArgs = False
        if opArgs.getChildByName("system"):
            opArgs = FnAttribute.GroupBuilder().update(opArgs).delete("system").build()
            addSystemOpArgs = True
    
        op_gb.set("addSystemOpArgs", FnAttribute.IntAttribute( int(addSystemOpArgs) ))
    
        #-------------------------
        # Set opArgs
    
        op_gb.set("opArgs", opArgs)
    
        #-------------------------
    
        gb.setWithUniqueName(attr_name, op_gb.build())
        
#-------------------------------------
# Register internal kodachi implicit resolvers
#-------------------------------------

def registerDeferredPrune():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(100))
    op_gb.set('opType', 'DeferredPruneResolve')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerDeferredPrune())


def registerCurveReduction():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(400))
    op_gb.set('opType', 'CurveReductionOp')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerCurveReduction())


def registerPrimitivePruneFrustum():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(401))
    op_gb.set('opType', 'PrimitivePruneByFrustumOp')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerPrimitivePruneFrustum())


def registerPrimitivePruneVolume():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(402))
    op_gb.set('opType', 'PrimitivePruneByVolumeOp')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerPrimitivePruneVolume())
    

def registerCurveDensity():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(403))
    op_gb.set('opType', 'CurveDensityOp')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerCurveDensity())


def registerCurveWidth():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(404))
    op_gb.set('opType', 'CurveWidthOp')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerCurveWidth())


def registerUsdOpSchema():
    op_gp = FnAttribute.GroupBuilder()
    op_gb.set('priority', FnAttribute.IntAttribute(405))
    op_gb.set('opType', 'OpSchemaResolve')
    op_gb.set('ignore', 'none')
    op_gb.set('addSystemOpArgs', FnAttribute.IntAttribute(1))
    return op_gb.build()

gb.setWithUniqueName(attr_name, registerUsdOpSchema())


implicitResolverCollection = gb.build()

#-------------------------------------
# Write the resulting GroupAttribute to an XML file on disk
#-------------------------------------

text_file = open("kodachi_implicit_resolvers.xml", "w")
text_file.write(implicitResolverCollection.getXML())
text_file.close()

#-------------------------------------------------------------
