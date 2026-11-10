"""Run after building PackageTool; loopback-only transfer/archive regression checks."""
import hashlib
import http.server
import io
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import warnings
import zipfile

ROOT = Path(__file__).resolve().parents[2]
TOOL = Path(os.environ.get('FUNKE_PACKAGE_TOOL_TEST', ROOT / 'bin/Release-windows-x86_64/Funke-PackageTool/Funke-PackageTool.dll'))

def archive(entries=None):
    output = io.BytesIO()
    with warnings.catch_warnings(), zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as zipped:
        warnings.simplefilter('ignore', UserWarning)
        for name, value in entries or [('Probe/funke-package.lua', 'return { name = "Probe", version = "1.0.0" }'), ('Probe/data', 'valid')]:
            zipped.writestr(name, value)
    return output.getvalue()

class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = 'HTTP/1.0'
    payload = archive()
    def log_message(self, *args): pass
    def do_GET(self):
        self.send_response(200)
        if self.path == '/declared-limit': self.send_header('Content-Length', str(512 * 1024 * 1024 + 1))
        elif self.path == '/truncated': self.send_header('Content-Length', str(len(self.payload) + 100))
        elif self.path != '/unknown' and self.path != '/stream-limit': self.send_header('Content-Length', str(len(self.payload)))
        self.end_headers()
        try:
            if self.path == '/stall': time.sleep(34)
            if self.path == '/stream-limit':
                for _ in range(513): self.wfile.write(b'x' * (1024 * 1024))
            else: self.wfile.write(self.payload)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError): pass

class PackageTransferTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        cls.server.daemon_threads = True
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown(); cls.server.server_close(); cls.thread.join()
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='funke-package-negative-')
        self.root = Path(self.temp.name)
        self.packages = self.root / 'Packages'; self.packages.mkdir()
        (self.packages / 'Existing').mkdir(); (self.packages / 'Existing/keep').write_bytes(b'old package')
        (self.root / 'project.json').write_bytes(b'{"packages":["Existing"]}')
        (self.root / 'sentinel').write_bytes(b'outside destination')
        Handler.payload = archive()
    def tearDown(self):
        self.assertEqual((self.root / 'sentinel').read_bytes(), b'outside destination')
        self.assertEqual((self.root / 'project.json').read_bytes(), b'{"packages":["Existing"]}')
        self.assertEqual((self.packages / 'Existing/keep').read_bytes(), b'old package')
        self.assertTrue(self.root.resolve().name.startswith('funke-package-negative-'))
        self.temp.cleanup()
    def run_tool(self, *args, success=False, timeout=38):
        result = subprocess.run(['dotnet', str(TOOL), *map(str,args)], capture_output=True, timeout=timeout)
        self.assertEqual(result.returncode == 0, success, result.stderr.decode(errors='replace'))
        if not success: self.assertNotIn(b'"success":true', result.stdout)
        return result
    def transfer(self, route='valid', mode='registry', success=False, digest=None):
        url = f'http://127.0.0.1:{self.server.server_port}/{route}'
        digest = hashlib.sha256(Handler.payload).hexdigest() if digest is None else digest
        if mode == 'registry':
            result = self.run_tool('registry-download', url, digest, self.packages, success=success)
            if not success: self.assertEqual(sorted(p.name for p in self.packages.iterdir()), ['Existing'])
        else:
            output = self.root / 'download'; output.write_bytes(b'old download')
            result = self.run_tool('github-download', url, output, digest, success=success)
            self.assertEqual(output.read_bytes(), Handler.payload if success else b'old download')
            self.assertFalse(list(self.root.glob('download.*.tmp')))
        return result
    def test_registry_valid_unknown_length(self): self.transfer('unknown', success=True)
    def test_github_valid_unknown_length(self): self.transfer('unknown', 'github', True)
    def test_registry_invalid_hash(self): self.transfer(digest='0'*64)
    def test_github_invalid_hash_preserves_file(self): self.transfer(mode='github', digest='0'*64)
    def test_registry_invalid_hash_syntax(self): self.transfer(digest='invalid')
    def test_registry_truncated(self): self.transfer('truncated')
    def test_github_truncated_preserves_file(self): self.transfer('truncated', 'github')
    def test_registry_declared_limit(self): self.transfer('declared-limit')
    def test_github_declared_limit(self): self.transfer('declared-limit', 'github')
    def test_registry_streamed_limit(self): self.transfer('stream-limit')
    def test_github_streamed_limit(self): self.transfer('stream-limit', 'github')
    def test_registry_stall(self): self.transfer('stall')
    def test_github_stall(self): self.transfer('stall', 'github')
    def bad_archive(self, payload):
        Handler.payload = payload
        self.transfer()
        zipped = self.root / 'input.zip'; zipped.write_bytes(payload)
        output = self.root / 'extracted'
        self.run_tool('unzip', zipped, output)
        self.assertFalse(output.exists())
        self.assertFalse(list(self.root.glob('.extract-*.tmp')))
    def test_traversal(self):
        for name in ['../sentinel', 'Probe/../../sentinel', 'Probe\\..\\..\\sentinel', '/absolute', 'C:/absolute', 'Probe/file:stream']:
            with self.subTest(name=name): self.bad_archive(archive([('Probe/funke-package.lua','manifest'),(name,'bad')]))
    def test_duplicate(self): self.bad_archive(archive([('Probe/funke-package.lua','a'),('Probe/funke-package.lua','b')]))
    def test_entry_limit(self): self.bad_archive(archive([(f'Probe/file{i}','') for i in range(10001)]))
    def test_declared_extracted_limit(self):
        data=bytearray(archive()); start=data.index(b'PK\x01\x02')
        struct.pack_into('<I',data,start+24,2*1024*1024*1024+1)
        self.bad_archive(bytes(data))
    def test_actual_size_exceeds_declared_size(self):
        data=bytearray(archive()); start=data.index(b'PK\x01\x02')
        struct.pack_into('<I',data,start+24,1)
        self.bad_archive(bytes(data))
    def test_entry_checksum(self):
        data=bytearray(archive()); start=data.index(b'PK\x01\x02')
        struct.pack_into('<I',data,start+16,0)
        self.bad_archive(bytes(data))
    def test_symlink_entry(self):
        link=zipfile.ZipInfo('Probe/link');link.create_system=3;link.external_attr=0o120777<<16
        self.bad_archive(archive([('Probe/funke-package.lua','manifest'),(link,'../../sentinel')]))
    def test_existing_install(self):
        Handler.payload=archive([('Existing/funke-package.lua','new'),('Existing/keep','replace')]);self.transfer()
    def test_unzip_existing_directory(self):
        zipped=self.root/'input.zip';zipped.write_bytes(archive())
        self.run_tool('unzip',zipped,self.packages)
        self.assertEqual(sorted(p.name for p in self.packages.iterdir()),['Existing'])
    def test_unzip_empty_directory_and_backslashes(self):
        zipped=self.root/'input.zip';zipped.write_bytes(archive([('Probe\\file','valid')]))
        output=self.root/'empty';output.mkdir()
        self.run_tool('unzip',zipped,output,success=True)
        self.assertEqual((output/'Probe/file').read_text(),'valid')
    def test_unzip_destination_junction(self):
        if os.name!='nt': self.skipTest('Windows junction boundary')
        destination=self.root/'linked'; outside=self.root/'outside';outside.mkdir()
        command="New-Item -ItemType Junction -Path '"+str(destination)+"' -Target '"+str(outside)+"' | Out-Null"
        subprocess.run(['powershell','-NoProfile','-Command',command],check=True,capture_output=True)
        zipped=self.root/'input.zip';zipped.write_bytes(archive())
        self.run_tool('unzip',zipped,destination)
        self.assertEqual(list(outside.iterdir()),[])

if __name__ == '__main__': unittest.main()
