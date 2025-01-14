# Copyright 2025 DreamWorks Animation LLC
# SPDX-License-Identifier: Apache-2.0

import unittest
import kodachi


class TestHarness(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        print("setupClass")
        cls.runtime = kodachi.createRuntime()

    def setup(self):
        self.txn = self.runtime.createTransaction()
