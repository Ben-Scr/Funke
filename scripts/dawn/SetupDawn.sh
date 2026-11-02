#!/usr/bin/env bash
# ============================================================================
# SetupDawn.sh  ---  Linux/macOS counterpart to SetupDawn.bat.
#
# See SetupDawn.bat for full prose. Builds Dawn's monolithic webgpu_dawn
# shared library under External/dawn/build/. Run once; subsequent engine
# builds link the result.
# ============================================================================
set -euo pipefail

cd "$(dirname "$0")/../.."

DAWN_DIR="External/dawn"
DAWN_BUILD_DIR="${DAWN_DIR}/build"

echo "============================================"
echo "  Dawn WebGPU Setup"
echo "============================================"
echo "Target: ${DAWN_DIR}"
echo

# --- Tool checks -------------------------------------------------------------
for tool in git cmake python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "[ERROR] $tool is not on PATH."
        exit 1
    fi
done

# --- Resolve pinned revision -------------------------------------------------
# DAWN_REVISION overrides the versioned commit pin. Both paths require an explicit revision.
if [[ -z "${DAWN_REVISION:-}" && -f "scripts/dawn/dawn-revision.txt" ]]; then
    DAWN_REVISION="$(tr -d '[:space:]' < scripts/dawn/dawn-revision.txt)"
fi

# --- Sources -----------------------------------------------------------------
python3 scripts/dawn/BootstrapDawn.py --revision "${DAWN_REVISION}"

# --- Configure ---------------------------------------------------------------
echo "[2/3] Configuring Dawn via CMake..."
# Dawn's build is Clang-oriented: it passes Clang-only -Wno-* flags, and under
# GCC 13 / libstdc++ it fails to compile (no operator== for dawn::native::Format
# during ityp::array instantiation). Prefer Clang when present; fall back to the
# CMake default otherwise (macOS already defaults to Clang). The resulting
# libstdc++/Itanium-ABI static lib links fine against the GCC-built engine.
CMAKE_ARGS=(
    -S "${DAWN_DIR}" -B "${DAWN_BUILD_DIR}"
    -DCMAKE_BUILD_TYPE=Release
    -DDAWN_FETCH_DEPENDENCIES=OFF
    -DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED
    -DDAWN_ENABLE_INSTALL=OFF
    -DDAWN_BUILD_SAMPLES=OFF
    -DTINT_BUILD_TESTS=OFF
    -DTINT_BUILD_CMD_TOOLS=OFF
    # Funke only feeds WGSL to Dawn; skip the SPIR-V reader and SPIRV-Tools validation.
    -DTINT_BUILD_SPV_READER=OFF
    -DDAWN_ENABLE_SPIRV_VALIDATION=OFF
)
if [[ "$(uname -s)" == "Darwin" ]]; then
    # Same floor as SDL3 (scripts/sdl/SetupSDL.py) and the exported games' Info.plist.
    CMAKE_ARGS+=(-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
        -DCMAKE_OSX_ARCHITECTURES="$(python3 scripts/dawn/ValidateDawnCache.py --print-architecture)")
fi
if command -v clang >/dev/null 2>&1 && command -v clang++ >/dev/null 2>&1; then
    echo "      Compiler: Clang ($(command -v clang++))"
    CMAKE_ARGS+=(-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++)
else
    echo "      Compiler: CMake default (Clang not found; Dawn may fail under GCC)."
fi
# Managed Macs can block the Command Line Tools' make ("Permission denied"); use GNU make then.
if ! make --version >/dev/null 2>&1 && command -v gmake >/dev/null 2>&1; then
    echo "      Make: $(command -v gmake) (make does not run here)"
    CMAKE_ARGS+=(-DCMAKE_MAKE_PROGRAM="$(command -v gmake)")
fi
cmake "${CMAKE_ARGS[@]}"

# --- Build -------------------------------------------------------------------
echo "[3/3] Building webgpu_dawn..."
cmake --build "${DAWN_BUILD_DIR}" --target webgpu_dawn -j"$(getconf _NPROCESSORS_ONLN || echo 4)"
python3 scripts/dawn/ValidateDawnCache.py --write

echo
echo "============================================"
echo "  Dawn build complete."
echo "============================================"
echo "Headers : ${DAWN_DIR}/include"
echo "Headers : ${DAWN_BUILD_DIR}/gen/include"
if [[ "$(uname -s)" == "Darwin" ]]; then
    echo "Library : ${DAWN_BUILD_DIR}/src/dawn/native/libwebgpu_dawn.dylib"
else
    echo "Library : ${DAWN_BUILD_DIR}/src/dawn/native/libwebgpu_dawn.so"
fi
echo
echo "Next: regenerate the engine Makefiles, e.g."
echo "  ./scripts/Setup.sh   (or: ./vendor/bin/premake5 gmake)"
echo
