#!/usr/bin/env python3
"""Compatibility entry point for the checked minimal Dawn prefetch."""

import sys
from BootstrapDawn import main

if __name__ == "__main__":
    sys.argv.extend(["--minimal", "--deps-only"])
    sys.exit(main())
