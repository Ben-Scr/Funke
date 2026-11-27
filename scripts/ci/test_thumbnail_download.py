"""Thumbnail destination-policy checks for the PackageTool boundary."""
import http.server
import pathlib
import subprocess
import tempfile
import threading
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]

class Handler(http.server.BaseHTTPRequestHandler):
    requests = 0
    def log_message(self, *args): pass
    def do_GET(self):
        type(self).requests += 1
        self.send_response(302)
        self.send_header("Location", "http://169.254.169.254/latest/meta-data/")
        self.end_headers()

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
    def reject(self, url):
        with tempfile.TemporaryDirectory() as folder:
            output = pathlib.Path(folder) / "image"
            result = subprocess.run(["dotnet", str(self.tool), "thumbnail-download",
                url, str(output)], capture_output=True, timeout=12)
            self.assertNotEqual(result.returncode, 0, result.stderr.decode())
            self.assertIn("Invalid thumbnail URL.", result.stderr.decode())
            self.assertFalse(output.exists())

    def test_local_redirect_server_is_not_contacted(self):
        before = Handler.requests
        self.reject(f"http://127.0.0.1:{self.server.server_port}/redirect")
        self.assertEqual(Handler.requests, before)

    def test_insecure_github_url_is_rejected(self):
        self.reject("http://raw.githubusercontent.com/Ben-Scr/Funke/main/README.md")

    def test_unapproved_host_is_rejected(self):
        self.reject("https://example.com/thumbnail.png")

    def test_embedded_credentials_are_rejected(self):
        self.reject("https://user:password@raw.githubusercontent.com/thumbnail.png")

    def test_nonstandard_port_is_rejected(self):
        self.reject("https://raw.githubusercontent.com:444/thumbnail.png")

    def test_local_and_private_addresses_are_rejected(self):
        for url in ("https://127.0.0.1/thumbnail.png",
                    "https://10.0.0.1/thumbnail.png",
                    "https://169.254.169.254/thumbnail.png",
                    "https://172.16.0.1/thumbnail.png",
                    "https://192.168.0.1/thumbnail.png",
                    "https://[::1]/thumbnail.png",
                    "https://[fc00::1]/thumbnail.png",
                    "https://[fe80::1]/thumbnail.png",
                    "https://2130706433/thumbnail.png"):
            with self.subTest(url=url): self.reject(url)

if __name__ == "__main__": unittest.main()
