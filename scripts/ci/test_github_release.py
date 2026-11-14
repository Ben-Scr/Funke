"""Release handoff tests; all GitHub calls are simulated."""

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from scripts.ci import prepare_github_release as release


class ReleaseArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.version = "2026.1.0-beta.1"
        self.commit = "a" * 40
        self.environment = mock.patch.dict(os.environ, {"GITHUB_RUN_ID": "42", "GITHUB_RUN_ATTEMPT": "2"})
        self.environment.start()
        self.addCleanup(self.environment.stop)
        for name in release.payload_names(self.version):
            (self.root / name).write_bytes(b"fixture artifact")
        for path in self.root.glob("*.manifest.json"):
            path.write_text(json.dumps({"engine": {"version": self.version, "commit": self.commit,
                                                   "dirty": False, "identityScope": "repository"},
                                        "signed": True, "notarized": True}), encoding="utf-8")
        for path in self.root.glob("*.sha256"):
            binary = path.with_suffix("")
            path.write_text(f"{release.digest(binary)}  {binary.name}\n", encoding="utf-8")
        self.evidence = {"commit": self.commit, "passed": True, "errors": [], "runId": "42", "attempt": "2",
                         "scope": {"platforms": "all", "coreOnly": False}}
        self.write_evidence()

    def write_evidence(self):
        (self.root / "ci-evidence.json").write_text(json.dumps(self.evidence), encoding="utf-8")

    def prepare(self):
        return release.prepare_assets(self.root, self.version, self.commit, "owner/repo")

    def test_complete_assets_replace_candidate_provenance_and_checksums(self):
        name = f"Funke-{self.version}-provenance.json"
        (self.root / name).write_text("old unsigned candidate", encoding="utf-8")
        (self.root / "SHA256SUMS").write_text("stale", encoding="utf-8")
        files = self.prepare()
        provenance = json.loads((self.root / name).read_text(encoding="utf-8"))
        self.assertEqual(provenance["commit"], self.commit)
        self.assertEqual(provenance["ci"]["attempt"], "2")
        self.assertFalse(provenance["publicationReady"])
        for line in (self.root / "SHA256SUMS").read_text().splitlines():
            checksum, filename = line.split("  ")
            self.assertEqual(checksum, release.digest(self.root / filename))
        self.assertEqual(len(files), 18)
        self.assertEqual([p.name for p in files], [p.name for p in self.prepare()])

    def test_private_symbols_are_rejected(self):
        (self.root / "Funke-private-symbols.zip").write_bytes(b"private")
        with self.assertRaisesRegex(ValueError, "inventory"):
            self.prepare()

    def test_missing_checksum_signature_is_rejected(self):
        next(self.root.glob("*.p7s")).unlink()
        with self.assertRaisesRegex(ValueError, "inventory"):
            self.prepare()

    def test_corrupted_installer_is_rejected(self):
        next(self.root.glob("*.exe")).write_bytes(b"corrupt")
        with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
            self.prepare()

    def test_unnotarized_candidate_is_rejected(self):
        path = next(self.root.glob("*.dmg.manifest.json"))
        value = json.loads(path.read_text())
        value["notarized"] = False
        path.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "signed and notarized"):
            self.prepare()

    def test_mismatching_installer_commit_is_rejected(self):
        path = next(self.root.glob("*.exe.manifest.json"))
        value = json.loads(path.read_text())
        value["engine"]["commit"] = "b" * 40
        path.write_text(json.dumps(value), encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "installer identity"):
            self.prepare()

    def test_incomplete_stale_or_failed_ci_is_rejected(self):
        for changes in ({"passed": False}, {"attempt": "1"}, {"commit": "b" * 40},
                        {"scope": {"platforms": "linux", "coreOnly": False}}, {"errors": ["failed"]}):
            with self.subTest(changes=changes):
                original = self.evidence.copy()
                self.evidence.update(changes)
                self.write_evidence()
                with self.assertRaisesRegex(ValueError, "CI evidence"):
                    self.prepare()
                self.evidence = original


class DraftHandoffTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.file = self.root / "installer.exe"
        self.file.write_bytes(b"signed fixture")
        self.notes = self.root / "notes.md"
        self.notes.write_text("Release notes", encoding="utf-8")
        self.commit = "a" * 40
        self.tag = "v2026.1.0-beta.1"
        self.remote_tag = {"type": "commit", "sha": self.commit}
        self.existing = {"id": 123, "tag_name": self.tag, "draft": True, "published_at": None,
                         "assets": [{"name": self.file.name}], "html_url": "https://example.test/draft"}
        self.uploaded = {**self.existing, "assets": [{"name": self.file.name, "state": "uploaded",
                        "size": self.file.stat().st_size, "digest": "sha256:" + release.digest(self.file)}]}
        self.calls = []

    def fake_gh(self, *arguments):
        self.calls.append(arguments)
        if arguments[0] != "api":
            return ""
        endpoint = arguments[1]
        if "/git/ref/" in endpoint or "/git/tags/" in endpoint:
            return json.dumps({"object": self.remote_tag})
        if "?per_page" in endpoint:
            return json.dumps([[self.existing]] if self.existing else [[]])
        if endpoint.endswith("/123"):
            return json.dumps(self.existing)
        if "/releases/tags/" in endpoint:
            return json.dumps(self.uploaded)
        raise AssertionError(endpoint)

    def sync(self):
        with mock.patch.object(release, "gh", side_effect=self.fake_gh):
            return release.sync_draft("owner/repo", self.tag, self.commit, self.notes, [self.file])

    def mutations(self):
        return [args for args in self.calls if args[0] == "release"]

    def test_existing_draft_is_updated_without_publishing(self):
        self.sync()
        self.assertEqual([args[1] for args in self.mutations()], ["upload", "edit"])
        self.assertIn("--draft=true", self.mutations()[-1])
        self.assertIn("--prerelease=true", self.mutations()[-1])

    def test_first_release_is_created_as_draft(self):
        self.existing = None
        self.sync()
        self.assertEqual(self.mutations()[0][1], "create")
        self.assertIn("--draft", self.mutations()[0])
        self.assertIn("--verify-tag", self.mutations()[0])

    def test_published_release_is_never_modified(self):
        self.existing.update(draft=False, published_at="2026-11-14T00:00:00Z")
        with self.assertRaisesRegex(ValueError, "published"):
            self.sync()
        self.assertEqual(self.mutations(), [])

    def test_wrong_remote_tag_is_never_modified(self):
        self.remote_tag["sha"] = "b" * 40
        with self.assertRaisesRegex(ValueError, "verified commit"):
            self.sync()
        self.assertEqual(self.mutations(), [])

    def test_unexpected_existing_assets_require_review(self):
        self.existing["assets"].append({"name": "private-symbols.zip"})
        with self.assertRaisesRegex(ValueError, "unexpected assets"):
            self.sync()
        self.assertEqual(self.mutations(), [])

    def test_upload_digest_failure_prevents_finalizing_notes(self):
        self.uploaded["assets"][0]["digest"] = "sha256:" + "0" * 64
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.sync()
        self.assertEqual([args[1] for args in self.mutations()], ["upload"])


if __name__ == "__main__":
    unittest.main()
