"""Fast tests for the standalone gate; no engine, SDK or graphical session required."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

import standalone_sdk_test as gate


class StandaloneTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="Funke-Standalone-Unitprüfung-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def install(self):
        install = self.root / "installation"
        manifest = {"schemaVersion": 1, "engineVersion": "1.0.0", "platform": "windows", "architecture": "x86_64",
                    "capabilities": {"managedProjects": True, "nativeDevelopment": False, "sourceBuild": False},
                    "runtimes": {"Development": "Runtime-Development", "Release": "Runtime-Dist"}}
        files = ["funke.exe", "Funke-Launcher.exe", "Funke-Editor.exe", "Funke-Engine.dll", "EngineSDK/premake5.exe"]
        files += ["EngineSDK/premake/" + name + ".lua" for name in
                  ("project-packages", "package-loader", "architecture", "compat", "fix_csharp_platforms")]
        managed = ("Funke-ScriptCore.dll", "Funke-ScriptCore.deps.json", "Funke-ScriptCore.runtimeconfig.json")
        files += ["EngineSDK/Funke-ScriptCore/" + name for name in managed]
        for folder in manifest["runtimes"].values():
            files += ["EngineSDK/" + folder + "/" + name for name in
                      ("Funke-Runtime.exe", "Funke-Engine.dll", "SDL3.dll", "nethost.dll", *managed)]
        for name in files:
            path = install / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("fixture", encoding="utf-8")
        (install / "EngineSDK/templates").mkdir()
        (install / "EngineSDK/FunkeSDK.json").write_text(json.dumps(manifest), encoding="utf-8")
        return install, manifest

    def test_valid_binary_payload_allows_managed_package_source_and_premake(self):
        install, manifest = self.install()
        source = install / "EngineSDK/packages/Fixture/Source/Probe.cs"
        source.parent.mkdir(parents=True)
        source.write_text("managed package source", encoding="utf-8")
        actual, inventory = gate.validate_installation(install)
        self.assertEqual(manifest, actual)
        self.assertIn("EngineSDK/premake5.exe", inventory)
        self.assertIn(source.relative_to(install).as_posix(), inventory)

    def test_manifest_rejects_invalid_schema_capability_and_runtime(self):
        install, manifest = self.install()
        cases = [[], manifest | {"schemaVersion": True}, manifest | {"schemaVersion": 2},
                 manifest | {"engineVersion": " "}, manifest | {"capabilities": []},
                 manifest | {"capabilities": manifest["capabilities"] | {"sourceBuild": True}},
                 manifest | {"capabilities": manifest["capabilities"] | {"nativeDevelopment": True}},
                 manifest | {"capabilities": manifest["capabilities"] | {"managedProjects": 1}},
                 manifest | {"runtimes": []}, manifest | {"runtimes": {"Release": "../escape"}}]
        for invalid in cases:
            with self.subTest(manifest=invalid):
                (install / "EngineSDK/FunkeSDK.json").write_text(json.dumps(invalid), encoding="utf-8")
                with self.assertRaises(ValueError):
                    gate.validate_installation(install)

    def test_runtime_missing_managed_host_metadata_is_rejected(self):
        install, _ = self.install()
        (install / "EngineSDK/Runtime-Dist/Funke-ScriptCore.runtimeconfig.json").unlink()
        with self.assertRaisesRegex(ValueError, "Incomplete runtime"):
            gate.validate_installation(install)

    def test_missing_launcher_is_rejected_before_running(self):
        install, _ = self.install()
        (install / "Funke-Launcher.exe").unlink()
        with self.assertRaisesRegex(ValueError, "Funke-Launcher.exe"):
            gate.validate_installation(install)

    def test_sdk_version_requires_stable_10_0_feature_band(self):
        for version in ("9.0.315", "10.0.99", "10.1.100", "11.0.100", "10.0.100-preview.1", "10.0.303-rc.2"):
            with self.subTest(version=version):
                self.assertIsNone(gate.stable_dotnet10_sdk(version + " [C:/dotnet/sdk]"))
        self.assertEqual("10.0.100", gate.stable_dotnet10_sdk("9.0.315 [C:/dotnet/sdk]\n10.0.100 [C:/dotnet/sdk]"))
        self.assertEqual("10.0.303", gate.stable_dotnet10_sdk("10.0.303 [C:/dotnet/sdk]\n10.0.203 [C:/dotnet/sdk]\n11.0.100 [C:/dotnet/sdk]"))

    def test_binary_payload_rejects_source_and_native_tools(self):
        install, _ = self.install()
        for relative in ("EngineSDK/Funke-Engine/src/A.cpp", "EngineSDK/External/entt/include.h",
                         "EngineSDK/Funke-ScriptCore/Source/A.cs", "EngineSDK/Funke-ScriptCore/Funke-ScriptCore.csproj",
                         "EngineSDK/bin/Debug/Funke-Engine.lib", "EngineSDK/premake5.lua", "Tools/MSBuild.exe", "git.cmd"):
            with self.subTest(path=relative):
                path = install / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("forbidden", encoding="utf-8")
                with self.assertRaises(ValueError):
                    gate.installation_inventory(install)
                path.unlink()
                parent = path.parent
                while parent != install and not list(parent.iterdir()):
                    parent.rmdir()
                    parent = parent.parent

    def test_payload_rejects_symlink_even_when_target_inside(self):
        install, _ = self.install()
        try:
            (install / "linked.exe").symlink_to(install / "funke.exe")
        except OSError as error:
            self.skipTest(f"Host does not permit symlinks: {error}")
        with self.assertRaisesRegex(ValueError, "Links/reparse"):
            gate.installation_inventory(install)

    def test_relative_runtime_paths_cannot_escape_or_alias_root(self):
        (self.root / "Runtime").mkdir()
        for value in (None, "", ".", "..", "../Runtime", "Runtime/../Runtime", "Runtime//child",
                      "Runtime/./child", "/Runtime", "C:/Runtime", "Runtime\\child", "Runtime:stream"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                gate.sdk_relative(self.root, value)
        self.assertEqual(self.root / "Runtime", gate.sdk_relative(self.root, "Runtime"))

    def test_work_directory_must_be_outside_repository_and_install(self):
        repository = self.root / "repository"
        with self.assertRaises(ValueError):
            gate.require_external(repository / ".." / "repository" / "tmp", [repository])
        gate.require_external(self.root / "repository-other", [repository])

    def test_environment_discards_ambient_roots_toolchains_and_user_profile(self):
        base = dict(os.environ) | {"FUNKE_ENGINE_ROOT": "old-engine", "FUNKE_HOME": "old-home",
            "MSBuildSDKsPath": "native-msbuild", "VSINSTALLDIR": "vs", "PATH": "inherited-tools",
            "NUGET_PACKAGES": "shared-cache", "USERPROFILE": "real-user", "DOTNET_ROOT": "wrong-dotnet",
            "ProgramFiles(x86)": "C:/Program Files (x86)", "ProgramData": "C:/ProgramData", "SystemDrive": "C:"}
        dotnet, guards = self.root / "sdk/dotnet.exe", self.root / "guards"
        env = gate.child_environment(base, self.root, dotnet, guards)
        self.assertEqual(["FUNKE_USER_DATA_ROOT"], [key for key in env if key.startswith("FUNKE_")])
        self.assertEqual(str(self.root / "funke-user-data"), env["FUNKE_USER_DATA_ROOT"])
        self.assertNotIn("MSBuildSDKsPath", env)
        self.assertNotIn("VSINSTALLDIR", env)
        self.assertNotIn("inherited-tools", env["PATH"])
        for key in ("ProgramFiles(x86)", "ProgramData", "SystemDrive"):
            self.assertEqual(base[key], env[key])
        self.assertEqual(str(self.root / "user"), env["USERPROFILE"])
        self.assertEqual(str(dotnet.parent), env["DOTNET_ROOT"])
        player = gate.child_environment(base, self.root, dotnet, guards, player=True)
        self.assertNotIn(str(dotnet.parent), player["PATH"].split(os.pathsep))
        self.assertFalse(Path(player["DOTNET_ROOT"]).exists())

    def test_generated_project_rejects_source_project_and_absolute_repository_reference(self):
        project = self.root / "project"
        project.mkdir()
        generated = project / "Package.csproj"
        for text in ("<ProjectReference Include='Funke-ScriptCore.csproj' />",
                     "<HintPath>" + str(self.root / "repo/bin/Funke-ScriptCore.dll") + "</HintPath>"):
            generated.write_text(text, encoding="utf-8")
            with self.subTest(reference=text), self.assertRaises(ValueError):
                gate.check_references(project, [self.root / "repo"])
        generated.write_text("<HintPath>../../EngineSDK/Funke-ScriptCore/Funke-ScriptCore.dll</HintPath>", encoding="utf-8")
        gate.check_references(project, [self.root / "repo"])

    def test_generated_project_rejects_aliased_absolute_repository_reference(self):
        repository = self.root / "repository"
        repository.mkdir()
        alias = self.root / "alias"
        if os.name == "nt":
            result = subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", "mklink", "/J",
                                     str(alias), str(repository)], capture_output=True, text=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.addCleanup(os.rmdir, alias)
        else:
            alias.symlink_to(repository, target_is_directory=True)
            self.addCleanup(alias.unlink)
        project = self.root / "project"
        project.mkdir()
        generated = project / "Package.csproj"
        generated.write_text("<HintPath>" + str(alias / "bin/Funke-ScriptCore.dll") + "</HintPath>", encoding="utf-8")
        for forbidden in (repository, alias):
            with self.subTest(forbidden=forbidden), self.assertRaises(ValueError):
                gate.check_references(project, [forbidden])

    def harness(self):
        root = self.root / "work"
        root.mkdir()
        env = gate.child_environment(dict(os.environ), root, Path(sys.executable), root / "guards")
        return gate.Harness(root, root / "evidence.json", env, 30)

    def test_nonzero_child_and_timeout_preserve_structured_evidence(self):
        harness = self.harness()
        with self.assertRaisesRegex(RuntimeError, "failed"):
            harness.run("failure", [sys.executable, "-c", "raise SystemExit(7)"])
        evidence = json.loads(harness.report.read_text(encoding="utf-8"))
        self.assertEqual(7, evidence["phases"][0]["exitCode"])
        with self.assertRaisesRegex(RuntimeError, "timeout"):
            harness.run("timeout", [sys.executable, "-c", "import time; time.sleep(20)"], timeout=0.1)
        evidence = json.loads(harness.report.read_text(encoding="utf-8"))
        self.assertTrue(evidence["phases"][1]["timedOut"])
        self.assertTrue(Path(evidence["phases"][1]["log"]).is_file())

    def test_forbidden_tool_log_and_managed_marker_are_mandatory_evidence(self):
        harness = self.harness()
        harness.assert_no_tools()
        Path(harness.env["STANDALONE_TOOL_LOG"]).write_text('{"tool":"git.exe"}', encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "forbidden native tool"):
            harness.assert_no_tools()
        marker = self.root / "probe.txt"
        with self.assertRaises(RuntimeError):
            gate.require_marker(marker, "V2|Release|package-ok")
        marker.write_text("V1|Release|package-ok", encoding="utf-8")
        with self.assertRaises(RuntimeError):
            gate.require_marker(marker, "V2|Release|package-ok")
        marker.write_text("V2|Release|package-ok", encoding="utf-8")
        gate.require_marker(marker, "V2|Release|package-ok")

    def test_abi_mismatch_rejects_even_successful_process_exit(self):
        harness = self.harness()
        with self.assertRaisesRegex(RuntimeError, "error marker"):
            harness.run("abi-mismatch", [sys.executable, "-c", "print('Application was compiled against other engine headers')"])
        self.assertEqual(0, harness.evidence["phases"][-1]["exitCode"])

    def test_package_compilation_requires_generated_project_and_result(self):
        project = self.root / "project"
        harness = Mock()
        dotnet = self.root / "sdk/dotnet.exe"
        with self.assertRaisesRegex(RuntimeError, "did not generate"):
            gate.build_managed_packages(harness, dotnet, project)
        harness.run.assert_not_called()
        generated = project / ".funke/build/packages/generated/Pkg.Standalone.ManagedProbe/Pkg.Standalone.ManagedProbe.csproj"
        generated.parent.mkdir(parents=True)
        generated.write_text("<Project />", encoding="utf-8")
        with self.assertRaisesRegex(RuntimeError, "produced no expected DLL"):
            gate.build_managed_packages(harness, dotnet, project)
        command = harness.run.call_args.args[1]
        self.assertEqual([str(dotnet), "build", str(generated)], command[:3])
        self.assertIn("Release", command)
        self.assertIn("-p:Platform=x64", command)

    def test_report_retries_windows_reader_conflicts_without_exposing_partial_json(self):
        report = self.root / "evidence.json"
        report.write_text('{"status":"old"}', encoding="utf-8")
        original_replace = Path.replace
        attempts = []

        def replace(source, destination):
            attempts.append(source)
            self.assertEqual({"status": "old"}, json.loads(report.read_text(encoding="utf-8")))
            if len(attempts) < 3:
                error = PermissionError("temporary Windows reader conflict")
                error.winerror = 32 if len(attempts) == 1 else 5
                raise error
            return original_replace(source, destination)

        with patch.object(Path, "replace", replace), patch.object(gate.time, "sleep") as sleep:
            gate.atomic_write_json(report, {"status": "new", "complete": [1, 2, 3]})
        self.assertEqual(3, len(attempts))
        self.assertEqual([0.025, 0.05], [call.args[0] for call in sleep.call_args_list])
        self.assertEqual({"status": "new", "complete": [1, 2, 3]}, json.loads(report.read_text(encoding="utf-8")))
        self.assertEqual([], list(self.root.glob("*.tmp")))

    def test_report_persistent_sharing_failure_has_bounded_retries_and_preserves_previous_report(self):
        report = self.root / "evidence.json"
        report.write_text('{"status":"old"}', encoding="utf-8")
        error = PermissionError("persistent Windows reader conflict")
        error.winerror = 33
        with patch.object(Path, "replace", side_effect=error) as replace, patch.object(gate.time, "sleep") as sleep:
            with self.assertRaises(PermissionError):
                gate.atomic_write_json(report, {"status": "new"})
        self.assertEqual(len(gate.REPORT_RETRY_DELAYS) + 1, replace.call_count)
        self.assertEqual(len(gate.REPORT_RETRY_DELAYS), sleep.call_count)
        self.assertLess(sum(call.args[0] for call in sleep.call_args_list), 2)
        self.assertEqual({"status": "old"}, json.loads(report.read_text(encoding="utf-8")))
        self.assertEqual([], list(self.root.glob("*.tmp")))

    def test_report_other_io_errors_fail_immediately(self):
        error = OSError("disk full")
        error.winerror = 112
        with patch.object(Path, "replace", side_effect=error) as replace, patch.object(gate.time, "sleep") as sleep:
            with self.assertRaises(OSError):
                gate.atomic_write_json(self.root / "evidence.json", {})
        replace.assert_called_once()
        sleep.assert_not_called()

    def test_editor_process_errors_fail_after_successful_bridge_and_both_script_revisions(self):
        harness = self.harness()
        project = self.root / "project"
        editor = Mock(pid=1234)
        editor.poll.return_value = None

        def bridge(label, command, **kwargs):
            harness.evidence["phases"].append({"name": label, "exitCode": 0})
            if command[2] == "recompile":
                return '{"succeeded":true}'
            if command[2] == "play":
                script = (project / "Assets/Scripts/StandaloneProbe.cs").read_text(encoding="utf-8")
                revision = "V2" if '"V2|' in script else "V1"
                (harness.root / "editor-probe.txt").write_text(revision + "|editor|package-ok", encoding="utf-8")
            return "{}"

        for marker in ("WebGPU [Validation]", "interop mismatch", "was compiled against other engine headers"):
            def launch(*args, **kwargs):
                kwargs["stdout"].write((marker + "\n").encode("utf-8"))
                return editor
            with self.subTest(marker=marker), patch.object(gate.subprocess, "Popen", side_effect=launch), \
                    patch.object(harness, "run", side_effect=bridge), patch.object(gate, "kill_tree") as cleanup:
                with self.assertRaisesRegex(RuntimeError, "Editor emitted an error marker"):
                    gate.editor_roundtrip(harness, self.root / "install", project, False)
                cleanup.assert_called_once_with(editor)
                self.assertEqual("passed V1 and V2", harness.evidence["editor"]["editCompilePlay"])
                self.assertNotIn("processLog", harness.evidence["editor"])

    def test_process_log_scan_detects_marker_across_read_boundary(self):
        log = self.root / "editor.log"
        log.write_bytes(b"x" * (64 * 1024 - 8) + b"WebGPU [Validation]\n")
        with self.assertRaisesRegex(RuntimeError, "error marker"):
            gate.require_clean_log(log, "Editor")
        log.write_text("V2|editor|package-ok\nNormal shutdown\n", encoding="utf-8")
        gate.require_clean_log(log, "Editor")


if __name__ == "__main__":
    unittest.main()
