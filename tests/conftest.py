# conftest.py — pytest configuration for the pricing engine test suite.
#
# Purpose: make the compiled C++ extension (.so) importable from the
# tests/ directory without installing it.  We just add build/ to sys.path
# before any test file is imported.
#
# How it works:
#   - pytest automatically loads conftest.py before collecting tests.
#   - sys.path.insert(0, ...) puts build/ at the FRONT of the Python
#     module search path, so `import pricing_core` finds the .so file.
#   - The path is constructed relative to this file's location so it
#     works regardless of where pytest is run from.

import sys
import os

# Go one level up from tests/ to reach the project root, then into build/.
BUILD_DIR = os.path.join(os.path.dirname(__file__), "..", "build")
sys.path.insert(0, os.path.abspath(BUILD_DIR))
