#!/usr/bin/env python3
"""Write the .indexabi sidecar next to a freshly linked native package library (Linux / macOS).

PackageHost refuses a native package whose sidecar is missing or names a different engine binary.
Windows writes the same file from a PowerShell postbuild (premake/package-loader.lua).

Usage:
    WriteNativeAbi.py <package library> standalone_c
    WriteNativeAbi.py <package library> engine_cpp <engine library>
"""

from __future__ import annotations

import hashlib
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) < 3 or sys.argv[2] not in ("standalone_c", "engine_cpp"):
        print(__doc__, file=sys.stderr)
        return 2
    library = Path(sys.argv[1])
    boundary = sys.argv[2]
    lines = ["schema=1", f"boundary={boundary}"]
    if boundary == "engine_cpp":
        if len(sys.argv) != 4:
            print(__doc__, file=sys.stderr)
            return 2
        engine = Path(sys.argv[3])
        if not engine.is_file():
            print(f"WriteNativeAbi: matching engine library was not built: {engine}", file=sys.stderr)
            return 1
        lines.append("engine_sha256=" + hashlib.sha256(engine.read_bytes()).hexdigest())
    Path(str(library) + ".indexabi").write_text("\n".join(lines) + "\n", encoding="ascii")
    return 0


if __name__ == "__main__":
    sys.exit(main())
