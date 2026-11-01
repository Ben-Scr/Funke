from __future__ import annotations

import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


POLICY_SCRIPT = Path(__file__).with_name("windows_pe_policy.ps1")


@unittest.skipUnless(os.name == "nt", "Windows PE release policy uses PowerShell")
class WindowsPePolicyTests(unittest.TestCase):
    def check_image(self, *, dll=True, subsystem=2, machine=0x8664, magic=0x20B,
                    flags=0x4160, dll_bit=None, expected_error=None):
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
            def quote(value):
                return "'" + str(value).replace("'", "''") + "'"

            result = subprocess.run(
                ["powershell.exe", "-NoProfile", "-NonInteractive", "-Command",
                 f"$ErrorActionPreference='Stop'; . {quote(POLICY_SCRIPT)}; Assert-NativePePolicy -Path {quote(binary)}"],
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


if __name__ == "__main__":
    unittest.main()
