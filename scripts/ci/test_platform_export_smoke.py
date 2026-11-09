"""Check release-smoke verdicts without treating these unit tests as device validation."""

from __future__ import annotations

import argparse
import contextlib
import functools
import http.server
import io
import json
import tempfile
import threading
import unittest
import urllib.request
from pathlib import Path
from unittest.mock import patch

from scripts.ci import platform_export_smoke as smoke

try:
    from PIL import Image
except ImportError:
    Image = None


class PlatformExportSmokeTests(unittest.TestCase):
    def test_artifact_digest_covers_web_resources_and_is_location_independent(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            first = root / "first"
            second = root / "second"
            for folder in (first, second):
                folder.mkdir()
                (folder / "index.html").write_text("game", encoding="utf-8")
                (folder / "game.wasm").write_bytes(b"wasm fixture")
            args = argparse.Namespace(platform="web", entry=first / "index.html", output=root / "evidence")
            original = smoke.artifact_identity(args)
            args.entry = second / "index.html"
            self.assertEqual(original["sha256"], smoke.artifact_identity(args)["sha256"])
            (second / "game.wasm").write_bytes(b"different build")
            self.assertNotEqual(original["sha256"], smoke.artifact_identity(args)["sha256"])
            args.output = second / "evidence"
            with self.assertRaisesRegex(ValueError, "outside"):
                smoke.artifact_identity(args)

    def test_missing_runtime_tool_records_blocked_without_claiming_build_or_device_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / "game.apk"
            apk.write_bytes(b"dummy compiler output")
            output = root / "evidence"
            with patch.object(smoke, "require_tool", side_effect=smoke.UnavailableToolError("No adb")), \
                    contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(1, smoke.main(["--output", str(output), "--expect-log", "SCRIPT_OK",
                    "--expect-pixel", "0,0,0,0,0", "android", "--apk", str(apk),
                    "--serial", "missing", "--package", "dev.funke.test"]))
            result = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertFalse(result["passed"])
            self.assertEqual("blocked", result["evidenceLevels"]["real_runtime"]["status"])
            for level in ("unit_mock", "real_build", "physical_device"):
                self.assertEqual("unverified", result["evidenceLevels"][level]["status"])
            self.assertEqual("unverified", result["qualification"])
            self.assertIsNone(result["sourceCommit"])

    @unittest.skipIf(Image is None, "Pillow is needed for rendered-pixel assertions")
    def test_changed_artifact_cannot_receive_runtime_pass(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / "game.apk"
            apk.write_bytes(b"initial build")
            output = root / "evidence"

            def changed_export(args, evidence):
                apk.write_bytes(b"replaced build")
                Image.new("RGB", (2, 2), (0, 0, 0)).save(evidence / "game.png")

            with patch.object(smoke, "android", side_effect=changed_export), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(1, smoke.main(["--output", str(output), "--expect-log", "SCRIPT_OK",
                    "--expect-pixel", "0,0,0,0,0", "android", "--apk", str(apk),
                    "--serial", "fixture", "--package", "dev.funke.test"]))
            result = json.loads((output / "result.json").read_text(encoding="utf-8"))
            self.assertFalse(result["passed"])
            self.assertEqual("failed", result["evidenceLevels"]["real_runtime"]["status"])
            self.assertIn("changed", result["error"])

    def test_requires_all_markers_and_rejects_script_failure_even_after_success(self):
        self.assertFalse(smoke.check_log("SCRIPT_OK", ["SCRIPT_OK", "SAVE_OK"]))
        self.assertTrue(smoke.check_log("SCRIPT_OK\nSAVE_OK", ["SCRIPT_OK", "SAVE_OK"]))
        for error in ("[error] GPU validation failed", "Exception in OnStart(): MissingMethodException",
                      "FATAL EXCEPTION: main", "Funke-ScriptCore interop mismatch", "Unhandled exception"):
            with self.subTest(error=error), self.assertRaises(RuntimeError):
                smoke.check_log("SCRIPT_OK\n" + error, ["SCRIPT_OK"])

    def test_arguments_require_explicit_device_and_nonempty_evidence(self):
        common = ["--output", "evidence", "--expect-log", "SCRIPT_OK", "--expect-pixel", "0.5,0.5,255,0,255"]
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            smoke.parse_args(common + ["android", "--apk", "game.apk", "--package", "dev.funke.test"])
        args = smoke.parse_args(common + ["android", "--apk", "game.apk", "--package", "dev.funke.test", "--serial", "test-device"])
        self.assertEqual(args.serial, "test-device")
        self.assertFalse(args.replace)
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            smoke.parse_args(["--output", "evidence", "--expect-log", "", "--expect-pixel", "0,0,0,0,0",
                              "web", "--entry", "index.html"])

    def test_rejects_invalid_application_identifiers_and_pixel_values(self):
        for value in ("a; rm -rf /", "a/b", "missingdot", "com.app --user 0"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                smoke.identifier(value)
        for value in ("1.1,0,0,0,0", "0,0,256,0,0", "nan,0,0,0,0", "0,0,0"):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                smoke.pixel(value)

    def test_android_install_only_replaces_when_explicitly_requested(self):
        with tempfile.TemporaryDirectory() as temporary:
            apk = Path(temporary) / "game.apk"
            apk.write_bytes(b"test")
            for replace in (False, True):
                args = argparse.Namespace(apk=apk, adb="adb", serial="test-device", package="dev.funke.test", replace=replace)
                commands = []

                def run(command, **kwargs):
                    commands.append(command)
                    if "install" in command:
                        raise RuntimeError("stop before installation")
                    return argparse.Namespace(stdout="device" if "get-state" in command else "")

                with self.subTest(replace=replace), patch.object(smoke, "require_tool", return_value="adb"), \
                        patch.object(smoke, "run", side_effect=run), self.assertRaisesRegex(RuntimeError, "stop before installation"):
                    smoke.android(args, Path(temporary))
                self.assertEqual(commands[-1], ["adb", "-s", "test-device", "install"]
                                 + (["-r"] if replace else []) + [str(apk.resolve())])

    @unittest.skipIf(Image is None, "Pillow is needed for rendered-pixel assertions")
    def test_render_assertions_reject_blank_or_wrong_images(self):
        with tempfile.TemporaryDirectory() as temporary:
            screenshot = Path(temporary) / "game.png"
            image = Image.new("RGB", (10, 10), (0, 0, 0))
            image.putpixel((9, 9), (255, 0, 255))
            image.save(screenshot)
            smoke.check_pixels(screenshot, [(1, 1, 255, 0, 255)], 0)
            with self.assertRaises(RuntimeError):
                smoke.check_pixels(screenshot, [(0.5, 0.5, 255, 0, 255)], 12)
            Image.new("RGB", (1, 1)).save(screenshot)
            with self.assertRaises(RuntimeError):
                smoke.check_pixels(screenshot, [(0, 0, 0, 0, 0)], 0)

    def test_browser_server_uses_wasm_mime_and_isolated_local_origin(self):
        with tempfile.TemporaryDirectory() as temporary:
            (Path(temporary) / "game.wasm").write_bytes(b"\x00asm\x01\x00\x00\x00")
            handler = functools.partial(smoke.WebHandler, directory=temporary)
            with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                try:
                    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
                    with opener.open(f"http://127.0.0.1:{server.server_port}/game.wasm", timeout=5) as response:
                        self.assertEqual(response.headers["Content-Type"], "application/wasm")
                        self.assertEqual(response.headers["Cross-Origin-Opener-Policy"], "same-origin")
                        self.assertEqual(response.headers["Cross-Origin-Embedder-Policy"], "require-corp")
                finally:
                    server.shutdown()
                    thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main()
