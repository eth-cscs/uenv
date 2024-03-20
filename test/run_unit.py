#!/usr/bin/env python3

import pathlib
import sys

prefix = pathlib.Path(__file__).parent.resolve()
lib =  prefix.parent / "lib"
sys.path = [lib.as_posix()] + sys.path

import unittest

if __name__ == "__main__":
    loader = unittest.TestLoader()
    # Discover all tests in ./unit
    suite = loader.discover(start_dir='unit')

    # Run the tests
    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)

    sys.exit(not result.wasSuccessful())

