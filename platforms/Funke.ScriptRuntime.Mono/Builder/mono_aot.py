"""Ahead-of-time compilation of managed assemblies with Mono's cross compiler.

Used as a library by Funke.Platform.* builders:

    from mono_aot import precompile
    images = precompile(assembly_dir, ["MyGame", "Funke-ScriptCore"], compiler_dir, triple="aarch64-linux-android",
                        llvm=True, link=..., image_name=..., cache_dir=...)

Compiled are the roots (game assemblies, Funke-ScriptCore) and the framework assemblies they reference
directly, following type-forwarding facades such as System.Runtime through to System.Private.CoreLib.
Deeper dependencies (networking, cryptography, ...) are rarely hot and stay JIT compiled; each image
costs APK size. Facades without code are skipped. Each image is cached under a key over the assembly,
everything it references and the compiler, because Mono rejects an image whose dependencies changed.
"""

from __future__ import annotations

import concurrent.futures
import hashlib
import os
import shutil
import struct
import time
from pathlib import Path
from typing import Callable

from funke_platform_sdk import BuilderError, emit_log, run

# Bump when the compile or link recipe changes so cached images are rebuilt.
_RECIPE = "1"


class AssemblyInfo:
    """The bits of ECMA-335 metadata the AOT step needs: referenced assembly names and whether there is code."""

    def __init__(self, path: Path):
        self.path = path
        self.name = path.stem
        self.references: list[str] = []
        self.method_count = 0
        self._read(path.read_bytes())

    def _read(self, data: bytes) -> None:
        try:
            pe = struct.unpack_from("<I", data, 0x3C)[0]
            sections, optional_size = struct.unpack_from("<H", data, pe + 6)[0], struct.unpack_from("<H", data, pe + 20)[0]
            optional = pe + 24
            directories = optional + (112 if struct.unpack_from("<H", data, optional)[0] == 0x20B else 96)
            cli_rva = struct.unpack_from("<I", data, directories + 14 * 8)[0]
            section_table = optional + optional_size
            spans = [struct.unpack_from("<IIII", data, section_table + i * 40 + 8) for i in range(sections)]

            def offset(rva: int) -> int:
                for virtual_size, address, raw_size, raw in spans:
                    if address <= rva < address + max(virtual_size, raw_size):
                        return rva - address + raw
                raise ValueError(f"RVA {rva:#x} outside every section")

            root = offset(struct.unpack_from("<I", data, offset(cli_rva) + 8)[0])
            version_length = struct.unpack_from("<I", data, root + 12)[0]
            cursor = root + 16 + version_length + 2
            stream_count = struct.unpack_from("<H", data, cursor)[0]
            cursor += 2
            streams = {}
            for _ in range(stream_count):
                stream_offset, size = struct.unpack_from("<II", data, cursor)
                end = data.index(b"\0", cursor + 8)
                streams[data[cursor + 8:end].decode()] = (root + stream_offset, size)
                cursor = (end + 4) & ~3
            self._read_tables(data, streams)
        except (struct.error, ValueError, KeyError) as error:
            raise BuilderError(f"{self.path.name} is not a readable .NET assembly: {error}") from error

    def _read_tables(self, data: bytes, streams: dict) -> None:
        tables, _ = streams.get("#~") or streams["#-"]
        strings, _ = streams["#Strings"]
        heap_sizes = data[tables + 6]
        valid = struct.unpack_from("<Q", data, tables + 8)[0]
        rows = [0] * 64
        cursor = tables + 24
        for table in range(64):
            if valid >> table & 1:
                rows[table] = struct.unpack_from("<I", data, cursor)[0]
                cursor += 4

        string_index = 4 if heap_sizes & 1 else 2
        guid_index = 4 if heap_sizes & 2 else 2
        blob_index = 4 if heap_sizes & 4 else 2

        def index(table: int) -> int:
            return 2 if rows[table] < 1 << 16 else 4

        def coded(tag_bits: int, *targets: int) -> int:
            return 2 if max(rows[t] for t in targets) < 1 << (16 - tag_bits) else 4

        type_def_or_ref = coded(2, 0x02, 0x01, 0x1B)
        has_custom_attribute = coded(5, 0x06, 0x04, 0x01, 0x02, 0x08, 0x09, 0x0A, 0x00, 0x0E, 0x17, 0x14, 0x11,
                                     0x1A, 0x1B, 0x20, 0x23, 0x26, 0x27, 0x28, 0x2A, 0x2C, 0x2B)
        method_def_or_ref = coded(1, 0x06, 0x0A)
        # Row sizes of every table stored before AssemblyRef (0x23), in table order.
        sizes = [
            2 + string_index + 3 * guid_index,                                  # Module
            coded(2, 0x00, 0x1A, 0x23, 0x01) + 2 * string_index,                # TypeRef
            4 + 2 * string_index + type_def_or_ref + index(0x04) + index(0x06),  # TypeDef
            index(0x04),                                                        # FieldPtr
            2 + string_index + blob_index,                                      # Field
            index(0x06),                                                        # MethodPtr
            8 + string_index + blob_index + index(0x08),                        # MethodDef
            index(0x08),                                                        # ParamPtr
            4 + string_index,                                                   # Param
            index(0x02) + type_def_or_ref,                                      # InterfaceImpl
            coded(3, 0x02, 0x01, 0x1A, 0x06, 0x1B) + string_index + blob_index,  # MemberRef
            2 + coded(2, 0x04, 0x08, 0x17) + blob_index,                        # Constant
            has_custom_attribute + coded(3, 0x06, 0x0A) + blob_index,           # CustomAttribute
            coded(1, 0x04, 0x08) + blob_index,                                  # FieldMarshal
            2 + coded(2, 0x02, 0x06, 0x20) + blob_index,                        # DeclSecurity
            6 + index(0x02),                                                    # ClassLayout
            4 + index(0x04),                                                    # FieldLayout
            blob_index,                                                         # StandAloneSig
            index(0x02) + index(0x14),                                          # EventMap
            index(0x14),                                                        # EventPtr
            2 + string_index + type_def_or_ref,                                 # Event
            index(0x02) + index(0x17),                                          # PropertyMap
            index(0x17),                                                        # PropertyPtr
            2 + string_index + blob_index,                                      # Property
            2 + index(0x06) + coded(1, 0x14, 0x17),                             # MethodSemantics
            index(0x02) + 2 * method_def_or_ref,                                # MethodImpl
            string_index,                                                       # ModuleRef
            blob_index,                                                         # TypeSpec
            2 + coded(1, 0x04, 0x06) + string_index + index(0x1A),              # ImplMap
            4 + index(0x04),                                                    # FieldRVA
            8,                                                                  # ENCLog
            4,                                                                  # ENCMap
            16 + blob_index + 2 * string_index,                                 # Assembly
            4,                                                                  # AssemblyProcessor
            12,                                                                 # AssemblyOS
        ]
        assembly_ref = cursor + sum(size * rows[table] for table, size in enumerate(sizes))
        row_size = 12 + 2 * blob_index + 2 * string_index
        name_offset = 12 + blob_index

        def string_at(position: int) -> str:
            start = strings + (struct.unpack_from("<I" if string_index == 4 else "<H", data, position)[0])
            return data[start:data.index(b"\0", start)].decode("utf-8")

        self.references = [string_at(assembly_ref + row * row_size + name_offset) for row in range(rows[0x23])]
        self.method_count = rows[0x06]


def reference_closure(roots: list[Path], available: dict[str, Path]) -> list[AssemblyInfo]:
    """roots plus the assemblies in `available` (name -> path) referenced by a root or by a reached facade."""
    infos: dict[str, AssemblyInfo] = {}
    root_names = {path.stem for path in roots}
    pending = list(roots)
    while pending:
        path = pending.pop()
        if path.stem in infos:
            continue
        info = AssemblyInfo(path)
        infos[info.name] = info
        if info.name in root_names or info.method_count == 0:
            pending += [available[name] for name in info.references if name in available and name not in infos]
    return list(infos.values())


def _sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def precompile(assembly_dir: Path, roots: list[str], compiler_dir: Path, triple: str, llvm: bool,
               link: Callable[[list[Path], Path], None], image_name: Callable[[str], str], cache_dir: Path,
               link_key: str = "") -> list[tuple[AssemblyInfo, Path]]:
    """AOT-compiles roots (assembly names) and their references and returns (assembly, image) pairs.

    assembly_dir holds every assembly exactly as it ships, in one flat directory, so the compiler binds
    references to the same bytes the device loads. link(objects, output) turns the compiler's
    assembly/object output into the platform's image format; image_name(assembly file name) names it;
    link_key describes the link recipe, so changing it rebuilds the cache.
    """
    compiler = next((compiler_dir / name for name in ("mono-aot-cross.exe", "mono-aot-cross")
                     if (compiler_dir / name).is_file()), None)
    if not compiler:
        raise BuilderError(f"mono-aot-cross not found in {compiler_dir}")
    if llvm and not any((compiler_dir / name).is_file() for name in ("llc.exe", "llc")):
        raise BuilderError(f"LLVM (llc/opt) not found next to {compiler}")

    available = {path.stem: path for path in assembly_dir.glob("*.dll")}
    missing = [name for name in roots if name not in available]
    if missing:
        raise BuilderError(f"AOT: {', '.join(missing)} not found in {assembly_dir}")
    closure = reference_closure([available[name] for name in roots], available)
    infos = {info.name: info for info in closure}
    hashes: dict[str, str] = {}
    compiler_hash = _sha256(compiler)

    def content_hash(name: str) -> str:
        if name not in hashes:
            hashes[name] = _sha256(available[name])
        return hashes[name]

    def dependencies(info: AssemblyInfo) -> list[str]:
        # Inlined code can come from any assembly reachable from this one, not just the compiled set.
        seen, stack = set(), list(info.references)
        while stack:
            name = stack.pop()
            if name in available and name not in seen:
                seen.add(name)
                if name not in infos:
                    infos[name] = AssemblyInfo(available[name])
                stack += infos[name].references
        return sorted(seen)

    compiled = [info for info in closure if info.method_count > 0]
    keys = {info.name: hashlib.sha256("\n".join(
        [_RECIPE, link_key, compiler_hash, triple, str(llvm), content_hash(info.name)]
        + [f"{name}:{content_hash(name)}" for name in dependencies(info)]).encode()).hexdigest()[:20]
        for info in compiled}

    def compile_one(info: AssemblyInfo) -> tuple[AssemblyInfo, Path, bool]:
        key = keys[info.name]
        out_dir = cache_dir / f"{info.name}-{key}"
        image = out_dir / image_name(info.path.name)
        if image.is_file():
            return info, image, True
        work = out_dir / "obj"
        shutil.rmtree(out_dir, ignore_errors=True)
        work.mkdir(parents=True)
        asm = work / f"{info.path.name}.s"
        options = [f"mtriple={triple}", "asmonly", "nodebug", f"outfile={asm}", f"temp-path={work}"]
        objects = [asm]
        if llvm:
            llvm_object = work / f"{info.path.name}-llvm.o"
            options += [f"llvm-path={compiler_dir}", f"llvm-outfile={llvm_object}"]
            objects.append(llvm_object)
        run([compiler, *(["--llvm"] if llvm else []), "--aot=" + ",".join(options), info.path],
            env={"MONO_PATH": str(assembly_dir)}, label=f"mono-aot-cross {info.path.name}")
        partial = work / image.name
        link(objects, partial)
        partial.replace(image)
        shutil.rmtree(work, ignore_errors=True)
        return info, image, False

    started = time.monotonic()
    # CoreLib dominates; start it first so the small assemblies fill the remaining workers.
    compiled.sort(key=lambda info: info.path.stat().st_size, reverse=True)
    results = []
    rebuilt = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, min(len(compiled), os.cpu_count() or 4))) as pool:
        for info, image, cached in pool.map(compile_one, compiled):
            results.append((info, image))
            rebuilt += not cached
    emit_log(f"AOT: {len(results)} assemblies precompiled{' with LLVM' if llvm else ''}, {rebuilt} of them rebuilt "
             f"({len(closure) - len(compiled)} facades skipped), {time.monotonic() - started:.1f} s")
    return results
