#!/usr/bin/env python3
"""Start an editor-owned HTTP preview for an exported Web game."""

from __future__ import annotations

import argparse
import ctypes
import functools
import http.server
import json
import os
import sys
import urllib.parse
from pathlib import Path


class OwnerProcess:
    def __init__(self, pid: int) -> None:
        if pid <= 0:
            raise ValueError("The editor process ID must be positive.")
        self.pid = pid
        self.handle = None
        if os.name == "nt":
            from ctypes import wintypes

            self.kernel = ctypes.WinDLL("kernel32", use_last_error=True)
            self.kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
            self.kernel.OpenProcess.restype = wintypes.HANDLE
            self.kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
            self.kernel.WaitForSingleObject.restype = wintypes.DWORD
            self.kernel.CloseHandle.argtypes = [wintypes.HANDLE]
            self.kernel.CloseHandle.restype = wintypes.BOOL
            self.handle = self.kernel.OpenProcess(0x00100000, False, pid)
            if not self.handle:
                raise OSError(f"Cannot watch editor process {pid}: {ctypes.WinError(ctypes.get_last_error())}")

    def is_running(self) -> bool:
        if self.handle is not None:
            return self.kernel.WaitForSingleObject(self.handle, 0) == 0x00000102
        try:
            os.kill(self.pid, 0)
            return True
        except ProcessLookupError:
            return False
        except PermissionError:
            return True

    def close(self) -> None:
        if self.handle is not None:
            self.kernel.CloseHandle(self.handle)
            self.handle = None


class PreviewHandler(http.server.SimpleHTTPRequestHandler):
    extensions_map = {
        **http.server.SimpleHTTPRequestHandler.extensions_map,
        ".wasm": "application/wasm",
        ".js": "text/javascript",
        ".mjs": "text/javascript",
    }

    def end_headers(self) -> None:
        self.send_header("Cache-Control", "no-store")
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        super().end_headers()

    def log_message(self, format: str, *args: object) -> None:
        pass


def _write_ready(path: Path, result: dict) -> None:
    pending = path.with_suffix(".tmp")
    pending.write_text(json.dumps(result), encoding="utf-8")
    pending.replace(path)


def serve(entry: Path, owner_pid: int, ready: Path) -> int:
    owner = None
    try:
        if not entry.is_absolute():
            raise ValueError("The Web entry point must be an absolute path.")
        if not entry.is_file():
            raise FileNotFoundError(f"Web entry point not found: {entry}")
        entry = entry.resolve()
        owner = OwnerProcess(owner_pid)
        if not owner.is_running():
            raise RuntimeError("The editor process has already exited.")
        handler = functools.partial(PreviewHandler, directory=str(entry.parent))
        with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
            server.timeout = 0.25
            url = f"http://127.0.0.1:{server.server_port}/{urllib.parse.quote(entry.name)}"
            _write_ready(ready, {"url": url, "pid": os.getpid()})
            while owner.is_running():
                server.handle_request()
        return 0
    except Exception as error:
        try:
            _write_ready(ready, {"error": str(error)})
        except OSError:
            pass
        if sys.stderr is not None:
            print(f"Could not start Web preview: {error}", file=sys.stderr)
        return 1
    finally:
        if owner is not None:
            owner.close()


def main() -> int:
    if sys.stderr is not None:
        sys.stderr.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    command = commands.add_parser("serve")
    command.add_argument("--entry", type=Path, required=True)
    command.add_argument("--owner-pid", type=int, required=True)
    command.add_argument("--ready-file", type=Path, required=True)
    args = parser.parse_args()
    return serve(args.entry, args.owner_pid, args.ready_file)


if __name__ == "__main__":
    sys.exit(main())
