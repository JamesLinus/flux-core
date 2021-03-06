#!/usr/bin/env python
import unittest
import errno
import os
import sys
import flux.core as core
import flux
import flux.kvs
import json
from pycotap import TAPTestRunner
from sideflux import run_beside_flux

def __flux_size():
  return 2

class TestTimer(unittest.TestCase):
    def setUp(self):
        """Create a handle, connect to flux"""
        self.f = core.Flux()

    def test_timer_add_negative(self):
        """Add a negative timer"""
        with self.assertRaises(EnvironmentError):
            self.f.timer_watcher_create(-500, lambda x, y:
                                        x.fatal_error('timer should not run'))

    def test_s1_0_timer_add(self):
        """Add a timer"""
        with self.f.timer_watcher_create(10000, lambda x, y, z, w:
                                         x.fatal_error(
                                             'timer should not run')) as tid:
            self.assertIsNotNone(tid)

    def test_s1_1_timer_remove(self):
        """Remove a timer"""
        to = self.f.timer_watcher_create(10000, lambda x, y:
                                         x.fatal_error('timer should not run'))
        to.stop()
        to.destroy()

    def test_timer_with_reactor(self):
        """Register a timer and run the reactor to ensure it can stop it"""
        timer_ran = [False]

        def cb(x, y, z, w):
            timer_ran[0] = True
            x.reactor_stop(self.f.get_reactor())
        with self.f.timer_watcher_create(0.1, cb) as timer:
            self.assertIsNotNone(timer, msg="timeout create")
            ret = self.f.reactor_run(self.f.get_reactor(), 0)
            self.assertEqual(ret, 0, msg="Reactor exit")
            self.assertTrue(timer_ran[0], msg="Timer did not run successfully")


