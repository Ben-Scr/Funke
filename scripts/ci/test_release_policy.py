import json
import os
import shutil
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from scripts import release_policy


class ReleasePolicyTests(unittest.TestCase):
    def test_only_the_approved_first_preview_can_omit_signing(self):
        self.assertTrue(release_policy.policy("v2026.1.0-beta.1")["unsignedPreview"])
        self.assertFalse(release_policy.policy("v2026.1.0-beta.1")["requiresSigning"])
        for tag in ("v2026.1.0", "v2026.1.0-rc.1", "v2026.1.0-beta.2", "v2026.1.0-beta.10",
                    "v2026.1.0-beta.1-extra", "v2027.1.0-beta.1", "V2026.1.0-beta.1", "other"):
            with self.subTest(tag=tag):
                self.assertTrue(release_policy.policy(tag)["requiresSigning"])
                self.assertFalse(release_policy.policy(tag)["unsignedPreview"])

    def test_branches_named_like_the_preview_are_not_release_exceptions(self):
        with mock.patch.dict(os.environ, {"GITHUB_REF_TYPE": "branch", "GITHUB_REF_NAME": "v2026.1.0-beta.1"}):
            self.assertEqual(release_policy.current_tag(), "")
            self.assertFalse(release_policy.policy(release_policy.current_tag())["unsignedPreview"])

    def test_missing_tag_identity_fails(self):
        with mock.patch.dict(os.environ, {"GITHUB_REF_TYPE": "tag", "GITHUB_REF_NAME": ""}):
            with self.assertRaises(ValueError):
                release_policy.current_tag()

    def test_cli_outputs_the_same_policy_for_workflow_and_powershell(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "github-output"
            for tag in ("v2026.1.0-beta.1", "v2026.1.0", "v2026.1.0-beta.2"):
                result = subprocess.run([sys.executable, release_policy.__file__, "--tag", tag,
                                         "--github-output", str(output)], capture_output=True, text=True, check=True)
                policy = json.loads(result.stdout)
                self.assertEqual(policy, release_policy.policy(tag))
                lines = output.read_text().splitlines()
                self.assertEqual(lines[-2], f"requires_signing={str(policy['requiresSigning']).lower()}")
                self.assertEqual(lines[-1], f"unsigned_preview={str(policy['unsignedPreview']).lower()}")

    @unittest.skipUnless(sys.platform == "win32", "Windows installer entrypoint")
    def test_windows_installer_still_rejects_unsigned_regular_tags_and_dirty_preview_bundles(self):
        host = shutil.which("pwsh") or shutil.which("powershell")
        self.assertIsNotNone(host)
        script = Path(release_policy.__file__).with_name("BuildInstaller.ps1")
        with tempfile.TemporaryDirectory() as directory:
            bundle = Path(directory)
            for tag, version, dirty, extra, error in (
                ("v2026.1.0", "2026.1.0", False, [], "signing identity is required"),
                ("v2026.1.0-beta.2", "2026.1.0-beta.2", False, [], "signing identity is required"),
                ("v2026.1.0-beta.1", "2026.1.0-beta.1", True, [], "clean release bundle"),
                ("v2026.1.0-beta.1", "2026.1.0-beta.2", False, [], "does not match"),
                ("v2026.1.0-beta.1", "2026.1.0-beta.1", False, ["-RequireSigning"], "signing identity is required"),
            ):
                with self.subTest(tag=tag, dirty=dirty, extra=extra):
                    (bundle / "BuildMetadata.json").write_text(json.dumps({"version": version, "dirty": dirty}))
                    environment = {**os.environ, "GITHUB_REF_TYPE": "tag", "GITHUB_REF_NAME": tag}
                    result = subprocess.run([host, "-NoProfile", "-NonInteractive", "-File", str(script),
                                             "-BundleDirectory", str(bundle), *extra], env=environment,
                                            capture_output=True, text=True, errors="replace", timeout=30)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(error, result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
