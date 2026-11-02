"""Real local repositories exercise interrupted checkouts without downloading Dawn."""

from pathlib import Path
import subprocess
import tempfile
import unittest

from scripts.dawn import BootstrapDawn as dawn


class DawnBootstrapTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="funke-dawn-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.remote = self.root / "remote"
        self.remote.mkdir()
        dawn.git(self.remote, "init", "--quiet")
        dawn.git(self.remote, "config", "core.autocrlf", "false")
        (self.remote / "CMakeLists.txt").write_text("first\n")
        self.first = self.commit()
        (self.remote / "CMakeLists.txt").write_text("second\n")
        self.second = self.commit()
        self.target = self.root / "checkout"

    def commit(self):
        dawn.git(self.remote, "add", ".")
        dawn.git(self.remote, "-c", "user.name=Ben-Scr", "-c",
                 "user.email=180831368+Ben-Scr@users.noreply.github.com", "commit", "-qm", "fixture")
        return dawn.git(self.remote, "rev-parse", "HEAD").stdout.strip()

    def checkout(self, revision=None, patches=None):
        return dawn.ensure_checkout(self.target, str(self.remote), revision or self.first, patches)

    def test_fresh_repeat_and_pin_change(self):
        self.assertTrue(self.checkout())
        self.assertFalse(self.checkout())
        self.assertTrue(self.checkout(self.second))
        self.assertEqual((self.target / "CMakeLists.txt").read_text(), "second\n")

    def test_resume_init_without_head_or_origin(self):
        self.target.mkdir()
        dawn.git(self.target, "init", "--quiet")
        self.assertTrue(self.checkout())
        self.assertEqual(dawn.git(self.target, "remote", "get-url", "origin").stdout.strip(), str(self.remote))

    def test_missing_remote_on_existing_checkout(self):
        self.checkout()
        dawn.git(self.target, "remote", "remove", "origin")
        self.assertTrue(self.checkout(self.second))

    def test_wrong_origin_is_diagnosed_without_rewriting_it(self):
        self.checkout()
        dawn.git(self.target, "remote", "set-url", "origin", "https://example.invalid/wrong")
        with self.assertRaisesRegex(dawn.BootstrapError, "Unexpected origin"):
            self.checkout()

    def test_git_directory_reference_file(self):
        self.checkout()
        metadata = self.root / "metadata"
        (self.target / ".git").rename(metadata)
        (self.target / ".git").write_text(f"gitdir: {metadata.as_posix()}\n")
        self.assertTrue(self.checkout(self.second))

    def test_failed_fetch_reports_dependency_url_revision_and_git_error(self):
        with self.assertRaises(dawn.BootstrapError) as raised:
            self.checkout("f" * 40)
        message = str(raised.exception)
        for expected in (str(self.target), str(self.remote), "f" * 40, "git fetch", "fatal:"):
            self.assertIn(expected, message)

    def test_untracked_files_survive_pin_update(self):
        self.checkout()
        local = self.target / "notes.txt"
        local.write_text("local")
        self.checkout(self.second)
        self.assertEqual(local.read_text(), "local")

    def test_local_changes_block_update_without_reset(self):
        self.checkout()
        source = self.target / "CMakeLists.txt"
        source.write_text("local edits\n")
        with self.assertRaisesRegex(dawn.BootstrapError, "Unexpected local changes"):
            self.checkout(self.second)
        self.assertEqual(source.read_text(), "local edits\n")

    def test_ignored_local_file_is_not_overwritten_by_new_pin(self):
        (self.remote / ".gitignore").write_text("local.txt\n")
        before = self.commit()
        (self.remote / "local.txt").write_text("upstream\n")
        dawn.git(self.remote, "add", "--force", "local.txt")
        after = self.commit()
        self.checkout(before)
        local = self.target / "local.txt"
        local.write_text("user data\n")
        with self.assertRaisesRegex(dawn.BootstrapError, "would be overwritten"):
            self.checkout(after)
        self.assertEqual(local.read_text(), "user data\n")
        self.assertEqual(dawn.git(self.target, "rev-parse", "HEAD").stdout.strip(), before)

    def test_known_patch_is_idempotent_but_extra_edits_are_protected(self):
        patch = self.root / "change.patch"
        patch.write_text("diff --git a/CMakeLists.txt b/CMakeLists.txt\n--- a/CMakeLists.txt\n"
                         "+++ b/CMakeLists.txt\n@@ -1 +1 @@\n-first\n+patched\n")
        self.checkout(patches=[patch])
        self.assertFalse(self.checkout(patches=[patch]))
        (self.target / "CMakeLists.txt").write_text("patched\nextra\n")
        with self.assertRaisesRegex(dawn.BootstrapError, "Unexpected local changes"):
            self.checkout(patches=[patch])

    def test_non_repository_files_are_not_adopted(self):
        self.target.mkdir()
        (self.target / "CMakeLists.txt").write_text("unknown")
        with self.assertRaisesRegex(dawn.BootstrapError, "no Git metadata"):
            self.checkout()

    def test_pin_update_preserves_known_patch_and_untracked_files(self):
        (self.remote / "untouched.txt").write_text("original\n")
        before = self.commit()
        (self.remote / "CMakeLists.txt").write_text("third\n")
        after = self.commit()
        patch = self.root / "change.patch"
        patch.write_text("diff --git a/untouched.txt b/untouched.txt\n--- a/untouched.txt\n"
                         "+++ b/untouched.txt\n@@ -1 +1 @@\n-original\n+patched\n")
        self.checkout(before, [patch])
        self.assertTrue(self.checkout(after, [patch]))
        self.assertEqual((self.target / "untouched.txt").read_text(), "patched\n")
        self.assertFalse(self.checkout(after, [patch]))

    def test_missing_dependency_source_is_reported_without_erasing_changes(self):
        self.checkout()
        (self.target / "CMakeLists.txt").unlink()
        with self.assertRaisesRegex(dawn.BootstrapError, "Unexpected local changes"):
            self.checkout()
        self.assertFalse((self.target / "CMakeLists.txt").exists())

    def test_manifest_drives_missing_dependency_recovery(self):
        self.target.mkdir()
        (self.target / "DEPS").write_text("vars = {'repo': " + repr(str(self.remote)) + "}\n"
            "deps = {'third_party/abseil': {'url': '{repo}@" + self.first + "'}}\n")
        dawn.fetch_dependencies(self.target, ["third_party/abseil"])
        self.assertTrue((self.target / "third_party/abseil/CMakeLists.txt").is_file())
        with self.assertRaisesRegex(dawn.BootstrapError, "missing from"):
            dawn.fetch_dependencies(self.target, ["missing"])


if __name__ == "__main__":
    unittest.main()
