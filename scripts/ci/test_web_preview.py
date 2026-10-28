"""Exercise Web preview startup and HTTP serving without opening a browser."""

from __future__ import annotations

import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.request
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
PREVIEW = REPO_ROOT / "platforms" / "Funke.Platform.Web" / "Builder" / "preview.py"
class WebPreviewTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(prefix="Funke Web preview ")
        self.root = Path(self.temporary.name) / "Grüße 世界"
        self.root.mkdir()
        self.entry = self.root / "index.html"
        self.entry.write_text("<html>Web preview Grüße 世界</html>", encoding="utf-8")
        (self.root / "game.wasm").write_bytes(b"\x00asm\x01\x00\x00\x00")
        self.options = {"creationflags": subprocess.CREATE_NO_WINDOW} if os.name == "nt" else {}
        self.owner = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(120)"],
                                      stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                      stderr=subprocess.DEVNULL, **self.options)
        self.servers = []

    def tearDown(self) -> None:
        if self.owner.poll() is None:
            self.owner.terminate()
        self.owner.wait(timeout=5)
        for server in self.servers:
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.terminate()
                server.wait(timeout=5)
                self.fail("Preview server did not exit when its editor owner exited.")
            if server.stderr is not None:
                server.stderr.close()
        self.temporary.cleanup()

    def run_start(self, entry: Path | None = None, owner_pid: int | None = None,
                  executable: Path | None = None) -> tuple[subprocess.Popen, dict]:
        ready = Path(self.temporary.name) / f"ready-{len(self.servers)}.json"
        process = subprocess.Popen([str(executable or sys.executable), str(PREVIEW), "serve", "--entry", str(entry or self.entry),
                                    "--owner-pid", str(self.owner.pid if owner_pid is None else owner_pid),
                                    "--ready-file", str(ready)], stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.PIPE, text=True, encoding="utf-8", **self.options)
        self.servers.append(process)
        deadline = time.monotonic() + 5
        while not ready.is_file() and process.poll() is None and time.monotonic() < deadline:
            time.sleep(0.05)
        self.assertTrue(ready.is_file(), "Preview did not write a startup result.")
        return process, json.loads(ready.read_text(encoding="utf-8"))

    def start(self, entry: Path | None = None) -> dict:
        process, result = self.run_start(entry)
        self.assertIsNone(process.poll(), result)
        self.assertEqual(set(result), {"url", "pid"})
        self.assertEqual(result["pid"], process.pid)
        self.assertTrue(result["url"].startswith("http://127.0.0.1:"))
        return result

    def get(self, url: str):
        return urllib.request.build_opener(urllib.request.ProxyHandler({})).open(url, timeout=3)

    def test_serves_html_and_wasm_with_preview_headers(self) -> None:
        result = self.start()
        with self.get(result["url"]) as response:
            self.assertEqual(response.read().decode("utf-8"), self.entry.read_text(encoding="utf-8"))
            self.assertEqual(response.headers["Cache-Control"], "no-store")
            self.assertEqual(response.headers["Cross-Origin-Opener-Policy"], "same-origin")
            self.assertEqual(response.headers["Cross-Origin-Embedder-Policy"], "require-corp")
        with self.get(result["url"].replace("index.html", "game.wasm")) as response:
            self.assertEqual(response.headers["Content-Type"], "application/wasm")
            self.assertEqual(response.read(), (self.root / "game.wasm").read_bytes())
        self.entry.write_text("updated build", encoding="utf-8")
        with self.get(result["url"]) as response:
            self.assertEqual(response.read(), b"updated build")

    def test_repeated_starts_choose_independent_free_ports(self) -> None:
        first, second = self.start(), self.start()
        self.assertNotEqual(first["pid"], second["pid"])
        self.assertNotEqual(first["url"], second["url"])
        for result in (first, second):
            with self.get(result["url"]) as response:
                self.assertEqual(response.status, 200)

    def test_entry_filename_is_url_encoded(self) -> None:
        entry = self.root / "Play Grüße 世界.html"
        entry.write_text("named entry", encoding="utf-8")
        result = self.start(entry)
        self.assertIn("Play%20Gr", result["url"])
        with self.get(result["url"]) as response:
            self.assertEqual(response.read(), b"named entry")

    def test_missing_entry_fails_without_success_output(self) -> None:
        process, result = self.run_start(self.root / "missing.html")
        self.assertNotEqual(process.wait(timeout=5), 0)
        self.assertEqual(set(result), {"error"})
        self.assertIn("Web entry point not found", result["error"])
        self.assertIn("Grüße 世界", process.stderr.read())

    def test_exited_owner_fails_without_success_output(self) -> None:
        self.owner.terminate()
        self.owner.wait(timeout=5)
        process, result = self.run_start()
        self.assertNotEqual(process.wait(timeout=5), 0)
        self.assertEqual(set(result), {"error"})
        self.assertIn("editor process", result["error"].lower())

    def test_server_stops_with_owner(self) -> None:
        result = self.start()
        self.owner.terminate()
        self.owner.wait(timeout=5)
        server = self.servers[-1]
        self.assertEqual(server.wait(timeout=5), 0)
        with self.assertRaises(OSError):
            self.get(result["url"])

    @unittest.skipUnless(os.name == "nt", "pythonw is the Windows preview host")
    def test_pythonw_reports_ready_and_startup_errors(self) -> None:
        executable = Path(sys.executable).with_name("pythonw.exe")
        if not executable.is_file():
            self.skipTest("pythonw.exe is not installed")
        process, result = self.run_start(executable=executable)
        self.assertIsNone(process.poll(), result)
        with self.get(result["url"]) as response:
            self.assertEqual(response.status, 200)
        process, result = self.run_start(self.root / "missing.html", executable=executable)
        self.assertNotEqual(process.wait(timeout=5), 0)
        self.assertIn("Web entry point not found", result["error"])


if __name__ == "__main__":
    unittest.main()
