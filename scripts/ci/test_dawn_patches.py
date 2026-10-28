"""Exercise Dawn patch checkout/application without fetching or building Dawn."""

from __future__ import annotations

import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
PATCH_PATH = Path("scripts/dawn/patches/0001-d3d-present-allow-tearing.patch")
SOURCE_PATH = Path("src/dawn/native/d3d/SwapChainD3D.cpp")
# The unchanged PresentDXGISwapChain context from the pinned Dawn revision.
SOURCE = b"""MaybeError SwapChain::PresentDXGISwapChain() {
    // Do the actual present. DXGI_STATUS_OCCLUDED is a valid return value that's just a
    // message to the application that it could stop rendering.
    HRESULT presentResult = mDXGISwapChain->Present(PresentModeToSwapInterval(GetPresentMode()), 0);
    if (presentResult != DXGI_STATUS_OCCLUDED) {
        DAWN_TRY(CheckHRESULT(presentResult, "IDXGISwapChain::Present"));
    }
}
"""


class DawnPatchCheckoutTests(unittest.TestCase):
    def test_patch_applies_after_lf_and_windows_checkouts(self) -> None:
        for autocrlf in ("false", "true"):
            with self.subTest(autocrlf=autocrlf), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)

                def git(*arguments: str) -> subprocess.CompletedProcess[str]:
                    result = subprocess.run(
                        ["git", "-C", str(root), "-c", f"core.autocrlf={autocrlf}",
                         "-c", "core.safecrlf=false", *arguments],
                        capture_output=True, text=True, encoding="utf-8",
                    )
                    self.assertEqual(result.returncode, 0, result.stderr)
                    return result

                git("init", "--quiet")
                # Dawn's own attributes force LF for its source files.
                (root / ".gitattributes").write_bytes(
                    (REPO_ROOT / ".gitattributes").read_bytes()
                    + b"\nsrc/**/*.cpp text eol=lf\n"
                )
                patch = root / PATCH_PATH
                patch.parent.mkdir(parents=True)
                patch.write_bytes((REPO_ROOT / PATCH_PATH).read_bytes())
                source = root / SOURCE_PATH
                source.parent.mkdir(parents=True)
                source.write_bytes(SOURCE)
                git("add", ".")
                patch.unlink()
                source.unlink()
                git("checkout-index", "--all")

                self.assertNotIn(b"\r\n", patch.read_bytes())
                self.assertEqual(source.read_bytes(), SOURCE)
                git("apply", "--check", str(patch))
                git("apply", str(patch))
                self.assertIn(b"DXGI_PRESENT_ALLOW_TEARING", source.read_bytes())
                # SetupDawn uses this check to skip a patch on repeated setup.
                git("apply", "--reverse", "--check", str(patch))


if __name__ == "__main__":
    unittest.main()
