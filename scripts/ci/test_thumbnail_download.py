"""Bounded thumbnail transfer checks against a local HTTP server."""
import http.server
import pathlib
import subprocess
import tempfile
import threading
import time
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

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
        build = tempfile.TemporaryDirectory(prefix="funke-thumbnail-test-")
        cls.addClassCleanup(build.cleanup)
        build_root = pathlib.Path(build.name)
        result = subprocess.run([
            "dotnet", "build", str(ROOT / "Funke-PackageTool/Funke-PackageTool.csproj"),
            "-c", "Release", "-p:PlatformTarget=AnyCPU", "-p:UseAppHost=false",
            "-o", str(build_root / "bin"),
            f"-p:BaseIntermediateOutputPath={build_root / 'obj'}/",
            f"-p:IntermediateOutputPath={build_root / 'obj'}/Release/",
            "--nologo", "-v:q",
        ], cwd=ROOT, capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise RuntimeError("PackageTool build failed:\n" + result.stdout + result.stderr)
        cls.tool = build_root / "bin/Funke-PackageTool.dll"
        if not cls.tool.is_file():
            raise RuntimeError(f"PackageTool build did not produce {cls.tool}")
        cls.server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join()
    def transfer(self, route, success, error=None):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "image"
            result = subprocess.run(["dotnet", str(self.tool), "thumbnail-download",
                f"http://127.0.0.1:{self.server.server_port}/{route}", str(output)], capture_output=True, timeout=12)
            self.assertEqual(result.returncode == 0, success, result.stderr.decode())
            if error:
                self.assertIn(error, result.stderr.decode())
            if success: self.assertEqual(output.read_bytes(), b"thumbnail fixture")
            else: self.assertFalse(output.exists())
    def test_valid(self): self.transfer("valid", True)
    def test_missing(self): self.transfer("missing", False, "404")
    def test_declared_limit(self): self.transfer("oversize", False, "Thumbnail exceeds 8 MiB.")
    def test_streamed_limit(self): self.transfer("stream", False, "Thumbnail exceeds 8 MiB.")
    def test_timeout(self): self.transfer("slow", False, "Request timed out")

if __name__ == "__main__": unittest.main()
