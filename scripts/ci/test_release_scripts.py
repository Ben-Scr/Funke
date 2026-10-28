from __future__ import annotations

import base64
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from io import StringIO
from pathlib import Path
from unittest import mock


SCRIPT_PATH = Path(__file__).resolve().parents[1] / "generate_build_metadata.py"
SPEC = importlib.util.spec_from_file_location("generate_build_metadata", SCRIPT_PATH)
assert SPEC and SPEC.loader
metadata_script = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(metadata_script)

SMOKE_SCRIPT_PATH = Path(__file__).resolve().parent / "runtime_smoke_test.py"
SMOKE_SPEC = importlib.util.spec_from_file_location("runtime_smoke_test", SMOKE_SCRIPT_PATH)
assert SMOKE_SPEC and SMOKE_SPEC.loader
runtime_smoke_test = importlib.util.module_from_spec(SMOKE_SPEC)
SMOKE_SPEC.loader.exec_module(runtime_smoke_test)
BUILD_WINDOWS_SCRIPT = Path(__file__).resolve().parents[1] / "BuildWindows.ps1"
VERIFY_RELEASE_SCRIPT = Path(__file__).resolve().parents[1] / "VerifyRelease.ps1"
REPO_ROOT = Path(__file__).resolve().parents[2]
ROOT_PREMAKE = REPO_ROOT / "premake5.lua"
ENGINE_PREMAKE = REPO_ROOT / "Funke-Engine/premake5.lua"
ENGINE_README = REPO_ROOT / "Funke-Engine/README.md"
SETUP_SCRIPT = REPO_ROOT / "scripts/Setup.py"
STORAGE_HEADER = REPO_ROOT / "Funke-Engine/src/Serialization/SceneSerializerStorage.hpp"
STORAGE_FALLBACK = REPO_ROOT / "Funke-Engine/src/Serialization/SceneSerializerStorageCore.cpp"
FULL_SERIALIZER = REPO_ROOT / "Funke-Engine/src/Serialization/SceneSerializer.cpp"
DATA_ASSET_MANAGER = REPO_ROOT / "Funke-Engine/src/Scripting/DataAssetManager.cpp"
SCRIPT_SYSTEM = REPO_ROOT / "Funke-Engine/src/Scripting/ScriptSystem.cpp"
SCRIPT_ENGINE_HEADER = REPO_ROOT / "Funke-Engine/src/Scripting/ScriptEngine.hpp"
SCRIPT_ENGINE_SOURCE = REPO_ROOT / "Funke-Engine/src/Scripting/ScriptEngine.cpp"
FILE_WATCHER_SOURCE = REPO_ROOT / "Funke-Engine/src/Serialization/FileWatcher.cpp"


class EditorScriptToolingSourceTests(unittest.TestCase):
    def test_dist_editor_tooling_uses_the_runtime_host_gate(self) -> None:
        source = SCRIPT_SYSTEM.read_text(encoding="utf-8")

        self.assertNotIn("kEnableHotReload", source)
        self.assertNotIn("#if FUNKE_WITH_EDITOR", source)
        self.assertIn(
            "if (Application::IsEditor() && std::filesystem::exists(csproj))",
            source,
        )
        self.assertIn(
            "if (Application::IsEditor() && std::filesystem::exists(sandboxCsproj))",
            source,
        )
        self.assertEqual(
            source.count(
                "if (Application::IsEditor() && !m_NativeProjectDirectory.empty())"
            ),
            2,
        )

    def test_managed_reload_targets_the_just_built_configuration(self) -> None:
        source = SCRIPT_SYSTEM.read_text(encoding="utf-8")

        self.assertIn(
            "m_ManagedBuildOutputPath = project->GetUserAssemblyOutputPath(buildConfig);",
            source,
        )
        self.assertIn(
            "const std::filesystem::path outputPath = "
            "Path::FromUtf8(m_ManagedBuildOutputPath);",
            source,
        )
        self.assertNotIn("ScriptEngine::ReloadAssemblies();", source)

    def test_managed_assembly_load_failure_is_observable(self) -> None:
        header = SCRIPT_ENGINE_HEADER.read_text(encoding="utf-8")
        engine = SCRIPT_ENGINE_SOURCE.read_text(encoding="utf-8")
        system = SCRIPT_SYSTEM.read_text(encoding="utf-8")

        self.assertIn("static bool LoadUserAssembly(const std::string& path);", header)
        self.assertIn("bool ScriptEngine::LoadUserAssembly(const std::string& path)", engine)
        self.assertIn("return s_HasUserAssembly;", engine)
        self.assertIn("if (ScriptEngine::LoadUserAssembly(m_UserAssemblyPath))", system)
        self.assertIn(
            "C# build succeeded, but the rebuilt assembly could not be loaded.",
            system,
        )

    def test_missing_managed_project_blocks_play_rebuild(self) -> None:
        source = SCRIPT_SYSTEM.read_text(encoding="utf-8")

        self.assertIn(
            "Cannot rebuild C# scripts: managed project path is not configured.",
            source,
        )
        self.assertRegex(
            source,
            r"RebuildAndReloadScripts\(\);\s+if \(m_LastRebuildFailed\) \{\s+return false;",
        )

    def test_failed_dotnet_output_is_not_trace_only(self) -> None:
        source = SCRIPT_SYSTEM.read_text(encoding="utf-8")
        start = source.index("if (TryTakeTaskResult(m_RebuildTask, result))")
        end = source.index("if (TryTakeTaskResult(m_NativeRebuildTask, result))", start)
        managed_completion = source[start:end]
        self.assertRegex(
            managed_completion,
            r'FNK_ERROR_TAG\("ScriptSystem", "(?:[^"\\]|\\.)*", result\.Output\);',
        )
        self.assertNotRegex(
            managed_completion,
            r'FNK_TRACE_TAG\("ScriptSystem", "(?:[^"\\]|\\.)*", result\.Output\);',
        )

    def test_file_watcher_decodes_utf8_targets(self) -> None:
        source = FILE_WATCHER_SOURCE.read_text(encoding="utf-8")

        self.assertIn("const std::filesystem::path path = Path::FromUtf8(pathString);", source)
        self.assertNotIn("const std::filesystem::path path(pathString);", source)


class DevelopmentRunnerTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32", "Windows window capture")
    def test_window_capture_compiles_in_available_powershell_hosts(self) -> None:
        source = (REPO_ROOT / "scripts/dev/Run.ps1").read_text(encoding="utf-8")
        start = source.index("Add-Type -AssemblyName System.Drawing")
        end = source.index("[void][FunkeAgent.WindowShot]::SetProcessDPIAware()", start)
        probe = "$ErrorActionPreference = 'Stop'\n" + source[start:end]
        probe += "\nif (-not ('FunkeAgent.WindowShot' -as [type])) { throw 'Capture helper missing' }\n"
        encoded = base64.b64encode(probe.encode("utf-16le")).decode("ascii")
        hosts = [path for name in ("powershell.exe", "pwsh.exe") if (path := shutil.which(name))]
        self.assertTrue(hosts, "No PowerShell host found")
        for host in hosts:
            with self.subTest(host=host):
                result = subprocess.run(
                    [host, "-NoProfile", "-NonInteractive", "-EncodedCommand", encoded],
                    capture_output=True, text=True, timeout=60,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class BuildMetadataTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "Funke-Engine" / "src" / "Core").mkdir(parents=True)
        (self.root / "Version.props").write_text(
            "<Project><PropertyGroup><FunkeVersion>2026.1.0-rc.1</FunkeVersion>"
            "<Version>$(FunkeVersion)</Version>"
            "<AssemblyVersion>2026.1.0.0</AssemblyVersion>"
            "<FileVersion>2026.1.0.0</FileVersion>"
            "<Product>Funke</Product>"
            "</PropertyGroup></Project>\n",
            encoding="utf-8",
        )
        (self.root / "CHANGELOG.md").write_text(
            "# Changelog\n\n## [2026.1.0-rc.1]\n\n- Candidate.\n",
            encoding="utf-8",
        )
        (self.root / ".gitignore").write_text(
            "build/\n*.generated.hpp\n.*.generated.hpp.*\n*.vcxproj\n",
            encoding="utf-8",
        )
        for filename in (
            "Funke-Engine/src/Core/Example.cpp",
            "Funke-Engine/src/Core/Example.hpp",
            "Funke-Engine/premake5.lua",
            "Funke-Editor/src/Example.cpp",
            "Funke-Engine/README.md",
            "Funke-Runtime/FunkeAssets/example.txt",
            "External/example/header.hpp",
            "premake/patches/vendor/example.patch",
        ):
            path = self.root / filename
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("original\n", encoding="utf-8")
        self._git("init")
        self._git("config", "core.autocrlf", "false")
        self._git("config", "user.email", "ci@example.invalid")
        self._git("config", "user.name", "CI")
        self._git("add", ".")
        self._git("commit", "-m", "fixture")
        self._git("tag", "v2026.1.0-rc.1")

        self.original_paths = {
            name: getattr(metadata_script, name)
            for name in ("REPO_ROOT", "VERSION_PROPS", "NATIVE_HEADER", "BUILD_IDENTITY_HEADER", "ENGINE_ABI_HEADER", "ENGINE_SOURCE_ROOT", "MANAGED_PROPS", "MANAGED_RC", "METADATA_JSON", "CHANGELOG")
        }
        metadata_script.REPO_ROOT = self.root
        metadata_script.VERSION_PROPS = self.root / "Version.props"
        metadata_script.NATIVE_HEADER = self.root / "Funke-Engine/src/Core/BuildMetadata.generated.hpp"
        metadata_script.BUILD_IDENTITY_HEADER = self.root / "Funke-Engine/src/Core/BuildIdentity.generated.hpp"
        metadata_script.ENGINE_ABI_HEADER = self.root / "Funke-Engine/src/Core/EngineAbi.generated.hpp"
        metadata_script.ENGINE_SOURCE_ROOT = self.root / "Funke-Engine/src"
        metadata_script.MANAGED_PROPS = self.root / "build/BuildMetadata.props"
        metadata_script.MANAGED_RC = self.root / "build/ManagedVersion.rc"
        metadata_script.METADATA_JSON = self.root / "build/BuildMetadata.json"
        metadata_script.CHANGELOG = self.root / "CHANGELOG.md"

    def tearDown(self) -> None:
        for name, value in self.original_paths.items():
            setattr(metadata_script, name, value)
        self.temporary.cleanup()

    def _git(self, *arguments: str) -> None:
        subprocess.run(["git", *arguments], cwd=self.root, check=True, capture_output=True)

    def _run(self, *arguments: str, environment: dict[str, str] | None = None) -> int:
        environment = {"CI": "", "GITHUB_ACTIONS": "", "TF_BUILD": "", **(environment or {})}
        with (
            mock.patch.object(sys, "argv", [str(SCRIPT_PATH), *arguments]),
            mock.patch.dict(os.environ, environment, clear=False),
            redirect_stdout(StringIO()),
            redirect_stderr(StringIO()),
        ):
            return metadata_script.main()

    def _metadata(self) -> dict:
        return json.loads(metadata_script.METADATA_JSON.read_text(encoding="utf-8"))

    def test_matching_prerelease_tag_generates_consistent_identity(self) -> None:
        self.assertEqual(self._run("--tag", "v2026.1.0-rc.1", "--require-clean"), 0)
        metadata = json.loads(metadata_script.METADATA_JSON.read_text(encoding="utf-8"))
        self.assertEqual(metadata["version"], "2026.1.0-rc.1")
        self.assertRegex(metadata["buildId"], r"^2026\.1\.0-rc\.1\+[0-9a-f]{12}$")
        self.assertEqual(metadata["archiveName"], "Funke-Editor-2026.1.0-rc.1-Windows-x64.zip")
        self.assertFalse(metadata["dirty"])
        managed_props = metadata_script.MANAGED_PROPS.read_text(encoding="utf-8")
        self.assertIn(
            "<IncludeSourceRevisionInInformationalVersion>false</IncludeSourceRevisionInInformationalVersion>",
            managed_props,
        )

    def test_final_tag_does_not_match_prerelease_version(self) -> None:
        self.assertEqual(self._run("--tag", "v2026.1.0"), 1)

    def test_release_gate_rejects_dirty_checkout(self) -> None:
        (self.root / "dirty.txt").write_text("dirty\n", encoding="utf-8")
        self.assertEqual(self._run("--tag", "v2026.1.0-rc.1", "--require-clean"), 1)

    def test_managed_version_cannot_drift_from_product_version(self) -> None:
        version_props = (self.root / "Version.props").read_text(encoding="utf-8")
        (self.root / "Version.props").write_text(
            version_props.replace("2026.1.0.0", "2025.9.0.0", 1),
            encoding="utf-8",
        )
        self.assertEqual(self._run(), 1)

    def test_release_tag_must_point_to_the_build_commit(self) -> None:
        (self.root / "later.txt").write_text("later\n", encoding="utf-8")
        self._git("add", "later.txt")
        self._git("commit", "-m", "later")
        self.assertEqual(self._run("--tag", "v2026.1.0-rc.1", "--require-clean"), 1)

    def test_patch_managed_submodules_do_not_dirty_release_metadata(self) -> None:
        status = (
            " m External/ImGuizmo\n"
            " m External/entt\n"
            " m External/imgui"
        )
        with mock.patch.object(metadata_script, "run_git", return_value=status):
            self.assertEqual(metadata_script.repository_status(), "")

    def test_git_status_keeps_the_leading_porcelain_column(self) -> None:
        with (self.root / "Version.props").open("a", encoding="utf-8") as stream:
            stream.write("\n")
        status = metadata_script.run_git("status", "--porcelain=v1")
        self.assertTrue(status.startswith(" M Version.props"), status)

    def test_engine_identity_ignores_editor_documentation_and_asset_edits(self) -> None:
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        identity = metadata_script.BUILD_IDENTITY_HEADER.read_bytes()
        for filename in (
            "Funke-Editor/src/Example.cpp",
            "Funke-Engine/README.md",
            "Funke-Runtime/FunkeAssets/example.txt",
            "new-notes.txt",
        ):
            with self.subTest(filename=filename):
                (self.root / filename).write_text("changed\n", encoding="utf-8")
                self.assertEqual(self._run("--identity-scope", "engine"), 0)
                self.assertEqual(metadata_script.BUILD_IDENTITY_HEADER.read_bytes(), identity)
                self.assertEqual(self._metadata()["identityScope"], "engine")
                self.assertIn(".dev.engine", self._metadata()["buildId"])

    def test_engine_identity_follows_source_vendor_and_build_input_edits(self) -> None:
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        previous = self._metadata()["buildId"]
        for filename in (
            "Funke-Engine/src/Core/Example.cpp",
            "External/example/header.hpp",
            "Funke-Engine/premake5.lua",
            "premake/patches/vendor/example.patch",
            "Funke-Engine/src/Core/New.cpp",
        ):
            for content in ("first edit\n", "second edit\n"):
                with self.subTest(filename=filename, content=content):
                    (self.root / filename).write_text(content, encoding="utf-8")
                    self.assertEqual(self._run("--identity-scope", "engine"), 0)
                    current = self._metadata()["buildId"]
                    self.assertNotEqual(current, previous)
                    self.assertTrue(self._metadata()["dirty"])
                    previous = current

    def test_engine_identity_survives_unrelated_commits(self) -> None:
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        original = self._metadata()
        identity = metadata_script.BUILD_IDENTITY_HEADER.read_bytes()
        (self.root / "Funke-Engine/README.md").write_text("documentation\n", encoding="utf-8")
        self._git("add", "Funke-Engine/README.md")
        self._git("commit", "-m", "documentation only")
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        self.assertEqual(metadata_script.BUILD_IDENTITY_HEADER.read_bytes(), identity)
        self.assertEqual(self._metadata()["commit"], original["commit"])
        self.assertNotEqual(self._metadata()["repositoryCommit"], original["repositoryCommit"])
        (self.root / "Funke-Engine/src/Core/Example.cpp").write_text("engine\n", encoding="utf-8")
        self._git("add", "Funke-Engine/src/Core/Example.cpp")
        self._git("commit", "-m", "engine change")
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        self.assertNotEqual(self._metadata()["buildId"], original["buildId"])
        self.assertEqual(self._metadata()["commit"], self._metadata()["repositoryCommit"])

    def test_ignored_outputs_do_not_change_engine_identity(self) -> None:
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        original = self._metadata()["buildId"]
        (self.root / "Funke-Engine/src/Core/Other.generated.hpp").write_text("generated\n", encoding="utf-8")
        (self.root / "Funke-Engine/Funke-Engine.vcxproj").write_text("generated\n", encoding="utf-8")
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        self.assertEqual(self._metadata()["buildId"], original)

    def test_engine_scope_preserves_header_abi_tracking(self) -> None:
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        original_abi = metadata_script.ENGINE_ABI_HEADER.read_bytes()
        (self.root / "Funke-Engine/src/Core/Example.hpp").write_text("changed layout\n", encoding="utf-8")
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        self.assertNotEqual(metadata_script.ENGINE_ABI_HEADER.read_bytes(), original_abi)

    def test_unchanged_generation_preserves_output_timestamps(self) -> None:
        for scope in ("repository", "engine"):
            with self.subTest(scope=scope):
                self.assertEqual(self._run("--identity-scope", scope), 0)
                paths = (
                    metadata_script.NATIVE_HEADER, metadata_script.BUILD_IDENTITY_HEADER,
                    metadata_script.ENGINE_ABI_HEADER, metadata_script.MANAGED_PROPS,
                    metadata_script.MANAGED_RC, metadata_script.METADATA_JSON,
                )
                for path in paths:
                    os.utime(path, ns=(1_500_000_000_000_000_000, 1_500_000_000_000_000_000))
                timestamps = {path: path.stat().st_mtime_ns for path in paths}
                self.assertEqual(self._run("--identity-scope", scope), 0)
                self.assertEqual({path: path.stat().st_mtime_ns for path in paths}, timestamps)

    def test_default_identity_still_tracks_the_whole_repository(self) -> None:
        self.assertEqual(self._run(), 0)
        previous = self._metadata()["buildId"]
        for filename in ("Funke-Editor/src/Example.cpp", "new-notes.txt"):
            for content in ("first edit\n", "second edit\n"):
                with self.subTest(filename=filename, content=content):
                    (self.root / filename).write_text(content, encoding="utf-8")
                    self.assertEqual(self._run(), 0)
                    self.assertEqual(self._metadata()["identityScope"], "repository")
                    current = self._metadata()["buildId"]
                    self.assertNotEqual(current, previous)
                    previous = current

    def test_ci_and_release_arguments_override_engine_scope(self) -> None:
        for arguments, environment in (
            (("--tag", "v2026.1.0-rc.1"), {}),
            (("--require-clean",), {}),
            (("--github-output", str(self.root / "build/github-output.txt")), {}),
            ((), {"CI": "true"}),
            ((), {"GITHUB_ACTIONS": "true"}),
            ((), {"TF_BUILD": "True"}),
        ):
            with self.subTest(arguments=arguments, environment=environment):
                self.assertEqual(self._run("--identity-scope", "engine", *arguments, environment=environment), 0)
                self.assertEqual(self._metadata()["identityScope"], "repository")
                self.assertNotIn(".dev.engine", self._metadata()["buildId"])
        (self.root / "new-notes.txt").write_text("dirty\n", encoding="utf-8")
        self.assertEqual(self._run("--identity-scope", "engine", "--require-clean"), 1)

    def test_switching_scope_replaces_the_previous_identity(self) -> None:
        (self.root / "new-notes.txt").write_text("dirty\n", encoding="utf-8")
        for scope in ("repository", "engine", "repository"):
            with self.subTest(scope=scope):
                self.assertEqual(self._run("--identity-scope", scope), 0)
                metadata = self._metadata()
                self.assertEqual(metadata["identityScope"], scope)
                self.assertIn(metadata["buildId"], metadata_script.BUILD_IDENTITY_HEADER.read_text(encoding="utf-8"))
                self.assertIn(metadata["buildId"], metadata_script.MANAGED_PROPS.read_text(encoding="utf-8"))
                self.assertEqual(".dev.engine" in metadata["buildId"], scope == "engine")

    @unittest.skipUnless(sys.platform == "win32", "Windows release verification")
    def test_release_verifier_rejects_clean_engine_scope(self) -> None:
        self.assertEqual(self._run("--identity-scope", "engine"), 0)
        self.assertFalse(self._metadata()["dirty"])
        result = subprocess.run(
            ["powershell.exe", "-NoProfile", "-NonInteractive", "-File",
             str(REPO_ROOT / "scripts/ci/verify_windows_release.ps1"),
             "-BundleDirectory", str(self.root), "-MetadataPath", str(metadata_script.METADATA_JSON),
             "-RequireReleaseIdentity"],
            capture_output=True, text=True, timeout=30,
        )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("development-scoped build metadata", result.stderr)


class RuntimeSmokeStagingTests(unittest.TestCase):
    def test_runtime_harness_uses_a_unicode_staging_path(self) -> None:
        script = SMOKE_SCRIPT_PATH.read_text(encoding="utf-8")
        self.assertIn('prefix="Funke Runtime Smoke Prüfung "', script)

    def test_staging_never_mutates_the_supplied_runtime_payload(self) -> None:
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            source = root / "source" / "Funke-Runtime"
            source_assets = source / "Assets"
            source_assets.mkdir(parents=True)
            binary = source / runtime_smoke_test.RUNTIME_NAME
            binary.write_bytes(b"runtime")
            sentinel = source_assets / "keep.txt"
            sentinel.write_text("original\n", encoding="utf-8")

            staged_binary = runtime_smoke_test.stage_runtime_copy(binary, root / "stage")

            self.assertEqual(binary.read_bytes(), b"runtime")
            self.assertEqual(sentinel.read_text(encoding="utf-8"), "original\n")
            self.assertTrue((staged_binary.parent / "project.json").is_file())
            self.assertTrue((staged_binary.parent / "Assets/Scenes/SampleScene.scene").is_file())
            project = json.loads((staged_binary.parent / "project.json").read_text(encoding="utf-8"))
            self.assertEqual(project["projectFormatVersion"], 1)
            self.assertEqual(project["assetSerializationFormat"], "Json")


class ReleaseSigningScriptTests(unittest.TestCase):
    def test_machine_certificate_store_is_forwarded_to_signtool(self) -> None:
        script = BUILD_WINDOWS_SCRIPT.read_text(encoding="utf-8")
        self.assertIn('@{ Path = "Cert:\\LocalMachine\\My"; UseMachineStore = $true }', script)
        self.assertIn('$arguments += "/sm"', script)
        self.assertIn('-UseMachineStore:$useMachineStore', script)

    def test_required_checksum_signature_validates_certificate_trust(self) -> None:
        script = VERIFY_RELEASE_SCRIPT.read_text(encoding="utf-8")
        self.assertIn(
            '$verifySignatureOnly = -not $RequireSignature -or [bool]$ExpectedSignerThumbprint',
            script,
        )
        self.assertIn('$signedCms.CheckSignature($verifySignatureOnly)', script)


class ModuleProfileSourceTests(unittest.TestCase):
    def test_custom_profiles_only_accept_core_or_complete_application_surface(self) -> None:
        premake = ROOT_PREMAKE.read_text(encoding="utf-8")
        readme = ENGINE_README.read_text(encoding="utf-8")
        setup = SETUP_SCRIPT.read_text(encoding="utf-8")
        editor_closure = premake.index("if modules.Editor then")
        dependency_gate = premake.index(
            'if profile == "custom" and modules.Scripting and not modules.Editor then'
        )
        partial_gate = premake.index(
            'if profile == "custom"\n'
            "        and (modules.Render or modules.Audio or modules.Physics or modules.Scripting)"
        )
        self.assertGreater(dependency_gate, editor_closure)
        self.assertGreater(partial_gate, dependency_gate)
        self.assertIn(
            "--with-scripting requires --with-editor in a custom module profile",
            premake,
        )
        self.assertIn(
            "Partial custom module profiles are not supported; use --module-profile=core or add --with-editor",
            premake,
        )
        self.assertIn(
            "--module-profile=custom --with-editor",
            readme,
        )
        self.assertNotIn(
            "--module-profile=custom --with-render --with-audio",
            readme,
        )
        self.assertIn(
            "Partial custom combinations fail during project generation.",
            readme,
        )
        self.assertIn(
            '"(core-equivalent) or --with-editor for the complete application surface."',
            setup,
        )


class ModularStorageSourceTests(unittest.TestCase):
    def test_fallback_defines_the_complete_storage_api(self) -> None:
        header = STORAGE_HEADER.read_text(encoding="utf-8")
        fallback = STORAGE_FALLBACK.read_text(encoding="utf-8")
        full = FULL_SERIALIZER.read_text(encoding="utf-8")
        declared = set(re.findall(r"FUNKE_API\s+bool\s+(\w+)\s*\(", header))
        self.assertEqual(
            declared,
            {
                "BeginBinaryScene",
                "IsBinaryData",
                "ReadNextBinaryEntity",
                "ReadRootFromFile",
                "WriteRootToFile",
            },
        )
        for name in declared:
            definition = rf"(?m)^\s*(?:FUNKE_API\s+)?bool\s+{name}\s*\("
            self.assertRegex(fallback, definition)
            self.assertRegex(full, definition)

    def test_fallback_guard_matches_full_serializer_profile_gate(self) -> None:
        fallback = STORAGE_FALLBACK.read_text(encoding="utf-8")
        premake = ENGINE_PREMAKE.read_text(encoding="utf-8")
        self.assertIn(
            "#if !(FUNKE_WITH_RENDER && FUNKE_WITH_AUDIO && FUNKE_WITH_PHYSICS && FUNKE_WITH_SCRIPTING)",
            fallback,
        )
        gate = re.search(
            r'RemoveFilesUnlessModulesEnabled\(\{\s*"Render"\s*,\s*"Audio"\s*,\s*'
            r'"Physics"\s*,\s*"Scripting"\s*\}\s*,\s*\{(?P<files>.*?)\}\s*\)',
            premake,
            re.DOTALL,
        )
        self.assertIsNotNone(gate)
        self.assertIn('"src/Serialization/SceneSerializer.cpp"', gate.group("files"))

    def test_scripting_data_assets_use_only_the_storage_surface(self) -> None:
        source = DATA_ASSET_MANAGER.read_text(encoding="utf-8")
        fallback = STORAGE_FALLBACK.read_text(encoding="utf-8")
        self.assertIn('#include "Serialization/SceneSerializerStorage.hpp"', source)
        self.assertNotIn("SceneSerializerShared.hpp", source)
        self.assertRegex(
            source,
            r'(?s)#if FUNKE_WITH_APPLICATION\s+#include "Project/FunkeProject.hpp".*?'
            r'#include "Core/Application.hpp"\s+#endif',
        )
        self.assertRegex(
            source,
            r'#if !FUNKE_WITH_APPLICATION\s+return SceneSerializationFormat::Binary;\s+#else',
        )
        self.assertNotIn("SceneSerializerShared.hpp", fallback)
        self.assertNotIn('#include "Audio/', fallback)
        self.assertNotIn('#include "Graphics/', fallback)


if __name__ == "__main__":
    unittest.main()
