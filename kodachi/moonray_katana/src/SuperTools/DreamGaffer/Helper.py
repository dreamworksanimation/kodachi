# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

def getUnusedName(basename, children):
    i = 0
    name = basename
    while (name in children):
        i = i + 1
        name = basename + str(i)
    return name

def stripNodeName(paramName):
    return ".".join(paramName.split(".")[1:])