"""Gate regressions; these fixtures do not constitute GPU execution evidence."""

import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from scripts.ci import gpu_render_test as gpu


class GpuEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)

    def adapter(self, root=None, stage="preflight", kind="hardware", backend="Direct3D12"):
        value = {"initialized": True, "valid": True, "renderer": "Fixture adapter",
                 "vendor": "Fixture", "driver": "Fixture driver", "adapterClass": kind, "expectedClass": kind,
                 "version": f"WebGPU (Dawn) / {backend} / {'CPU' if kind == 'software' else 'DiscreteGPU'}"}
        gpu.write_json((root or self.root) / f"{stage}-adapter.json", value)
        return value

    def image(self, root=None, name="sprite2d", actual=None, negative=False):
        root = root or self.root
        actual = actual if actual is not None else bytes([255, 0, 0, 255])
        reference = bytes([0, 255, 0, 255]) if negative else bytes([255, 0, 0, 255])
        differences = [abs(a - b) for a, b in zip(actual, reference)]
        mismatches = int(max(differences) > 2)
        value = {"name": name, "width": 1, "height": 1, "tolerance": 2,
                 "gpuReadback": True, "passed": not mismatches, "mismatchedPixels": mismatches,
                 "maximumChannelError": max(differences), "negativeProbe": negative}
        (root / f"{name}-actual.rgba").write_bytes(actual)
        (root / f"{name}-reference.rgba").write_bytes(reference)
        gpu.write_json(root / f"{name}.json", value)
        return value

    def xml(self, case, root=None, failed=False, skipped=False, exception=False):
        text = (f'<doctest><TestSuite><TestCase name="{case}" skipped="{str(skipped).lower()}">'
                + ('<Exception>crashed</Exception>' if exception else '')
                + f'</TestCase></TestSuite><OverallResultsAsserts successes="3" failures="{int(failed)}"/>'
                + f'<OverallResultsTestCases successes="{int(not failed)}" failures="{int(failed)}"/></doctest>')
        (root or self.root).joinpath("doctest.xml").write_text(text, encoding="utf-8")

    def test_readback_tolerance_and_alpha_are_checked_and_pngs_retained(self):
        for actual, passed in [(bytes([253, 2, 0, 255]), True), (bytes([255, 0, 0, 252]), False)]:
            self.image(actual=actual)
            result = gpu.verify_image(self.root, "sprite2d")
            self.assertEqual(result["passed"], passed)
            for suffix in ("actual", "reference", "diff"):
                self.assertTrue((self.root / f"sprite2d-{suffix}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n"))

    def test_corrupt_or_missing_readback_and_false_pass_are_rejected(self):
        self.image(actual=bytes([0, 0, 0, 255]))
        result_path = self.root / "sprite2d.json"
        value = json.loads(result_path.read_text())
        value["passed"] = True
        gpu.write_json(result_path, value)
        with self.assertRaisesRegex(ValueError, "retained bytes"):
            gpu.verify_image(self.root, "sprite2d")
        self.image()
        (self.root / "sprite2d-actual.rgba").write_bytes(b"\0")
        with self.assertRaisesRegex(ValueError, "incomplete"):
            gpu.verify_image(self.root, "sprite2d")
        (self.root / "sprite2d-actual.rgba").unlink()
        with self.assertRaises(OSError):
            gpu.verify_image(self.root, "sprite2d")

    def test_adapter_class_backend_and_preflight_identity_are_required(self):
        original = self.adapter()
        path = self.root / "preflight-adapter.json"
        gpu.verify_adapter(path, "hardware", "Direct3D12")
        for kind, backend in [("software", "Direct3D12"), ("hardware", "Vulkan")]:
            with self.assertRaises(ValueError):
                gpu.verify_adapter(path, kind, backend)
        changed = {**original, "renderer": "Other adapter"}
        gpu.write_json(path, changed)
        with self.assertRaisesRegex(ValueError, "different adapter"):
            gpu.verify_adapter(path, "hardware", "Direct3D12", original)

    def test_zero_skipped_wrong_case_and_exception_cannot_pass(self):
        case = gpu.CASES["preflight"][0]
        for values in ({"skipped": True}, {"exception": True}, {"failed": True}):
            self.xml(case, **values)
            with self.assertRaises(ValueError):
                gpu.verify_doctest(self.root / "doctest.xml", case)
        self.xml("different test")
        with self.assertRaises(ValueError):
            gpu.verify_doctest(self.root / "doctest.xml", case)
        (self.root / "doctest.xml").write_text("<doctest/>")
        with self.assertRaises(ValueError):
            gpu.verify_doctest(self.root / "doctest.xml", case)

    def test_negative_probe_requires_one_assertion_failure(self):
        case = gpu.CASES["basics"][0]
        self.xml(case, failed=True)
        gpu.verify_doctest(self.root / "doctest.xml", case, expected_failure=True)
        self.xml(case)
        with self.assertRaises(ValueError):
            gpu.verify_doctest(self.root / "doctest.xml", case, expected_failure=True)

    def test_runner_resets_inherited_gpu_flags(self):
        with patch.dict(os.environ, {"FUNKE_TEST_RENDER_GPU": "0", "FUNKE_TEST_GPU_NEGATIVE": "1",
                                    "FUNKE_TEST_GPU_ADAPTER": "software", "FUNKE_TEST_GPU_FUTURE_OVERRIDE": "1"}):
            env = gpu.child_environment(self.root, "hardware", "Direct3D12")
        self.assertEqual(env["FUNKE_TEST_RENDER_GPU"], "1")
        self.assertEqual(env["FUNKE_TEST_ASYNC_ASSET_GPU"], "1")
        self.assertEqual(env["FUNKE_TEST_GPU_REQUIRED"], "1")
        self.assertEqual(env["FUNKE_TEST_GPU_NEGATIVE"], "0")
        self.assertNotIn("FUNKE_TEST_GPU_FUTURE_OVERRIDE", env)

    def test_staging_supplies_required_mask_without_overwriting_assets(self):
        source = self.root / "source"
        mask = source / "Textures/Default/Square.png"
        mask.parent.mkdir(parents=True)
        mask.write_bytes(b"white mask fixture")
        binary = self.root / "bin/tests.exe"
        result = gpu.prepare_assets(binary, source)
        staged = binary.parent / "FunkeAssets/Textures/Default/Square.png"
        self.assertEqual(staged.read_bytes(), mask.read_bytes())
        self.assertEqual(len(result["files"]), 1)
        staged.write_bytes(b"preserve user asset")
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            gpu.prepare_assets(binary, source)
        self.assertEqual(staged.read_bytes(), b"preserve user asset")

    def fake_process(self, command, root, env, timeout):
        negative = root.name == "negative"
        stage = "basics" if negative else root.name
        self.adapter(root, stage)
        for name in gpu.CASES[stage][1]:
            self.image(root, name, negative=negative and name == "sprite2d")
        self.xml(gpu.CASES[stage][0], root, failed=negative)
        (root / "process.log").write_text("fixture only")
        return int(negative)

    def test_runner_requires_all_cases_and_specific_negative_failure(self):
        binary = self.root / "tests.exe"
        binary.write_bytes(b"fixture")
        output = self.root / "evidence"
        with patch.object(gpu, "driver_inventory", return_value={"returncode": 0}), \
                patch.object(gpu, "run_process", side_effect=self.fake_process):
            result = gpu.main(["--binary", str(binary), "--output", str(output), "--adapter", "hardware",
                               "--backend", "Direct3D12", "--negative-probe"])
        self.assertEqual(result, 0)
        summary = json.loads((output / "summary.json").read_text())
        self.assertEqual([stage["status"] for stage in summary["stages"]], ["passed"] * 6 + ["expected-failure"])
        self.assertEqual(len(list(output.rglob("*-actual.png"))), 24)

    def test_missing_adapter_reports_failed_even_if_process_exits_zero(self):
        binary = self.root / "tests.exe"
        binary.write_bytes(b"fixture")
        output = self.root / "evidence"
        with patch.object(gpu, "driver_inventory", return_value={"returncode": 0}), \
                patch.object(gpu, "run_process", return_value=0):
            result = gpu.main(["--binary", str(binary), "--output", str(output), "--adapter", "hardware",
                               "--backend", "Direct3D12"])
        self.assertEqual(result, 1)
        self.assertEqual(json.loads((output / "summary.json").read_text())["status"], "failed")


if __name__ == "__main__":
    unittest.main()
