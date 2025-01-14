# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

#-------------------------------------------------------------

import kodachi


#-------------------------------------------------------------
# * NOTES *
#    1) For reference, here are the priorities we have assigned to  
#       implicit resolver stages in Katana:
#
#           0 -  99 : Before Preprocess resolvers
# 
#           100 - 199 : Preprocess resolvers
# 
#           200 - 299 : Before Standard Resolvers
# 
#           300 - 399 : Standard Resolvers
# 
#           400 - 499 : After Standard Resolvers
# 
#           500 - 599 : Postprocess Resolvers
# 
#           600 - 699 : After Post Process Resolvers
#
#    2) Use this script to to only register resolvers with Kodachi,
#       and only for the following stages:
#            1) Before Preprocess resolvers,
#            2) Before Standard Resolvers
#            3) After Standard Resolvers
#            4) After Post Process Resolvers
#
#    3) For each implicit resolver defined by this package, add a new
#       entry to kodachi.ImplicitResolverRegistry using its add() method;
#       kodachi.ImplicitResolverRegistry.add() arguments in order:
#            1) "opType", StringAttribute
#            2) "opArgs", GroupAttribute
#            3) "priority", IntAttribute
#            4) "ignore", StringAttribute
#            5) "addSystemOpArgs", IntAttribute
#
#    4) At the end of the script call kodachi.ImplicitResolverRegistry.writeXML()
#       to write out the XML file.
#
#-------------------------------------------------------------

#-------------
# LocalizeLightMateraials (in KPOPTerminal.cc)
#-------------
kodachi.ImplicitResolverRegistry.add(opType = "LightInputResolve",
                                     opArgs = kodachi.GroupAttribute(),
                                     priority = 203, # Before Standard Resolvers
                                     ignore = "none",
                                     addSystemOpArgs = True)

#-------------
# UDIMPathsResolver
#-------------
kodachi.ImplicitResolverRegistry.add(opType = "UDIMPathsResolver",
                                     opArgs = kodachi.GroupAttribute(),
                                     priority = 204, # Before Standard Resolvers
                                     ignore = "none",
                                     addSystemOpArgs = False)


#-------------------------------------------------------------
# Write opCollection GroupAttribute (built above) to a new XML file
#-------------------------------------------------------------
kodachi.ImplicitResolverRegistry.writeXML()

#-------------------------------------------------------------


