from __future__ import annotations

import os
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


POLICY_SCRIPT = Path(__file__).with_name("windows_pe_policy.ps1")
BUILD_SCRIPT = POLICY_SCRIPT.parents[1] / "BuildWindows.ps1"
VERIFY_SCRIPT = POLICY_SCRIPT.with_name("verify_windows_release.ps1")


def quote_powershell(value):
    return "'" + str(value).replace("'", "''") + "'"


@unittest.skipUnless(os.name == "nt", "Windows PE release policy uses PowerShell")
class WindowsPePolicyTests(unittest.TestCase):
    def check_image(self, *, dll=True, subsystem=2, machine=0x8664, magic=0x20B,
                    flags=0x4160, dll_bit=None, executable_subsystem=2, expected_error=None):
        image = bytearray(512)
        image[:2] = b"MZ"
        struct.pack_into("<I", image, 0x3C, 0x80)
        struct.pack_into("<I", image, 0x80, 0x00004550)
        struct.pack_into("<H", image, 0x84, machine)
        is_dll = dll if dll_bit is None else dll_bit
        struct.pack_into("<H", image, 0x96, 0x22 | (0x2000 if is_dll else 0))
        struct.pack_into("<H", image, 0x98, magic)
        struct.pack_into("<HH", image, 0x98 + 68, subsystem, flags)
        with tempfile.TemporaryDirectory() as temporary:
            binary = Path(temporary) / ("Funke Engine's.dll" if dll else "Funke-App.exe")
            binary.write_bytes(image)
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                 f"$ErrorActionPreference='Stop'; . {quote_powershell(POLICY_SCRIPT)}; "
                 f"Assert-NativePePolicy -Path {quote_powershell(binary)} -ExecutableSubsystem {executable_subsystem}"],
                capture_output=True, text=True, timeout=30,
            )
        if expected_error:
            self.assertNotEqual(result.returncode, 0, result.stdout)
            self.assertIn(expected_error, result.stderr)
        else:
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_dll_accepts_both_windows_desktop_subsystems(self):
        for subsystem in (2, 3):
            with self.subTest(subsystem=subsystem):
                self.check_image(subsystem=subsystem)

    def test_gui_executable_is_accepted(self):
        self.check_image(dll=False)

    def test_console_executable_is_rejected(self):
        self.check_image(dll=False, subsystem=3, expected_error="platform/subsystem policy failed")

    def test_cli_console_executable_is_accepted(self):
        self.check_image(dll=False, subsystem=3, executable_subsystem=3)

    def test_cli_requires_the_console_subsystem(self):
        self.check_image(dll=False, executable_subsystem=3, expected_error="platform/subsystem policy failed")

    def test_non_desktop_dll_is_rejected(self):
        self.check_image(subsystem=1, expected_error="platform/subsystem policy failed")

    def test_wrong_architecture_is_rejected(self):
        self.check_image(machine=0xAA64, expected_error="platform/subsystem policy failed")
        self.check_image(magic=0x10B, expected_error="platform/subsystem policy failed")

    def test_dll_characteristic_must_match_the_file_type(self):
        self.check_image(dll=True, dll_bit=False, expected_error="platform/subsystem policy failed")
        self.check_image(dll=False, dll_bit=True, expected_error="platform/subsystem policy failed")

    def test_each_hardening_flag_is_required(self):
        for flag in (0x20, 0x40, 0x100, 0x4000):
            with self.subTest(flag=flag):
                self.check_image(flags=0x4160 & ~flag, expected_error="PE hardening flag")
                self.check_image(dll=False, subsystem=3, executable_subsystem=3,
                                 flags=0x4160 & ~flag, expected_error="PE hardening flag")


@unittest.skipUnless(os.name == "nt", "Windows signing pipeline uses PowerShell")
class WindowsSigningSelectionTests(unittest.TestCase):
    def test_signing_includes_cli_and_managed_files_but_excludes_vendor_files(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            expected = {"funke.exe", "Funke-Engine.dll", "Funke-ScriptCore.dll", "nested/funke.exe"}
            for relative in expected | {"SDL3.dll", "vendor.exe", "funke.exe.bak"}:
                path = root / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"fixture")
            script = f"""
$ErrorActionPreference = 'Stop'
$tokens = $null
$errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile({quote_powershell(BUILD_SCRIPT)}, [ref]$tokens, [ref]$errors)
if ($errors.Count) {{ throw 'BuildWindows parse failed' }}
$signer = $ast.Find({{ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Invoke-AuthenticodeSigning'
}}, $true)
. ([scriptblock]::Create($signer.Extent.Text))
$script:SignedPaths = @()
function Get-WindowsSdkTool {{ return 'Test-SignTool' }}
function Test-SignTool {{ $script:SignedPaths += $args[-1]; $global:LASTEXITCODE = 0 }}
$SigningCertificatePath = ''
$SigningTimestampUrl = ''
$certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2
try {{ Invoke-AuthenticodeSigning -PublishDirectory {quote_powershell(root)} -Certificate $certificate }}
finally {{ $certificate.Dispose() }}
ConvertTo-Json -Compress -InputObject @($script:SignedPaths)
"""
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                capture_output=True, text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            actual = {Path(path).relative_to(root).as_posix() for path in json.loads(result.stdout)}
            self.assertEqual(actual, expected)


@unittest.skipUnless(os.name == "nt", "Windows release verifier uses PowerShell")
class WindowsCliReleaseVerificationTests(unittest.TestCase):
    def verify(self, scenario="valid", expected_error=None):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            metadata = {"schemaVersion": 1, "version": "2026.1.0-beta.1", "commit": "a" * 40,
                        "buildId": "2026.1.0-beta.1+aaaaaaaaaaaa", "dirty": False,
                        "identityScope": "repository"}
            (root / "BuildMetadata.json").write_text(json.dumps(metadata), encoding="utf-8")
            sdk = root / "EngineSDK"
            sdk.mkdir()
            (sdk / "FunkeSDK.json").write_text(json.dumps({"capabilities": {"nativeDevelopment": False}}))
            names = ["Funke-Launcher.exe", "Funke-Editor.exe", "Funke-Runtime.exe", "Funke-Engine.dll"]
            names += [f"EngineSDK/Runtime-{config}/{name}" for config in ("Development", "Dist")
                      for name in ("Funke-Runtime.exe", "Funke-Engine.dll")]
            if scenario != "missing":
                names.append("funke.exe")
            for name in names:
                image = bytearray(512)
                image[:2] = b"MZ"
                struct.pack_into("<I", image, 0x3C, 0x80)
                struct.pack_into("<I", image, 0x80, 0x00004550)
                struct.pack_into("<H", image, 0x84, 0x8664)
                struct.pack_into("<H", image, 0x96, 0x22 | (0x2000 if name.endswith(".dll") else 0))
                struct.pack_into("<H", image, 0x98, 0x20B)
                subsystem = 3 if name == "funke.exe" and scenario != "gui" else 2
                struct.pack_into("<HH", image, 0x98 + 68, subsystem, 0x4160)
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(image)
            script = f"""
$ErrorActionPreference = 'Stop'
$scenario = {quote_powershell(scenario)}
function Get-FixtureVersion([string]$Path) {{
    $cli = [System.IO.Path]::GetFileName($Path) -eq 'funke.exe'
    $version = if ($cli -and $scenario -eq 'version') {{ 'old' }} else {{ '2026.1.0-beta.1' }}
    $buildId = if ($cli -and $scenario -eq 'buildId') {{ 'old' }} else {{ '2026.1.0-beta.1+aaaaaaaaaaaa' }}
    return [pscustomobject]@{{ ProductVersion = $version; SpecialBuild = $buildId }}
}}
function Get-Item {{
    param([string]$LiteralPath)
    return [pscustomobject]@{{ VersionInfo = (Get-FixtureVersion $LiteralPath) }}
}}
function Get-ChildItem {{
    param([string]$LiteralPath, [switch]$Recurse, [switch]$File)
    foreach ($item in Microsoft.PowerShell.Management\\Get-ChildItem -LiteralPath $LiteralPath -Recurse:$Recurse -File:$File) {{
        [pscustomobject]@{{ Name = $item.Name; FullName = $item.FullName; VersionInfo = (Get-FixtureVersion $item.FullName) }}
    }}
}}
function Get-AuthenticodeSignature {{
    param([string]$LiteralPath)
    $status = if ([System.IO.Path]::GetFileName($LiteralPath) -eq 'funke.exe' -and $scenario -eq 'unsigned') {{
        [System.Management.Automation.SignatureStatus]::NotSigned
    }} else {{ [System.Management.Automation.SignatureStatus]::Valid }}
    return [pscustomobject]@{{ Status = $status; StatusMessage = 'fixture' }}
}}
& {quote_powershell(VERIFY_SCRIPT)} -BundleDirectory {quote_powershell(root)} -MetadataPath {quote_powershell(root / "BuildMetadata.json")} -RequireReleaseIdentity -RequireSigned
"""
            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script],
                capture_output=True, text=True, timeout=30,
            )
            if expected_error:
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn(expected_error, result.stderr)
                self.assertIn("funke.exe", result.stderr)
            else:
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_complete_release_passes(self):
        self.verify()

    def test_cli_is_required(self):
        self.verify("missing", "Required native release binary is missing")

    def test_cli_version_must_match(self):
        self.verify("version", "ProductVersion mismatch")

    def test_cli_build_identity_must_match(self):
        self.verify("buildId", "SpecialBuild mismatch")

    def test_cli_signature_is_required(self):
        self.verify("unsigned", "Authenticode verification failed")

    def test_gui_cli_is_rejected(self):
        self.verify("gui", "PE platform/subsystem policy failed")


if __name__ == "__main__":
    unittest.main()
