"""Bounded thumbnail transfer checks against a local HTTP server."""
import http.server
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
TOOL = ROOT / "bin/Release-windows-x86_64/Funke-PackageTool/Funke-PackageTool.dll"

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args): pass
    def do_GET(self):
        if self.path == "/missing":
            self.send_error(404)
            return
        self.send_response(200)
        if self.path == "/oversize": self.send_header("Content-Length", str(9 * 1024 * 1024))
        self.end_headers()
        try:
            if self.path == "/slow": time.sleep(10)
            if self.path == "/stream":
                for _ in range(145): self.wfile.write(b"x" * 65536)
            else: self.wfile.write(b"thumbnail fixture")
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

class ThumbnailDownloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
    def transfer(self, route, success):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "image"
            result = subprocess.run(["dotnet", str(TOOL), "thumbnail-download",
                f"http://127.0.0.1:{self.server.server_port}/{route}", str(output)], capture_output=True, timeout=12)
            self.assertEqual(result.returncode == 0, success, result.stderr.decode())
            if success: self.assertEqual(output.read_bytes(), b"thumbnail fixture")
            else: self.assertFalse(output.exists())
    def test_valid(self): self.transfer("valid", True)
    def test_missing(self): self.transfer("missing", False)
    def test_declared_limit(self): self.transfer("oversize", False)
    def test_streamed_limit(self): self.transfer("stream", False)
    def test_timeout(self): self.transfer("slow", False)

if __name__ == "__main__": unittest.main()
