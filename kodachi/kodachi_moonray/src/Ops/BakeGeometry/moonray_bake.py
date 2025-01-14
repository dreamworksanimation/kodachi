# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

# ----------------------------------------------------------------------

import sys, getopt, logging

inputName = 'scene_file_input'
saveToName = 'saveToFile'
usdRootName = 'usdRootPath'
rootLocationsName = 'rootLocations'
exportSessionLayerName = 'exportSessionLayer'
exportGeometryName = 'exportGeometry'

# Generate the GroupAttribute to send to the kodachi backend
def createArgs(bakeCurves, rdlFile, saveTo, usdRoot, rootLocations, exportSessionLayer):
    import kodachi
    
    # Bake Geometry op tree
    # BakeGeometryRdlOp -> UsdExportBackend
    
    # Bake Curves op tree
    # RDLInOp -> UsdExportBackend
    
    otb = kodachi.OpTreeBuilder()
    op = otb.createOp()
    gb = kodachi.GroupBuilder()
    gb.set(inputName, kodachi.StringAttribute(rdlFile))
    
    if bakeCurves:
        # for curves we utilize RDLIn to read the scene file and 
        # rely on implicit fur resolvers to create curve locations during
        # usd export
        otb.setOpArgs(op, 'RDLInOp', gb.build())
    else:
        # for geometry, BakeGeometryRdl will read the scene file and output 
        # baked geometry locations
        otb.setOpArgs(op, 'BakeGeometryRdlOp', gb.build())
    
    # usd export
    sscb = kodachi.FnGeolibServices.OpArgsBuilders.StaticSceneCreate()
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.backend', kodachi.StringAttribute('USDExportBackend'))
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.{0}'.format(saveToName), kodachi.StringAttribute(saveTo.values()))
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.{0}'.format(usdRootName), kodachi.StringAttribute(usdRoot))
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.{0}'.format(rootLocationsName), kodachi.StringAttribute(rootLocations))
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.{0}'.format(exportSessionLayerName), kodachi.IntAttribute(exportSessionLayer))
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.{0}'.format(exportGeometryName), kodachi.IntAttribute(1))
    
    sscb.setAttrAtLocation('/root', 'kodachi.backendSettings.diffOpTrees.default', otb.build(op))
    
    op = otb.createOp()
    otb.setOpArgs(op, 'StaticSceneCreate', sscb.build())
    
    return otb.build(op)

def moonrayBakeGeometry(inputFile, outputFile, bakeCurves):
    import os
    import kodachi
    from kodachi import (GroupAttribute,
                         FloatAttribute,
                         IntAttribute,
                         StringAttribute,
                         DoubleAttribute,
                         GroupBuilder,
                         OpTreeBuilder)

    logging.basicConfig(format='MoonrayBakeGeometry: %(message)s', level=logging.INFO)

    rt = kodachi.KodachiRuntime.createRuntime()

    rdlFile = inputFile             # rdl input file
    saveTo = {}
    saveTo['default'] = outputFile  # usd output file
    usdRoot = []                    # usd root name to export under
    rootLocations = ['/root']       # root location to bake
    exportSessionLayer = 0          # whether to export usd session layer

    args = createArgs(bakeCurves, rdlFile, saveTo, usdRoot, rootLocations, exportSessionLayer)

    backend = kodachi.BackendClient()
    try:
        backend.initialize(args)
        backend.start()
    except Exception:
        logging.error('Moonray Bake Geometry Failed: {0}'.format(sys.exc_info()[0]))
        return
        
    results = backend.getData(kodachi.GroupAttribute()).getAttr()

    filesWritten = []
    filesNotWritten = []
    for i in xrange(results.getNumberOfChildren()):
        if results.getChildByIndex(i):
            filesWritten.append(saveTo[results.getChildName(i)])
        else:
            filesNotWritten.append(saveTo[results.getChildName(i)])
    
    statusMessage = ''
    if filesWritten:
        statusMessage += 'Saved USD files to:<br/>{0}'.format('<br/>'.join(filesWritten))
    else:
        statusMessage += 'No files were saved'
    if filesNotWritten:
        statusMessage += '<br/></br>Failed to save files:<br/>{0}'.format('<br/>'.join(filesNotWritten))
    logging.info(statusMessage)  

def main(argv):
    import argparse

    descriptionText = """Given a rdl scene file input, bakes all geometry via Moonray and outputs to a designated usd file.
                       Use --fur (-f) to specify baking of fur. Baking of geometry and fur is currently
                       a separate process and cannot be done together."""
    
    parser = argparse.ArgumentParser(description=descriptionText)
    parser.add_argument('-i', '--input', help="The Rdl scene file input")
    parser.add_argument('-o', '--output', help="A Usd target file to output to")
    parser.add_argument('-f', '--fur', action='store_true', help="Bakes fur instead of geometry.")
    
    args = parser.parse_args()

    moonrayBakeGeometry(args.input, args.output, args.fur)
    
if __name__ == "__main__":
   main(sys.argv[1:])

# ----------------------------------------------------------------------
