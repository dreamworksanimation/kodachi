# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import os
import sys
import uuid
import time
from datetime import datetime, timedelta
from collections import defaultdict
from Katana import (Callbacks, Utils, Nodes3DAPI, NodegraphAPI,
                    RenderManager, FnAttribute, FnGeolib, enum,
                    ResolutionTable, ScenegraphManager)

from PyQt5 import QtCore
from PyQt5 import QtWidgets

import kodachi
import zmq
import logging
import shutil

logger = logging.getLogger('MoonrayRenderManager')

rendererName = 'moonray'

RenderMethodType = enum.Enum('previewRender', 'liveRender', 'diskRender')

liveUpdateDelta = timedelta(milliseconds=100)

rezContextFile = None


def getRezContextFile():
    """
    We handle the rez context file in the MoonrayRenderManager instead of the
    renderer plugin so that the tmp file can be persisted for the entire Katana
    session, and not copied once per renderboot. Once we are able to send the
    contents of the rez context file instead of a filepath, this will not be necessary
    """
    global rezContextFile
    if rezContextFile is None:
        try:
            srcFile = os.environ.get('REZ_CONTEXT_FILE')
            if not srcFile:
                logger.warn("Cannot get rez context file. 'REZ_CONTEXT_FILE' environment variable not set")
                raise Exception()

            if not os.path.exists(srcFile):
                logger.warn("Cannot get rez context file. File at location '{}' does not exist".format(srcFile))
                raise Exception()

            if not os.path.isabs(srcFile):
                logger.warn("Cannot get rez context file. File path '{}' must be absolute.".format(srcFile))
                raise Exception()

            if ".." in srcFile:
                logger.warn("Cannot get rez context file. File path '{}' must be absolute.".format(srcFile))
                raise Exception()

            # Check if the file exists and is a regular file
            if not os.path.isfile(srcFile):
                logger.warn("Cannot get rez context file. File at location '{}' is not valid.".format(srcFile))
                raise Exception()
            
            fileName = os.path.basename(srcFile)

            arrasTmpDir = os.environ.get('ARRAS_REZ_CONTEXT_DIR')
            if not arrasTmpDir:
                logger.warn("Cannot copy rez context file. 'ARRAS_REZ_CONTEXT_DIR' environment variable not set")
                raise Exception()

            if not os.path.exists(arrasTmpDir):
                os.makedirs(arrasTmpDir)

            dstFile = os.path.join(arrasTmpDir, fileName)

            shutil.copyfile(srcFile, dstFile)
        except Exception as e:
            logger.exception('Unable to copy rez context file to arras tmp dir')
            rezContextFile = ''
        else:
            rezContextFile = dstFile

    return rezContextFile


def cleanupRezContextFile(**kwargs):
    global rezContextFile
    if rezContextFile:
        os.remove(rezContextFile)


class RenderWorker(QtCore.QObject):
    """
    Worker object for communicating with the renderboot process
    """

    failed = QtCore.pyqtSignal(object)
    finished = QtCore.pyqtSignal()
    closed = QtCore.pyqtSignal()
    cancelled = QtCore.pyqtSignal()

    def __init__(self, opTreeId):
        super(RenderWorker, self).__init__()

        self._cancelled = False
        self._socket = None
        self._opTreeId = opTreeId

        self._running = True
        self._mutex = QtCore.QMutex()

        logger.debug('Created new RenderWorker')

    def _isRunning(self):
        try:
            self._mutex.lock()
            return self._running
        finally:
            self._mutex.unlock()

    def _connect(self):
        # pubSocketName = 'ipc://' + os.environ['KATANA_TMPDIR'] + '/moonray_katana_ipc'

        socketAddressHash = kodachi.utils.cppStringHash_u64(self._opTreeId)

        pubSocketName = 'ipc:///tmp/{}'.format(socketAddressHash)
        logger.debug('PYTHON connecting to ZMQ socket {}'.format(pubSocketName))

        currentThread = QtCore.QThread.currentThread()
        logger.debug('PYTHON connecting on Thread: {} ({})'.format(currentThread.objectName(),
                                                            int(currentThread.currentThreadId())))

        # the socket for publishing optree changes
        pubSocket = zmq.Context.instance().socket(zmq.PUB)
        pubSocket.bind(pubSocketName)

        # socket for syncing with the render plugin
        # we need to wait for it to be ready before we publish messages
        # otherwise they will be missed
        syncSocketName = pubSocketName + '_sync'
        syncSocket = zmq.Context.instance().socket(zmq.REP)
        syncSocket.bind(syncSocketName)

        # in case renderboot has a failure or the user cancels the render
        # immediately, we don't want to wait forever for the sync message
        # Check every half second if the render has been cancelled.
        poller = zmq.Poller()
        poller.register(syncSocket, zmq.POLLIN)

        while self._isRunning():
            sockets = dict(poller.poll(400))
            if syncSocket in sockets:
                msg = syncSocket.recv()
                syncSocket.send(b'')

                logger.debug('connected to ZMQ socket {}'.format(pubSocketName))
                return pubSocket

            # give the main event loop some time to process events
            logger.debug('Polling ZMQ Socket {}...'.format(pubSocketName))
            currentThread.msleep(100)
            # QtCore.QCoreApplication.processEvents(QtCore.QEventLoop.ExcludeUserInputEvents)

            if self._cancelled:
                logger.debug('Render cancelled while waiting to establish connection to renderboot')
                return None

    def cancel(self):
        logger.debug('Cancelling render {}...'.format(self._opTreeId))
        self._cancelled = True
        self.cancelled.emit()

    def stop(self):
        self._mutex.lock()
        self._running = False
        self._mutex.unlock()

    def close(self):
        logger.debug('Closing render socket...')
        if self._socket:
            logger.debug('Closing open render socket...')
            self._socket.close()

        self._socket = None

        self.closed.emit()
        logger.debug('Closed render socket')

    @QtCore.pyqtSlot(object)
    def process(self, delta):
        if not delta or self._cancelled:
            self.finished.emit()
            return

        logger.debug('Processing delta...')

        if self._socket is None:
            self._socket = self._connect()

        # the work may have been cancelled before the socket
        # could be connected
        if self._socket:
            logger.debug('Sending delta: {}'.format(delta.getHash()))
            self._socket.send(delta.getBinary())

        logger.debug('Processed delta')
        self.finished.emit()


class Context(object):

    def __init__(self, contextId, index, node, rmt, renderSelected, isMultiContext):
        self._contextId = contextId
        self._index = index
        self._node = node
        self._nodeIsRender = False
        self._rmt = rmt
        self._renderId = kodachi.StringAttribute(str(uuid.uuid4()))
        self._liveAttrSet = False
        self._liveAttrs = {}
        self._liveAttrOp = None
        self._nodeOp = None
        self._client = None
        self._opChain = []
        self._lastLiveUpdateTime = datetime.now()
        self._renderSelected = rmt == RenderMethodType.previewRender and renderSelected
        self._isMultiContext = isMultiContext
        logger.debug('renderSelected = {}'.format(self._renderSelected))
        self.isArrasRender = False

        self.resetGraphState()

        # Render nodes are 2D nodes, which you can't append IRFs to
        # if our node is a render node, apply the IRFs to the input node
        # and append the render node to the end
        if self._node.getType() == 'Render':
            self._nodeIsRender = True
            rt = Nodes3DAPI.GetRuntime()
            txn = rt.createTransaction()
            renderOp = Nodes3DAPI.GetOp(txn, self._node, self._gs, applyTerminalOpDelegates=False)
            rt.commit(txn)
            renderOpArgs = renderOp.getOpArgs()

            self._renderOp = txn.createOp()
            txn.setOpArgs(self._renderOp, renderOpArgs[0], renderOpArgs[1])
            rt.commit(txn)

            inputPort = self._node.getInputPortByIndex(0)

            try:
                logicalPort, _ = self._node.getInputPortAndGraphState(inputPort, self._gs)
            except AttributeError:
                # TODO: This is to support Katana 3.0v4
                # once 3.0v4 is no longer available this should be removed
                logicalPort = self._node.getLogicalInputPort(inputPort, self._gs)[0]

            connectedPort = logicalPort.getConnectedPort(0)
            self._node = connectedPort.getNode()

        self._opTreeBuilder = kodachi.OpTreeBuilder()
        # , map of geolibOpId to [OpTreeBuilder::Op, opType, opArgs, opInputs]
        self._opDict = defaultdict(lambda: [self._opTreeBuilder.createOp(), None, None, None])

        self.syncWithNodegraph()

    def getNodeOpWithIRFs(self, txn):
        # Get the active render filter nodes
        DelegateManager = RenderManager.InteractiveRenderDelegateManager
        renderFiltersDelegate = DelegateManager.GetRenderFiltersDelegate()
        activeNodes = renderFiltersDelegate.getActiveRenderFilterNodes()

        op = None
        if activeNodes:
            # Initialize the interactive render filters context so that we can gather
            # Ops in the right order
            NodegraphAPI.SetEventsEnabled(False)
            renderItem = {'port': self._node.getOutputPortByIndex(0), 'node': self._node}
            blindData = DelegateManager.RenderPreprocessBegin(renderItem)

            # Get the last logical interactive render filter node
            lastActiveNode = activeNodes[-1]
            returnPort = lastActiveNode.getReturnPort("out")
            outputPort = returnPort.getConnectedPorts()[0]

            op = Nodes3DAPI.GetOp(txn, outputPort.getNode(), self._gs,
                                  outputPort.getIndex(),
                                  applyTerminalOpDelegates=False)

            # End the interactive render filters context. This will restore connections
            # between nodes
            DelegateManager.RenderPreprocessEnd(renderItem, blindData)
            NodegraphAPI.SetEventsEnabled(True)
        else:
            op = Nodes3DAPI.GetOp(txn, self._node, self._gs, applyTerminalOpDelegates=False)

        if self._nodeIsRender:
            txn.setOpInputs(self._renderOp, [op])
            op = self._renderOp

        return op

    def updateROI(self, roiAttr):
        opArgs = self._renderSettingsLocalizeOp.getOpArgs()
        gb = kodachi.GroupBuilder()
        gb.update(opArgs[1])
        gb.set('ROI', roiAttr)
        updatedOpArgs = gb.build()

        rt = Nodes3DAPI.GetRuntime()
        txn = rt.createTransaction()
        txn.setOpArgs(self._renderSettingsLocalizeOp, opArgs[0], updatedOpArgs)
        rt.commit(txn)

        opEntry = self._opDict[self._renderSettingsLocalizeOp.getOpId()]
        self._opTreeBuilder.setOpArgs(opEntry[0], opArgs[0], updatedOpArgs)

    def applyImplicitResolvers(self, txn, op, cameraName):
        irOp = Nodes3DAPI.ApplyImplicitResolverOps(txn, op, self._node, self._gs);
        self._renderSettingsLocalizeOp = irOp

        # previewRender only
        if self._renderSelected:
            asg = ScenegraphManager.getActiveScenegraph()

            # assume selections like '/root' and '/root/world/geo' should
            # be treated as no selection. We also want to ignore selections
            # outside of '/root/world'
            selectedLocations = [x for x in asg.getSelectedLocations() if
                                 x.startswith('/root/world') and len(x.split('/')) > 4]
            if selectedLocations:
                # When Katana builds these opArgs, it seems to limit the
                # common prefix to at most 3 levels (/root/world/geo, root/world/lgt, etc)
                commonPrefix = '/'.join(os.path.dirname(os.path.commonprefix(selectedLocations)).split('/')[:4])
                if cameraName.startswith(commonPrefix):
                    selectedLocations.append(cameraName)
                gb = kodachi.GroupBuilder()
                gb.set('isolateFrom', kodachi.StringAttribute(commonPrefix))
                gb.set('isolateLocations', kodachi.StringAttribute(selectedLocations))
                isolateOp = txn.createOp()
                txn.setOpArgs(isolateOp, 'Isolate', gb.build())
                txn.setOpInputs(isolateOp, [irOp])
                irOp = isolateOp

        return irOp

    def applyRenderOps(self, txn, op):
        rsdOp = txn.createOp()
        txn.setOpArgs(rsdOp, 'RenderSettingsDefaults', kodachi.GroupAttribute())
        txn.setOpInputs(rsdOp, [op])

        if RenderManager.GetRenderIDPass():
            # We want this op to be executed by the OpResolve
            # in the middle of KPOPs before primitive attributes are
            # processed
            idPassOp = txn.createOp()
            gb = kodachi.GroupBuilder()
            gb.set('opType', kodachi.StringAttribute('GenerateKatanaId'))
            gb.set('opArgs', kodachi.GroupAttribute())
            gb.set('recursiveEnable', kodachi.IntAttribute(1))

            asb = kodachi.op_args_builder.AttributeSet()
            asb.setLocationPaths(["/root"])
            asb.setAttr("ops.katanaId", gb.build())

            txn.setOpArgs(idPassOp, 'AttributeSet', asb.build())
            txn.setOpInputs(idPassOp, [rsdOp])
            return idPassOp
        else:
            return rsdOp

    def applyTerminalOps(self, txn, op):
        terminalOps = Nodes3DAPI.GetRenderTerminalOps(txn, rendererName, str(self._rmt), self._gs)
        txn.setOpInputs(terminalOps[0], [op])
        return terminalOps[-1]

    def syncOpTree(self, rootOp):
        ops = [rootOp]
        visitedIds = {rootOp.getOpId()}

        while ops:
            op = ops.pop()
            opId = op.getOpId()

            opEntry = self._opDict[opId]
            opArgs = op.getOpArgs()
            if opArgs[0] != opEntry[1] or opArgs[1] != opEntry[2]:
                self._opTreeBuilder.setOpArgs(opEntry[0], opArgs[0], opArgs[1])
                opEntry[1] = opArgs[0]
                opEntry[2] = opArgs[1]

            opInputs = op.getInputs()
            kOpInputs = []
            for input in opInputs:
                inputId = input.getOpId()

                kOpInputs.append(self._opDict[inputId][0])
                if inputId not in visitedIds:
                    visitedIds.add(inputId)
                    ops.append(input)

            if kOpInputs != opEntry[3]:
                self._opTreeBuilder.setOpInputs(opEntry[0], kOpInputs)
                opEntry[3] = kOpInputs

    def getOps(self, start, end):
        ops = []

        op = start

        while op:
            if op.getOpId() == end.getOpId():
                ops.reverse()
                return ops
            ops.append(op)

            inputs = op.getInputs()
            if not inputs:
                raise Exception('op has no inputs')
            op = inputs[0]

        raise Exception('Op {} not in chain'.format(end))

    def buildOpChain(self, rt, txn, renderSettingsAttr):
        opChain = []

        rt = Nodes3DAPI.GetRuntime()
        txn = rt.createTransaction()

        # The resolutions XML won't necessarily be available
        # so convert it from a named to numbered resolution
        resolutionAttr = renderSettingsAttr.getChildByName("resolution")
        if resolutionAttr:
            namedResolution = resolutionAttr.getValue()
            resTableEntry = ResolutionTable.GetResolutionTable().getResolution(namedResolution)
            numberedResolution = '{}x{}'.format(resTableEntry.xres(), resTableEntry.yres())
        else:
            numberedResolution = '512x512'

        resolutionOp = txn.createOp()
        asb = kodachi.op_args_builder.AttributeSet()
        asb.setLocationPaths(["/root"])
        asb.setAttr("renderSettings.resolution", kodachi.StringAttribute(numberedResolution))
        if self._isMultiContext:
            asb.deleteAttr("renderSettings.overscan")
        txn.setOpArgs(resolutionOp, "AttributeSet", asb.build())
        opChain.append(resolutionOp)

        # implicit resolvers
        cameraNameAttr = renderSettingsAttr.getChildByName("cameraName")
        cameraName = cameraNameAttr.getValue() if cameraNameAttr else "/root/world/cam/camera"
        irOp = self.applyImplicitResolvers(txn, opChain[-1], cameraName)
        rt.commit(txn)
        opChain.extend(self.getOps(irOp, opChain[-1]))

        # render ops
        renderOp = self.applyRenderOps(txn, opChain[-1])
        rt.commit(txn)
        opChain.extend(self.getOps(renderOp, opChain[-1]))

        # insert ops for renderID and setting live attributes
        renderIdOp = txn.createOp()
        asb = kodachi.op_args_builder.AttributeSet()
        asb.setLocationPaths(["/root"])
        asb.setAttr("kodachi.renderID", self._renderId)
        txn.setOpArgs(renderIdOp, "AttributeSet", asb.build())
        txn.setOpInputs(renderIdOp, [opChain[-1]])
        opChain.append(renderIdOp)

        if self._rmt is RenderMethodType.liveRender:
            self._liveAttrOp = txn.createOp()
            txn.setOpArgs(self._liveAttrOp, FnGeolib.NO_OP, kodachi.GroupAttribute())
            txn.setOpInputs(self._liveAttrOp, [opChain[-1]])
            opChain.append(self._liveAttrOp)

        # terminal ops
        terminalOp = self.applyTerminalOps(txn, opChain[-1])
        rt.commit(txn)
        opChain.extend(self.getOps(terminalOp, opChain[-1]))

        return opChain

    def updateOpChainSystemArgs(self, txn, systemArgs):
        for op in self._opChain:
            opArgs = op.getOpArgs()
            opArgsAttr = opArgs[1]
            if opArgsAttr:
                systemAttr = opArgsAttr.getChildByName("system")
                if systemAttr:
                    gb = kodachi.GroupBuilder()
                    gb.update(opArgsAttr)
                    gb.set("system", systemArgs)
                    txn.setOpArgs(op, opArgs[0], gb.build())

    def syncWithNodegraph(self):
        # cook root to get the render settings
        rt = Nodes3DAPI.GetRuntime()
        txn = rt.createTransaction()
        if self._client is None:
            self._client = txn.createClient()

        # Assume that implicit resolvers and terminal ops won't change
        # the render settings, so we only need the ops from the nodegraph
        nodeOp = self.getNodeOpWithIRFs(txn)
        txn.setClientOp(self._client, nodeOp)
        rt.commit(txn)

        rootAttrs = self._client.cookLocation('/root').getAttrs()
        if not rootAttrs:
            raise Exception('could not get root attrs for context {}'.format(self._contextId))

        # the current graph state does not reflect the renderSettings
        # values, update with those values
        shutterOpen = 0.0
        shutterClose = 0.0
        maxTimeSamples = 1
        renderSettingsAttr = rootAttrs.getChildByName("renderSettings")
        if renderSettingsAttr:
            useArrasAttr = rootAttrs.getChildByName("moonrayArrasSettings.use arras")
            if useArrasAttr and useArrasAttr.getValue() == 1:
                self.isArrasRender = True

            shutterOpenAttr = renderSettingsAttr.getChildByName("shutterOpen")
            shutterCloseAttr = renderSettingsAttr.getChildByName("shutterClose")
            timeSamplesAttr = renderSettingsAttr.getChildByName("maxTimeSamples")

            if shutterOpenAttr:
                shutterOpen = shutterOpenAttr.getValue()
            if shutterCloseAttr:
                shutterClose = shutterCloseAttr.getValue()
            if timeSamplesAttr:
                maxTimeSamples = timeSamplesAttr.getValue()
        else:
            logger.warn('Could not get renderSettings for context {}'.format(self._contextId))
            logger.warn(rootAttrs.getXML())

        gs = self._gs.edit().setShutterOpen(shutterOpen).setShutterClose(shutterClose).setMaxTimeSamples(
            maxTimeSamples).build()
        if not self._opChain:
            # First time syncing with nodegraph, build the opchain
            self._gs = gs
            self._nodeOp = self.getNodeOpWithIRFs(txn)
            self._opChain = self.buildOpChain(rt, txn, renderSettingsAttr)

            rsl = (op for op in self._opChain if op.getOpArgs()[0] == 'RenderSettingsLocalize')
            try:
                self._renderSettingsLocalizeOp = next(rsl)
            except StopIteration:
                raise Exception('RenderSettingsLocalize not found in opchain')

            txn.setOpInputs(self._opChain[0], [self._nodeOp])
            rt.commit(txn)

            self.syncOpTree(self._opChain[-1])

            systemArgs = gs.getOpSystemArgs()
            self._systemArgsHash = systemArgs.getHash()

            if self._rmt == RenderMethodType.liveRender:
                self._liveAttrOtbOp = self._opDict[self._liveAttrOp.getOpId()][0]
        else:
            if gs.getHash() != self._gs.getHash():
                # graphstate has changed, get the node op with the new graph state
                self._gs = gs
                nodeOp = self.getNodeOpWithIRFs(txn)

                systemArgs = gs.getOpSystemArgs()
                systemArgsHash = systemArgs.getHash()
                if systemArgsHash != self._systemArgsHash:
                    self.updateOpChainSystemArgs(txn, systemArgs)
                    self._systemArgsHash = systemArgsHash

            if nodeOp != self._nodeOp:
                self._nodeOp = nodeOp
                txn.setOpInputs(self._opChain[0], [self._nodeOp])

            rt.commit(txn)
            self.syncOpTree(self._opChain[-1])

    def resetGraphState(self):
        self._gs = NodegraphAPI.GetCurrentGraphState().edit().setDynamicEntry('var:contextID',
                                                                              self._contextId).setDynamicEntry(
            'var:nodeHash', hash(self._node)).build()

    def setLiveAttr(self, locationPath, attrValue):
        if attrValue is None:
            attrValue = kodachi.GroupAttribute()
        logger.debug('setting live attribute for location {} to {}'.format(locationPath, attrValue.getXML()))

        self._liveAttrs[locationPath] = attrValue

        # live attributes are updated very quickly, possibly 10-100s of times per
        # second. Sending each update to renderboot can
        # slow down the renderer, especially in the case of Arras renders,
        # so only send updates after a specified time interval is reached.
        now = datetime.now()
        if now >= (self._lastLiveUpdateTime + liveUpdateDelta):
            attrsBuilder = kodachi.GroupBuilder()
            for location, value in self._liveAttrs.iteritems():
                attrsBuilder.set(location, value)

            attrs = attrsBuilder.build()
            attrsBuilder.set('liveAttrs', attrs)
            self._opTreeBuilder.setOpArgs(self._liveAttrOtbOp, 'MoonrayLiveAttribute', attrsBuilder.build())
            self._liveAttrSet = True
            self._lastUpdateTime = now

            return True
        return False

    def resetLiveAttr(self):
        self._liveAttrs = {}
        if self._liveAttrSet:
            self._opTreeBuilder.setOpArgs(self._liveAttrOtbOp, FnGeolib.NO_OP, kodachi.GroupAttribute())
            self._liveAttrSet = False

    def buildOpTree(self):
        rootOp = self._opDict[self._opChain[-1].getOpId()][0]
        return self._opTreeBuilder.build(rootOp)

    def buildOpTreeDelta(self):
        return self._opTreeBuilder.buildDelta()


class OpTreeSender(QtCore.QObject):
    """
    Responsible for building the Op Trees that will be sent
    to the renderboot process
    """

    cancelled = QtCore.pyqtSignal()
    stopped = QtCore.pyqtSignal()
    finished = QtCore.pyqtSignal()

    process = QtCore.pyqtSignal(object)

    def __init__(self, node, renderMethodType, renderSelected):
        super(OpTreeSender, self).__init__()

        self._node = node
        self._renderMethodType = renderMethodType
        self._contexts = {}
        self._isConnected = False

        gs = NodegraphAPI.GetCurrentGraphState()
        self._currentGraphStateHash = gs.getHash()

        # cook root to get the attrs we need to build the optrees
        rt = Nodes3DAPI.GetRuntime()
        txn = rt.createTransaction()
        client = txn.createClient()
        op = Nodes3DAPI.GetOp(txn, self._node, gs, applyTerminalOpDelegates=False)
        txn.setClientOp(client, op)
        rt.commit(txn)

        rootAttrs = client.cookLocation('/root').getAttrs()
        if not rootAttrs:
            raise Exception('Could not cook root attrs for optree')

        contextIds = []
        contextsAttr = rootAttrs.getChildByName('contexts')
        if contextsAttr:
            for i in xrange(contextsAttr.getNumberOfChildren()):
                contextIds.append(contextsAttr.getChildName(i))
        else:
            # treat a single-context render as multi-context with 1 context
            contextIds = [str(self._renderMethodType)]

        # TODO: Always use the 0th context as active, but we should allow
        # dynamic updating
        self._activeContextId = contextIds[0]

        logger.debug('using active context {}'.format(self._activeContextId))

        isMultiContext = len(contextIds) > 1

        for i in xrange(len(contextIds)):
            self._contexts[contextIds[i]] = Context(contextIds[i],
                                                    i,
                                                    self._node,
                                                    self._renderMethodType,
                                                    renderSelected,
                                                    isMultiContext)

        self._renderWorkerThread = None
        self._renderWorker = None

        # if Katana is shutdown make sure the worker thread
        # is stopped
        if QtWidgets.qApp:
            QtWidgets.qApp.aboutToQuit.connect(self.shutdown)

    @property
    def renderMethodType(self):
        return self._renderMethodType

    def shutdown(self):
        # make sure that the thread exits properly before
        # this object is garbage collected
        logger.debug('Shutting down OpTreeSender...')
        if not self._renderWorker and not self._renderWorkerThread:
            return

        self._renderWorker.stop()

        self._renderWorkerThread.quit()
        if not self._renderWorkerThread.wait(1000):
            logger.warning('Render worker was did not properly finish running')

        logger.debug('Shut down OpTreeSender')

    def cancel(self):
        self.cancelled.emit()

    def initialize(self, opTreeId):
        # Send the initial optree message
        gb = kodachi.GroupBuilder()
        gb.set('type', kodachi.StringAttribute('OPTREE_INITIAL'))

        # In the case of multiple contexts, let the render plugin know which
        # one to get settings from
        gb.set('activeContext', kodachi.StringAttribute(self._activeContextId))

        useArras = False
        for contextId, context in self._contexts.iteritems():
            opTree = context.buildOpTree()
            gb.set('contexts.{}.optree'.format(contextId), opTree)
            gb.set('contexts.{}.index'.format(contextId), kodachi.IntAttribute(context._index))
            useArras = useArras or context.isArrasRender

        self._renderWorkerThread = QtCore.QThread()
        self._renderWorker = RenderWorker(opTreeId)
        self._renderWorker.moveToThread(self._renderWorkerThread)

        self._renderWorker.cancelled.connect(self.shutdown)
        self._renderWorker.cancelled.connect(self.finished)

        if self._renderMethodType is RenderMethodType.previewRender:
            # since we are just doing a preview render we need to
            # close the socket when it's finished sending the op tree
            self._renderWorker.finished.connect(self._renderWorker.close)
            self._renderWorker.finished.connect(self.finished)
            self._renderWorkerThread.setObjectName('previewRenderThread')
        elif self._renderMethodType is RenderMethodType.liveRender:
            self._renderWorkerThread.setObjectName('liveRenderThread')

        self.cancelled.connect(self._renderWorker.cancel)
        self.process.connect(self._renderWorker.process)

        self._renderWorkerThread.start()

        self.process.emit(gb.build())

    def resetGraphState(self):
        gsHash = NodegraphAPI.GetCurrentGraphState().getHash()
        if gsHash != self._currentGraphStateHash:
            self._currentGraphStateHash = gsHash
            for contextId, context in self._contexts.iteritems():
                context.resetGraphState()

    def syncContexts(self):
        for contextId, context in self._contexts.iteritems():
            context.syncWithNodegraph()

    def processUpdates(self):
        self._contexts[self._activeContextId].resetLiveAttr()
        self.syncContexts()

        deltas = []
        for contextId, context in self._contexts.iteritems():
            delta = context.buildOpTreeDelta()
            if delta.getNumberOfChildren() > 0:
                deltas.append((contextId, delta))

        if deltas:
            gb = kodachi.GroupBuilder()
            gb.set('type', kodachi.StringAttribute('OPTREE_DELTA'))
            for contextId, delta in deltas:
                gb.set('optrees.' + contextId, delta)

            attr = gb.build()
            logger.debug('Processed Update Delta: {}'.format(attr.getHash()))
            self.process.emit(attr)

    def sendLiveAttribute(self, locationPath, attrPath, attrValue):
        logger.debug('Sending Live Attribute {}'.format(attrPath))
        context = self._contexts[self._activeContextId]

        if context.setLiveAttr(locationPath, attrValue):
            gb = kodachi.GroupBuilder()
            gb.set('type', kodachi.StringAttribute('OPTREE_DELTA'))
            gb.set('optrees.' + self._activeContextId, context.buildOpTreeDelta())

            attr = gb.build()
            logger.debug('Processed Live Attribute Delta: {}'.format(attr.getHash()))
            self.process.emit(attr)

    def updateROI(self, roiAttr):
        logger.debug('Updating ROI')
        deltas = []
        for contextId, context in self._contexts.iteritems():
            context.updateROI(roiAttr)
            delta = context.buildOpTreeDelta()
            if delta.getNumberOfChildren():
                deltas.append((contextId, delta))

        if deltas:
            gb = kodachi.GroupBuilder()
            gb.set('type', kodachi.StringAttribute('OPTREE_DELTA'))
            for contextId, delta in deltas:
                gb.set('optrees.' + contextId, delta)

            self.process.emit(gb.build())


class MoonrayRenderBase(QtCore.QObject):
    """
    Abstract-base class for a Moonray Render Method type
    """

    finished = QtCore.pyqtSignal(object)

    def __init__(self, opTreeId, catalogItem, selected=False):
        super(MoonrayRenderBase, self).__init__()

        self._catalogItem = catalogItem
        self._renderNode = NodegraphAPI.GetNode(self._catalogItem.getNodeName())
        self._opTreeId = opTreeId
        self._selected = selected

        self._opTreeSender = OpTreeSender(self.renderNode, self.getRenderMethodType(), self._selected)
        self._opTreeSender.finished.connect(self._onRenderFinished)

    @classmethod
    def getRenderMethodType(cls):
        raise NotImplementedError('Render Method Type must be implemented by MoonrayRender sub-class')

    @property
    def renderId(self):
        return self.catalogItem.getCatalogItemID()

    @property
    def catalogItem(self):
        return self._catalogItem

    @catalogItem.setter
    def catalogItem(self, value):
        if value.getCatalogItemID() == self.catalogItem.getCatalogItemID():
            return

        self._catalogItem = value

    @property
    def renderNode(self):
        return self._renderNode

    @property
    def opTreeId(self):
        return self._opTreeId

    def _onRenderFinished(self):
        logger.debug('Render {} finished'.format(self.opTreeId))
        self.finished.emit(self)


class MoonrayLiveRender(MoonrayRenderBase):
    """
    Handles the necessary plumbing for interacting with
    a Live Render with the Moonray Render plugin
    """

    @classmethod
    def getRenderMethodType(cls):
        return RenderMethodType.liveRender

    def _onNodegraphChanged(self, eventID, *args, **kwargs):
        logger.debug('onNodegraphChanged...')
        self._opTreeSender.resetGraphState()
        logger.debug('onNodegraphChanged')

    def _onGeolibProcessingStateChanged(self, eventID, *args, **kwargs):
        logger.debug('onGeolibProcessingStateChanged...')
        
        if kwargs['state']:
            self._opTreeSender.resetGraphState()
            self._opTreeSender.processUpdates()

        logger.debug('onGeolibProcessingStateChanged')

    def _onROIChanged(self, eventID, *args, **kwargs):
        logger.debug('onROIChanged...')

        if RenderManager.GetRenderUseROI():
            rootParameters = NodegraphAPI.GetRootNode().getParameters()
            roiParam = rootParameters.getChild('ROI')
            roiAttr = kodachi.FloatAttribute([roiParam.getChild('left').getValue(0),
                                              roiParam.getChild('bottom').getValue(0),
                                              roiParam.getChild('width').getValue(0),
                                              roiParam.getChild('height').getValue(0)])
        else:
            roiAttr = kodachi.IntAttribute([0, 0, 0, 0])

        self._opTreeSender.updateROI(roiAttr)
        logger.debug('onROIChanged')

    def setLiveAttribute(self, locationPath, attrPath, attrValue):
        logger.debug('setLiveAttribute...')

        self._opTreeSender.sendLiveAttribute(locationPath, attrPath, attrValue)

        logger.debug('setLiveAttribute')

    def _onRenderStopped(self, eventId, *args, **kwargs):
        Utils.EventModule.UnregisterEventHandler(self._onNodegraphChanged, 'nodegraph_changed')
        Utils.EventModule.UnregisterEventHandler(self._onGeolibProcessingStateChanged, 'geolib_processingStateChanged')
        Utils.EventModule.UnregisterEventHandler(self._onRenderStopped, 'renderStopped')
        Utils.EventModule.UnregisterEventHandler(self._onROIChanged, 'renderManager_roiChanged')

    def start(self):
        logger.debug('starting live render...')
        Utils.EventModule.RegisterEventHandler(self._onNodegraphChanged, 'nodegraph_changed')
        Utils.EventModule.RegisterEventHandler(self._onGeolibProcessingStateChanged, 'geolib_processingStateChanged')
        Utils.EventModule.RegisterEventHandler(self._onRenderStopped, 'renderStopped')
        Utils.EventModule.RegisterEventHandler(self._onROIChanged, 'renderManager_roiChanged')

        self._opTreeSender.initialize(self.opTreeId)

        logger.debug('started live render')

    def stop(self):
        logger.debug('Shutting down Live Render {}...'.format(self.opTreeId))
        self._opTreeSender.shutdown()


class MoonrayPreviewRender(MoonrayRenderBase):
    """
    Allows for a preview render with the Moonray Render plugin
    """

    @classmethod
    def getRenderMethodType(cls):
        return RenderMethodType.previewRender

    def start(self):
        self._opTreeSender.initialize(self.opTreeId)

    def stop(self):
        self._opTreeSender.shutdown()


class MoonrayRenderManager(QtCore.QObject):
    """
    Manages the different types of render methods
    that can be executed during an interactive session
    of Katana
    """

    def __init__(self):
        super(MoonrayRenderManager, self).__init__()

        Utils.EventModule.RegisterEventHandler(self._onRenderLogAppend, 'catalog_renderLogAppend')
        Utils.EventModule.RegisterEventHandler(self._onScenegraphMaskEnabled, 'scenegraphMask_enabledSet')
        Utils.EventModule.RegisterEventHandler(self._onRenderCancelled, 'renderCancelled')

        self._liveRender = None
        self._previewRenders = []
        self._renderSelected = False

    @classmethod
    def parseRenderbootMsg(cls, message):
        ret = {}
        args = message.split(' -')
        _, cmd = args[0].rsplit(' ', 1)
        for arg in args[1:]:
            try:
                argument, value = arg.split()
            except ValueError:
                pass
            else:
                ret[argument] = value

        return ret

    def _onScenegraphMaskEnabled(self, eventId, *args, **kwargs):
        self._renderSelected = kwargs['scenegraphMaskEnabled']

    def _onRenderLogAppend(self, eventId, *args, **kwargs):
        try:
            catalogItem = kwargs['item']
        except KeyError:
            return

        try:
            message = kwargs['message']
        except KeyError:
            return

        if 'renderboot' not in message:
            return

        arguments = self.parseRenderbootMsg(message)
        for arg in ('renderer', 'geolib3OpTree', 'renderMethodType'):
            if arg not in arguments:
                logger.warning('Unable to parse required argument {} from renderboot command'.format(arg))
                return

        renderer = arguments['renderer']
        if renderer != 'moonray':
            return

        optreePath = arguments['geolib3OpTree']
        optreeId, _ = os.path.splitext(optreePath)

        renderMethodType = RenderMethodType.get(arguments['renderMethodType'])

        if renderMethodType is RenderMethodType.diskRender:
            logger.debug('MoonrayRenderManager is not available for DiskRenders')
            return
        elif renderMethodType is RenderMethodType.liveRender:
            logger.debug('Starting Live Render')
            self._startLiveRender(optreeId, catalogItem)
        elif renderMethodType is RenderMethodType.previewRender:
            logger.debug('Starting Preview Render')
            self._startPreviewRender(optreeId, catalogItem)
        else:
            logger.warning('Unsupported Render Method Type: {}'.format(renderMethodType))

    def _onRenderCancelled(self, eventId, *args, **kwargs):
        if self._liveRender:
            self._liveRender.stop()

        for previewRender in self._previewRenders:
            previewRender.stop()

    def _startLiveRender(self, opTreeId, catalogItem):
        # if a snapshot of the Live Render is created for the catalog
        # another catalog_renderLogAppend event will be sent for the same
        # Live Render. In this case just update to the new catalog item
        if self._liveRender:
            if self._liveRender.opTreeId == opTreeId:
                logger.debug('Live Render for {} is already running. Updating Catalog Item'.format(opTreeId))
                self._liveRender.catalogItem = catalogItem

                return
            else:
                logger.debug('Stopping Live Render {}'.format(self._liveRender.opTreeId))
                self._liveRender.stop()

        logger.debug('Creating Live Render {}'.format(opTreeId))
        self._liveRender = MoonrayLiveRender(opTreeId, catalogItem, self._renderSelected)
        self._liveRender.start()

    def _startPreviewRender(self, opTreeId, catalogItem):
        previewRender = MoonrayPreviewRender(opTreeId, catalogItem, self._renderSelected)
        previewRender.start()

        self._previewRenders.append(previewRender)

    def setLiveAttribute(self, locationPath, attrPath, attrValue):
        if self._liveRender:
            self._liveRender.setLiveAttribute(locationPath, attrPath, attrValue)


def setupMoonrayRenderManager(**kwargs):
    # Wrap Live Render API functions to provide callback behavior
    liveRenderAPI = sys.modules.get('PyUtilModule.LiveRenderAPI')
    if not liveRenderAPI:
        raise Exception('Could not find PyUtilModule.LiveRenderAPI module')

    setLiveAttributeFunc = liveRenderAPI.SetLiveAttribute

    attrHashes = {}

    moonrayRenderManager = MoonrayRenderManager()

    def isValidUpdate(locationPath, attrPath, attrValue):
        """
        Validates the live attribute function call to reduce the number of
        false positive calls indicating that live attributes are being
        changed when they are not.

        @type locationPath : str
        @type attrPath : str
        @type attrValue : FnAttribute.Attribute
        @param locationPath : The scene graph location when the live attribute
                              is being set.
        @param attrPath : The name of the attribute being set
                          eg. 'xform.interactive'. An empty string indicates
                          that attrValue is a groupAttribute setting top level
                          attributes.
        @param attrValue : The attribute being set.
        """
        # The API function is called frequently, but performs various early out
        # tests so we'll do a few here to get a clearer idea of when the op
        # args are actually changed. The internal code does further checks
        # and throttling on top of these.
        if not RenderManager.RenderGlobals.IsRerendering():
            return False

        # Only update live attributes if we're in continuous update mode
        if Nodes3DAPI.UpdateModes.GetUpdateMode() < Nodes3DAPI.UpdateModes.Continuous:
            return False

        # Only update live attributes on scenegraph locations.
        # This ignores application changes like __VIEWERPATH__
        if not locationPath.startswith('/root'):
            return False

        # Ensure that the attributes hash has actually changed since the last update
        attrHash = None
        if attrValue:
            attrHash = attrValue.getHash()

        attributeKey = "%s|%s" % (locationPath, attrPath)
        cachedAttrHash = attrHashes.get(attributeKey, None)
        if attrHash == cachedAttrHash:
            return False

        if attrHash is None:
            del attrHashes[attributeKey]
        else:
            attrHashes[attributeKey] = attrHash

        return True

    def SetLiveAttributeWrapper(locationPath, attrPath, attrValue):
        """
        A wrapper that performs behavior before and after the call to
        C{LiveRenderAPI.SetLiveAttribute()}.

        @type locationPath : str
        @type attrPath : str
        @type attrValue : FnAttribute.Attribute
        @param locationPath : The scene graph location when the live attribute
                              is being set.
        @param attrPath : The name of the attribute being set
                          eg. 'xform.interactive'. An empty string indicates
                          that attrValue is a groupAttribute setting top level
                          attributes.
        @param attrValue : The attribute being set.
        """
        if not isValidUpdate(locationPath, attrPath, attrValue):
            return

        moonrayRenderManager.setLiveAttribute(locationPath, attrPath, attrValue)

    # Replace the API function with the wrapper function
    liveRenderAPI.SetLiveAttribute = SetLiveAttributeWrapper


Callbacks.addCallback(Callbacks.Type.onStartupComplete, setupMoonrayRenderManager)
Callbacks.addCallback(Callbacks.Type.onShutdown, cleanupRezContextFile)


