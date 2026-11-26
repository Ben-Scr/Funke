"""Build the editor's prepared Bolt modules with the target compiler toolchain."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

from .protocol import BuildRequest, BuilderError
from .tools import run


def _string(value: str) -> str:
    # Octal UTF-8 escapes also preserve non-ASCII field names across C++ toolchains.
    return '"' + ''.join(chr(b) if 32 <= b < 127 and chr(b) not in '"\\'
                         else f'\\{b:03o}' for b in value.encode('utf-8')) + '"'


def _field(values: list) -> str:
    if len(values) != 8 or any(type(x) is not (bool if i in (5, 7) else str)
                               for i, x in enumerate(values)):
        raise BuilderError("Invalid Bolt field schema supplied by the editor")
    return '{' + ', '.join(str(x).lower() if type(x) is bool else _string(x) for x in values) + '}'


def _registry(modules: list[tuple[str, list]]) -> str:
    lines = [f'extern "C" int FunkeBoltRun{key}();' for key, _ in modules]
    lines.append('static const AotModule* FindAotModule(std::string_view id) {')
    lines.append('    static const AotModule modules[] = {')
    for key, fields in modules:
        schema = ', '.join(_field(f) for f in fields)
        lines.append(f'        {{"{key}", {key}ULL, FunkeBoltRun{key}, {{{schema}}}}},')
    lines += ['    };', '    for (const auto& module : modules) if (module.Identity == id) return &module;',
              '    return nullptr;', '}', '']
    return '\n'.join(lines)


def _mangle(name: str) -> str:
    return ''.join(chr(b) if chr(b).isascii() and chr(b).isalnum() else f'_x{b:02x}_'
                   for b in name.encode('utf-8'))


def _layout_checks(layouts: list[dict]) -> str:
    checks = []
    for layout in layouts:
        cpp = 'bolt_generated::bC_' + _mangle(layout['name'])
        checks += [f'static_assert(sizeof({cpp}) == {int(layout["size"])});',
                   f'static_assert(alignof({cpp}) == {int(layout["alignment"])});']
        for field in layout['fields']:
            member = '.'.join('bF_' + _mangle(part) for part in field['name'].split('.'))
            checks += [f'static_assert(offsetof({cpp}, {member}) == {int(field["offset"])});',
                       f'static_assert(sizeof((({cpp}*)nullptr)->{member}) == {int(field["size"])});']
    return '\n'.join(checks) + '\n'


def _layout_registry(layouts: dict) -> str:
    lines = ['static bool InstallAotLayouts(std::string& error) {',
             '    std::vector<NativeStructLayout> layouts;']
    for name, layout in sorted(layouts.items()):
        if layout['managed']:
            continue
        fields = _string(json.dumps(layout['fields'], separators=(',', ':')))
        lines += ['    {', '        NativeStructLayout row;',
                  f'        row.Name = {_string(name)};',
                  f'        row.Size = {int(layout["size"])};',
                  f'        row.Alignment = {int(layout["alignment"])};',
                  f'        if (!Json::TryParse({fields}, row.Fields, &error)) return false;',
                  '        layouts.push_back(std::move(row));', '    }']
    return '\n'.join(lines + ['    return InstallNativeStructs(layouts, error);', '}', ''])


def _bindings(names: list[str]) -> str:
    lines = []
    for i, name in enumerate(names):
        if not re.fullmatch('[A-Za-z_][A-Za-z_0-9]*', name):
            raise BuilderError("Invalid Bolt host binding name")
        lines += [f'static_assert(std::string_view(s_Bindings[{i}].name) == "{name}");',
                  f'extern "C" uint64_t {name}(uint64_t a, uint64_t b) {{',
                  f'    return s_Bindings[{i}].function(a, b);', '}']
    return '\n'.join(lines) + '\n'


def prepare_bolt(request: BuildRequest, work: Path, target: str) -> list[str]:
    root = request.output_dir / '.funke-bolt'
    if not root.is_dir():
        # Explicitly clear a cached configuration when the last Bolt script is removed.
        return ['-DFUNKE_BOLT_DIR=']
    compiler = Path(json.loads((root / 'compiler.json').read_text(encoding='utf-8')))
    if not compiler.is_file():
        raise BuilderError(f"Bolt AOT requires the host SDK emitter at {compiler}")
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=work) as temp:
        stage = Path(temp)
        modules = []
        schemas = {}
        for project in sorted(p for p in root.iterdir() if p.is_dir()):
            key = project.name
            if not re.fullmatch('[1-9][0-9]{0,19}', key) or int(key) >= 2**64:
                raise BuilderError(f'Invalid Bolt module GUID {key}')
            meta = json.loads((project / 'module.json').read_text(encoding='utf-8'))
            output = stage / f'Bolt{key}.cpp'
            run([compiler, 'emit', project, '--target', target,
                 '--release' if request.is_release else '--debug', '-o', output],
                label=f"Bolt emitter for {meta['asset']} ({target})")
            # Separate namespaces and entry points allow independent persistent script statics.
            unit = output.read_text(encoding='utf-8')
            prefix = f'#define bolt_generated FunkeBoltGenerated{key}\n'
            if target == 'web':
                prefix += f'#define main FunkeBoltMain{key}\n'
                call = f'FunkeBoltMain{key}'
                suffix = ('\n#undef main\ntemplate<class F> static int InvokeBolt(F f) {\n'
                          '    if constexpr (std::is_invocable_v<F>) return f();\n'
                          '    else return f(0, nullptr);\n}\n')
                call = f'InvokeBolt({call})'
            else:
                prefix += f'#define BOLT_MOBILE_ENTRY FunkeBoltMain{key}\n'
                suffix = '\n'
                call = f'FunkeBoltMain{key}(0, nullptr)'
            layout_path = project / 'layouts.json'
            layouts = json.loads(layout_path.read_text(encoding='utf-8')) if layout_path.exists() else []
            for layout in layouts:
                if schemas.setdefault(layout['name'], layout) != layout:
                    raise BuilderError(f"Conflicting Bolt layouts for {layout['name']}")
            output.write_text(prefix + unit + '\n' + _layout_checks(layouts) + suffix + f'extern "C" int FunkeBoltRun{key}() {{ return {call}; }}\n',
                              encoding='utf-8')
            modules.append((key, meta['fields']))
        if not modules:
            raise BuilderError('The Bolt AOT stage contains no script modules')
        (stage / 'FunkeBoltModules.inc').write_text(_registry(modules) + _layout_registry(schemas), encoding='utf-8')
        (stage / 'FunkeBoltBindings.inc').write_text(_bindings(json.loads(
            (root / 'bindings.json').read_text(encoding='utf-8'))), encoding='utf-8')
        # Xcode projects and incremental CMake builds retain these files after staging is published.
        digest = hashlib.sha256()
        for file in sorted(stage.iterdir()):
            digest.update(file.name.encode())
            digest.update(hashlib.sha256(file.read_bytes()).digest())
        destination = work / digest.hexdigest()
        if not destination.exists():
            try:
                stage.rename(destination)
            except OSError:
                if not destination.is_dir():
                    raise

    sdk_var, archive = {
        'web': ('BOLT_WEB_SDK', 'lib/libBoltInterpreter.a'),
        'android-arm64': ('BOLT_ANDROID_SDK', 'lib/arm64-v8a/libBoltInterpreter.a'),
        'ios-arm64': ('BOLT_IOS_SDK', 'BoltInterpreter.xcframework/ios-arm64/libBoltInterpreter.a'),
    }[target]
    sdk = Path(os.environ.get(sdk_var, '')).resolve()
    runtime = sdk / archive
    headers = sdk / 'include'
    diagnostics = sdk / 'src/Diagnostics.cpp'
    if not os.environ.get(sdk_var) or not (headers / 'bolt/Version.hpp').is_file():
        raise BuilderError(f'Set {sdk_var} to the matching Bolt target SDK or source tree')
    if runtime.is_file():
        support = [f'-DFUNKE_BOLT_RUNTIME={runtime.as_posix()}', '-DFUNKE_BOLT_DIAGNOSTICS=']
    elif diagnostics.is_file():
        support = ['-DFUNKE_BOLT_RUNTIME=', f'-DFUNKE_BOLT_DIAGNOSTICS={diagnostics.as_posix()}']
    elif (headers / 'bolt/detail/DiagnosticsImplementation.inc').is_file():
        support = ['-DFUNKE_BOLT_RUNTIME=', '-DFUNKE_BOLT_DIAGNOSTICS=embedded']
    else:
        raise BuilderError(f'{sdk_var} must supply {archive} or the Bolt diagnostics implementation')
    notices = request.content_dir / 'Licenses' / 'Bolt' / target
    for name in ('LICENSE', 'THIRD_PARTY_NOTICES.md', 'licenses'):
        source = sdk / name
        if source.is_dir():
            shutil.copytree(source, notices / name, dirs_exist_ok=True)
        elif source.is_file():
            notices.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, notices / name)
    return [f'-DFUNKE_BOLT_DIR={destination.as_posix()}', f'-DFUNKE_BOLT_INCLUDE={sdk.as_posix()}/include',
            *support]
