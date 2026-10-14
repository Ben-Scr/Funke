#!/usr/bin/env python3
"""Index.Platform.Android builder (Index Platform Builder protocol v1).

Produces a signed arm64-v8a APK without Gradle: CMake/NDK builds libmain.so (engine, runtime, Mono
backend), javac + d8 build classes.dex from SDL's Java glue, aapt2 links the manifest and assets,
then zipalign + apksigner finish the package.
"""

from __future__ import annotations

import hashlib
import os
import re
import shutil
import sys
import zipfile
from pathlib import Path

PACKAGE_ROOT = Path(__file__).resolve().parents[1]


def _bootstrap_sdk_path(engine_root: Path | None) -> None:
    roots = [engine_root] if engine_root else []
    roots += [Path(os.environ["INDEX_ROOT"])] if os.environ.get("INDEX_ROOT") else []
    roots.append(PACKAGE_ROOT.parents[1])  # engine checkout layout: <root>/platforms/<package>
    for root in roots:
        sdk = root / "scripts" / "platform"
        if (sdk / "index_platform_sdk").is_dir():
            sys.path.insert(0, str(sdk))
            return


def _engine_root_from_argv() -> Path | None:
    argv = sys.argv
    for i, arg in enumerate(argv):
        if arg == "--engine-root" and i + 1 < len(argv):
            return Path(argv[i + 1])
        if arg == "--request" and i + 1 < len(argv):
            try:
                import json
                return Path(json.loads(Path(argv[i + 1]).read_text(encoding="utf-8"))["engineRoot"])
            except (OSError, KeyError, ValueError):
                return None
    return None


_bootstrap_sdk_path(_engine_root_from_argv())
sys.path.insert(0, str(Path(__file__).resolve().parent))

from index_platform_sdk import (  # noqa: E402
    BuildRequest, BuilderError, emit_log, emit_progress, find_cmake, platform_cache, run, run_builder,
)
from index_platform_sdk.deps import CMakeToolchain, build_dawn, build_sdl3  # noqa: E402
from index_platform_sdk.orientation import ANDROID_SCREEN_ORIENTATION, project_orientation  # noqa: E402
from index_platform_sdk.tools import set_log_file  # noqa: E402

import android_toolchain  # noqa: E402

ABI = "arm64-v8a"
RID = "android-arm64"
MONO_PACKAGE = "Index.ScriptRuntime.Mono"


def _find_mono_package(engine_root: Path) -> Path:
    candidates = [PACKAGE_ROOT.parent / MONO_PACKAGE, engine_root / "platforms" / MONO_PACKAGE]
    for candidate in candidates:
        if (candidate / "index-package.lua").is_file():
            return candidate
    raise BuilderError(f"{MONO_PACKAGE} is not installed; it is required for C# scripting on Android.")


def describe() -> dict:
    return {
        "platform": "Android",
        "displayName": "Android",
        "hosts": ["windows", "macos", "linux"],
        "artifact": "apk",
        "architectures": [ABI],
        "options": {
            "orientation": "raw android:screenOrientation value; overrides the project's Mobile > Orientation setting",
            "backgroundAudio": "true declares the foreground media service Audio.PlayInBackground needs (keeps playing with the app closed)",
            "permissions": "extra manifest permissions, comma separated (READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32 = maxSdkVersion 32)",
            "keystore": "path to a release keystore (debug key when empty)",
            "keystorePassword": "env:NAME reads it from the environment instead of the .build file",
            "keyAlias": "", "keyPassword": "defaults to keystorePassword; env:NAME works here too",
            "sdkPath": "Android SDK (defaults to ANDROID_HOME / Android Studio / Unity's bundled SDK)",
            "ndkPath": "Android NDK (defaults to the newest NDK inside the SDK)",
            "jdkPath": "JDK 17+ (defaults to JAVA_HOME / Android Studio's JBR)",
            "versionCode": "integer, derived from the version when empty",
            "aot": "true precompiles C# scripts ahead of time (Mono AOT); default: on for Release, off for Development",
            "aotLlvm": "false skips LLVM optimization of the AOT code (faster first build, slower code); default true",
        },
    }


def check(engine_root: Path | None) -> dict:
    toolchain, issues = android_toolchain.resolve()
    try:
        find_cmake(engine_root)
    except BuilderError as error:
        issues.append({"severity": "error", "message": str(error), "fix": "Install CMake 3.22 or newer."})
    if toolchain and not toolchain.ninja and not shutil.which("ninja"):
        issues.append({"severity": "error", "message": "Ninja not found.",
                       "fix": "Install the 'CMake' package in the Android SDK Manager (it bundles Ninja)."})
    if engine_root:
        for required in (engine_root / "External" / "SDL" / "CMakeLists.txt", engine_root / "External" / "dawn" / "CMakeLists.txt"):
            if not required.is_file():
                issues.append({"severity": "error", "message": f"Engine source missing: {required}",
                               "fix": "Use an engine checkout with submodules and Dawn set up."})
    return {"ok": not any(i["severity"] == "error" for i in issues), "issues": issues,
            "details": {"sdk": str(toolchain.sdk) if toolchain else "", "ndk": str(toolchain.ndk) if toolchain else "",
                        "jdk": str(toolchain.jdk) if toolchain else ""}}


def _sanitize_identifier(text: str) -> str:
    part = re.sub(r"[^A-Za-z0-9_]", "", text.replace(" ", "_").replace("-", "_")).lower()
    if not part or part[0].isdigit():
        part = "app" + part
    return part


def _app_id(request: BuildRequest) -> str:
    if request.app_id:
        return request.app_id
    company = _sanitize_identifier(request.company or "index")
    return f"com.{company}.{_sanitize_identifier(request.project_name)}"


def _version_code(request: BuildRequest) -> int:
    explicit = str(request.options.get("versionCode") or "").strip()
    if explicit.isdigit():
        return int(explicit)
    numbers = [int(n) for n in re.findall(r"\d+", request.version)[:3]] + [0, 0, 0]
    return max(1, numbers[0] * 10000 + numbers[1] * 100 + numbers[2])


_BACKGROUND_AUDIO_PERMISSIONS = "FOREGROUND_SERVICE, FOREGROUND_SERVICE_MEDIA_PLAYBACK, POST_NOTIFICATIONS"
_BACKGROUND_AUDIO_SERVICE = ('        <service android:name="dev.index.player.IndexAudioService" '
                             'android:foregroundServiceType="mediaPlayback" android:exported="false" />\n')


def _background_audio(request: BuildRequest) -> bool:
    return request.option("backgroundAudio").strip().lower() in ("1", "true", "yes", "on")


def _permissions_option(request: BuildRequest) -> str:
    extra = request.option("permissions")
    if not _background_audio(request):
        return extra
    return f"{extra}, {_BACKGROUND_AUDIO_PERMISSIONS}" if extra.strip() else _BACKGROUND_AUDIO_PERMISSIONS


def _permission_lines(option: str) -> str:
    """"READ_MEDIA_AUDIO, READ_EXTERNAL_STORAGE:32" -> <uses-permission> lines; bare names get the android.permission. prefix."""
    lines = []
    for entry in (part.strip() for part in option.split(",")):
        if not entry:
            continue
        name, _, max_sdk = entry.partition(":")
        name = name.strip()
        if not re.fullmatch(r"[A-Za-z0-9_.]+", name):
            raise BuilderError(f"Invalid Android permission '{entry}'.")
        if "." not in name:
            name = "android.permission." + name
        max_attribute = f' android:maxSdkVersion="{int(max_sdk)}"' if max_sdk.strip().isdigit() else ""
        lines.append(f'    <uses-permission android:name="{name}"{max_attribute} />\n')
    return "".join(lines)


def _ndk_toolchain(tc: android_toolchain.AndroidToolchain) -> CMakeToolchain:
    ninja = tc.ninja or (Path(shutil.which("ninja")) if shutil.which("ninja") else None)
    return CMakeToolchain(
        key=f"Android/{ABI}",
        configure_args=[
            f"-DCMAKE_TOOLCHAIN_FILE={tc.ndk / 'build' / 'cmake' / 'android.toolchain.cmake'}",
            f"-DANDROID_ABI={ABI}",
            f"-DANDROID_PLATFORM=android-{android_toolchain.MIN_SDK}",
            "-DANDROID_STL=c++_static",
        ],
        generator="Ninja",
        make_program=ninja,
    )


def _build_native(request: BuildRequest, tc: android_toolchain.AndroidToolchain, cmake_tc: CMakeToolchain,
                  sdl3: Path, dawn, mono_pack, mono_package: Path) -> Path:
    cmake = find_cmake(request.engine_root)
    engine_key = hashlib.sha256(str(request.engine_root.resolve()).encode()).hexdigest()[:10]
    build_dir = platform_cache("Android", ABI, f"player-{engine_key}-{request.configuration}-{request.modules_key}")
    args = [
        f"-DINDEX_ROOT={request.engine_root.as_posix()}",
        f"-DINDEX_MONO_PACKAGE={mono_package.as_posix()}",
        f"-DINDEX_MONO_PACK_NATIVE={mono_pack.native_dir.as_posix()}",
        f"-DINDEX_SDL3_DIR={sdl3.as_posix()}",
        f"-DINDEX_DAWN_LIBRARY={dawn.library.as_posix()}",
        "-DINDEX_DAWN_INCLUDE_DIRS=" + ";".join(p.as_posix() for p in dawn.include_dirs),
        f"-DINDEX_CONFIGURATION={request.configuration}",
        f"-DINDEX_ENTITY_BITS={request.entity_bits}",
        *request.cmake_module_args(),
        "-DCMAKE_BUILD_TYPE=" + ("Release" if request.is_release else "RelWithDebInfo"),
    ]
    run(cmake_tc.configure_command(cmake, PACKAGE_ROOT / "Native", build_dir, args), label="CMake configure (libmain)")
    run([str(cmake), "--build", str(build_dir), "--target", "main", "--parallel"], label="Compile engine (libmain.so)")
    lib = build_dir / "libmain.so"
    if not lib.is_file():
        raise BuilderError(f"libmain.so was not produced in {build_dir}")
    return lib


def _build_dex(tc: android_toolchain.AndroidToolchain, engine_root: Path) -> Path:
    sources = sorted((engine_root / "External" / "SDL" / "android-project" / "app" / "src" / "main" / "java").rglob("*.java"))
    sources += sorted((PACKAGE_ROOT / "Java").rglob("*.java"))
    stamp = hashlib.sha256(b"".join(p.read_bytes() for p in sources) + str(tc.platform_jar).encode()).hexdigest()[:16]
    out = platform_cache("Android", "java", stamp)
    dex = out / "classes.dex"
    if dex.is_file():
        return dex
    classes = out / "classes"
    shutil.rmtree(classes, ignore_errors=True)
    classes.mkdir(parents=True)
    run([str(tc.javac), "-nowarn", "-encoding", "UTF-8", "-source", "8", "-target", "8",
         "-bootclasspath", str(tc.platform_jar), "-classpath", str(tc.platform_jar),
         "-d", str(classes)] + [str(s) for s in sources], label="javac")
    class_files = [str(p) for p in classes.rglob("*.class")]
    run([str(tc.d8), "--release", "--min-api", str(android_toolchain.MIN_SDK), "--lib", str(tc.platform_jar),
         "--output", str(out)] + class_files, env={"JAVA_HOME": str(tc.jdk)}, label="d8")
    if not dex.is_file():
        raise BuilderError("d8 did not produce classes.dex")
    return dex


def _stage_payload(request: BuildRequest, mono_pack, assets_dir: Path) -> None:
    """assets/payload/{game,managed}; _write_payload_listing finishes it once everything is in place."""
    payload = assets_dir / "payload"
    shutil.rmtree(assets_dir, ignore_errors=True)
    shutil.copytree(request.content_dir, payload / "game", dirs_exist_ok=True)
    managed = payload / "managed"
    managed.mkdir(parents=True, exist_ok=True)
    for assembly in mono_pack.framework_assemblies():
        shutil.copy2(assembly, managed / assembly.name)
    for assembly in request.content_dir.glob("Index-ScriptCore.dll"):
        shutil.copy2(assembly, managed / assembly.name)


def _aot_enabled(request: BuildRequest) -> bool:
    value = request.option("aot").strip().lower()
    if value in ("1", "true", "yes", "on"):
        return True
    if value in ("0", "false", "no", "off"):
        return False
    return request.is_release


def _precompile_scripts(request: BuildRequest, tc: android_toolchain.AndroidToolchain, assets_dir: Path,
                        work: Path) -> dict[str, Path]:
    """Mono AOT for the game's assemblies, Index-ScriptCore and the framework parts they use directly.

    Returns the images to ship as native libraries ({"libaot-X.dll.so": path}) and lists the covered
    assemblies in managed/index-aot.txt, which tells the Mono backend to load them.
    """
    import mono_aot  # noqa: E402
    import mono_runtime  # noqa: E402

    payload = assets_dir / "payload"
    managed = payload / "managed"
    game_assemblies = sorted(p for p in (payload / "game" / "bin").rglob("*.dll") if p.name != "Index-ScriptCore.dll") \
        if (payload / "game" / "bin").is_dir() else []
    if not game_assemblies or not (managed / "Index-ScriptCore.dll").is_file():
        emit_log("AOT skipped: the project has no C# scripts.")
        return {}

    # One flat copy of exactly what ships, so references bind to the bytes the device loads.
    inputs = work / "aot-input"
    shutil.rmtree(inputs, ignore_errors=True)
    inputs.mkdir(parents=True)
    for assembly in [*managed.glob("*.dll"), *game_assemblies]:
        shutil.copy2(assembly, inputs / assembly.name)

    clang = next((p for name in ("clang.exe", "clang") for p in tc.ndk.glob(f"toolchains/llvm/prebuilt/*/bin/{name}")), None)
    strip = next(tc.ndk.glob("toolchains/llvm/prebuilt/*/bin/llvm-strip*"), None)
    if not clang or not strip:
        raise BuilderError(f"clang / llvm-strip not found in the NDK ({tc.ndk}); needed for AOT.")
    target = f"--target=aarch64-linux-android{android_toolchain.MIN_SDK}"
    # --no-relax: lld would turn the PLT entries' adrp+add into nop+adr, and Mono decodes and patches
    # exactly that instruction sequence at runtime. 16 KB pages: Android 15+ devices may use them.
    link_flags = ["-shared", "-Wl,-Bsymbolic", "-Wl,--no-relax", "-Wl,-z,max-page-size=16384"]

    def link(objects: list[Path], output: Path) -> None:
        linked = []
        for source in objects:
            if source.suffix == ".s":
                obj = source.with_suffix(".o")
                run([clang, target, "-c", "-o", obj, source], label=f"assemble {source.name}")
                linked.append(obj)
            else:
                linked.append(source)
        run([clang, target, *link_flags, f"-Wl,-soname,{output.name}", "-o", output, *linked, "-lm"],
            label=f"link {output.name}")
        run([strip, "--strip-unneeded", output], label=f"strip {output.name}")

    llvm = request.option("aotLlvm", "true").strip().lower() not in ("0", "false", "no", "off")
    images = mono_aot.precompile(
        inputs, [p.stem for p in game_assemblies] + ["Index-ScriptCore"], mono_runtime.fetch_aot_compiler(RID),
        triple="aarch64-linux-android", llvm=llvm, link=link,
        image_name=lambda assembly: f"libaot-{assembly}.so", cache_dir=platform_cache("Android", "aot"),
        link_key=" ".join([target, *link_flags]))
    shutil.rmtree(inputs, ignore_errors=True)
    (managed / "index-aot.txt").write_text("".join(f"{info.path.name}\n" for info, _ in images), encoding="utf-8")
    return {image.name: image for _, image in images}


def _write_payload_listing(assets_dir: Path) -> None:
    """assets/index-payload.txt: payload hash, then every file; the player re-extracts when the hash changes."""
    payload = assets_dir / "payload"
    files = sorted(p for p in payload.rglob("*") if p.is_file())
    digest = hashlib.sha256()
    listing = []
    for file in files:
        relative = file.relative_to(payload).as_posix()
        listing.append(relative)
        digest.update(relative.encode())
        digest.update(file.read_bytes())
    (assets_dir / "index-payload.txt").write_text(digest.hexdigest() + "\n" + "\n".join(listing) + "\n", encoding="utf-8")


def _debug_keystore(tc: android_toolchain.AndroidToolchain) -> tuple[Path, str, str, str]:
    keystore = platform_cache("Android") / "debug.keystore"
    if not keystore.is_file():
        run([str(tc.keytool), "-genkeypair", "-keystore", str(keystore), "-storepass", "android",
             "-alias", "androiddebugkey", "-keypass", "android", "-keyalg", "RSA", "-keysize", "2048",
             "-validity", "10000", "-dname", "CN=Index Debug,O=Index,C=US"], label="keytool")
    return keystore, "android", "androiddebugkey", "android"


def build(request: BuildRequest) -> Path:
    request.output_dir.mkdir(parents=True, exist_ok=True)
    work = platform_cache("Android", "work", hashlib.sha256(str(request.project_root.resolve()).encode()).hexdigest()[:10])
    set_log_file(work / "build.log")
    emit_log(f"Build log: {work / 'build.log'}")

    emit_progress(0.02, "Checking the Android toolchain")
    tc, issues = android_toolchain.resolve(request.options)
    if not tc:
        raise BuilderError("Android toolchain incomplete:\n" + "\n".join(f"- {i['message']} {i['fix']}" for i in issues))
    cmake_tc = _ndk_toolchain(tc)
    if not cmake_tc.make_program:
        raise BuilderError("Ninja not found (install the CMake package in the Android SDK Manager).")

    mono_package = _find_mono_package(request.engine_root)
    sys.path.insert(0, str(mono_package / "Builder"))
    import mono_runtime  # noqa: E402

    emit_progress(0.05, "SDL3 for Android")
    sdl3 = build_sdl3(request.engine_root, cmake_tc, shared=True)
    emit_progress(0.15, "Dawn (WebGPU) for Android")
    dawn = build_dawn(request.engine_root, cmake_tc)
    emit_progress(0.30, "Mono runtime pack")
    mono_pack = mono_runtime.fetch_runtime_pack(RID)

    emit_progress(0.35, "Compiling the engine for Android")
    libmain = _build_native(request, tc, cmake_tc, sdl3, dawn, mono_pack, mono_package)

    emit_progress(0.72, "Java glue (classes.dex)")
    dex = _build_dex(tc, request.engine_root)

    emit_progress(0.78, "Staging game payload")
    assets = work / "assets"
    _stage_payload(request, mono_pack, assets)
    aot_images = {}
    if _aot_enabled(request):
        emit_progress(0.80, "Precompiling C# (Mono AOT)")
        aot_images = _precompile_scripts(request, tc, assets, work)
    _write_payload_listing(assets)

    emit_progress(0.84, "Linking the APK")
    app_id = _app_id(request)
    manifest_text = (PACKAGE_ROOT / "Template" / "AndroidManifest.xml").read_text(encoding="utf-8")
    replacements = {
        "@APP_ID@": app_id,
        "@VERSION_CODE@": str(_version_code(request)),
        "@VERSION_NAME@": request.version,
        "@MIN_SDK@": str(android_toolchain.MIN_SDK),
        "@TARGET_SDK@": str(android_toolchain.TARGET_SDK),
        "@APP_NAME@": request.app_name.replace("&", "&amp;").replace('"', "&quot;").replace("<", "&lt;"),
        "@ORIENTATION@": request.option("orientation")
                         or ANDROID_SCREEN_ORIENTATION[project_orientation(request.content_dir)],
        "@PERMISSIONS@": _permission_lines(_permissions_option(request)),
        "@SERVICES@": _BACKGROUND_AUDIO_SERVICE if _background_audio(request) else "",
        "@ICON_ATTRIBUTE@": 'android:icon="@mipmap/ic_launcher"' if request.icon_path and request.icon_path.is_file() else "",
    }
    for key, value in replacements.items():
        manifest_text = manifest_text.replace(key, value)
    manifest = work / "AndroidManifest.xml"
    manifest.write_text(manifest_text, encoding="utf-8")

    link = [str(tc.aapt2), "link", "-o", str(work / "base.apk"), "-I", str(tc.platform_jar),
            "--manifest", str(manifest), "--auto-add-overlay"]
    if request.icon_path and request.icon_path.is_file():
        res = work / "res" / "mipmap"
        shutil.rmtree(work / "res", ignore_errors=True)
        res.mkdir(parents=True)
        shutil.copy2(request.icon_path, res / "ic_launcher.png")
        flat = work / "res.zip"
        run([str(tc.aapt2), "compile", "--dir", str(work / "res"), "-o", str(flat)], label="aapt2 compile")
        link.append(str(flat))
    run(link, label="aapt2 link")

    unsigned = work / "unsigned.apk"
    shutil.copy2(work / "base.apk", unsigned)
    strip = next(tc.ndk.glob("toolchains/llvm/prebuilt/*/bin/llvm-strip*"), None)
    stripped = work / "libmain.so"
    shutil.copy2(libmain, stripped)
    if strip:
        # Development keeps the symbol table so native crashes still show function names.
        run([str(strip), "--strip-unneeded" if request.is_release else "--strip-debug", str(stripped)], label="llvm-strip")
    native_libs = {
        "libmain.so": stripped,
        "libSDL3.so": sdl3 / "lib" / "libSDL3.so",
    }
    for name in ("libmonosgen-2.0.so", "libSystem.Native.so", "libSystem.IO.Compression.Native.so",
                 "libSystem.Globalization.Native.so", "libmono-component-marshal-ilgen.so"):
        path = mono_pack.native_dir / name
        if path.is_file():
            native_libs[name] = path
    native_libs.update(aot_images)
    with zipfile.ZipFile(unsigned, "a", compression=zipfile.ZIP_DEFLATED) as apk:
        apk.write(dex, "classes.dex")
        for name, path in native_libs.items():
            apk.write(path, f"lib/{ABI}/{name}")
        # Added here rather than with aapt2 -A, which silently drops dot-files such as .index/ that the
        # payload listing requires.
        for file in sorted(p for p in assets.rglob("*") if p.is_file()):
            apk.write(file, "assets/" + file.relative_to(assets).as_posix())

    emit_progress(0.92, "Aligning and signing")
    aligned = work / "aligned.apk"
    run([str(tc.zipalign), "-p", "-f", "4", str(unsigned), str(aligned)], label="zipalign")
    keystore_option = request.option("keystore")
    if keystore_option:
        keystore, store_pass = Path(keystore_option), request.option("keystorePassword")
        alias, key_pass = request.option("keyAlias"), request.option("keyPassword", store_pass)
        if not keystore.is_file():
            raise BuilderError(f"Keystore not found: {keystore}")
    else:
        if request.is_release:
            emit_log("Release build signed with the debug key; set a keystore in the build options before publishing.", "warn")
        keystore, store_pass, alias, key_pass = _debug_keystore(tc)
    output = request.output_dir / f"{re.sub(r'[^A-Za-z0-9._-]', '_', request.app_name)}.apk"
    # Passwords go through the environment: command lines end up in the build log and process lists.
    run([str(tc.apksigner), "sign", "--ks", str(keystore), "--ks-pass", "env:INDEX_APK_STORE_PASS",
         "--ks-key-alias", alias, "--key-pass", "env:INDEX_APK_KEY_PASS", "--out", str(output), str(aligned)],
        env={"JAVA_HOME": str(tc.jdk), "PATH": str(tc.jdk / "bin") + os.pathsep + os.environ.get("PATH", ""),
             "INDEX_APK_STORE_PASS": store_pass, "INDEX_APK_KEY_PASS": key_pass},
        label="apksigner")
    emit_log(f"APK: {output} ({output.stat().st_size // (1024 * 1024)} MiB, {app_id})")
    return output


if __name__ == "__main__":
    sys.exit(run_builder(describe, check, build))
