"""Incremental experimental build of the original C++ editor for the browser.

Uses the installed Web player toolchain and its SDL/Dawn caches. No Premake regeneration,
desktop build or cache deletion. Generated files stay in output/web-editor/.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
spec = importlib.util.spec_from_file_location('funke_web_player_builder', HERE.parent / 'Builder/build.py')
web = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', default='index', choices=['index', 'FunkeWebEditorObjects'])
    parser.add_argument('--jobs', type=int, default=6)
    parser.add_argument('--shell-only', action='store_true', help='Update only the HTML host around an existing successful Web build.')
    args = parser.parse_args()
    work = ROOT / 'output/web-editor'
    if args.shell_only:
        site = work / 'site'
        for name in ['index.js', 'index.wasm', 'index.data']:
            if not (site / name).is_file():
                raise RuntimeError('A successful editor build is required before --shell-only.')
        html = (HERE / 'shell.html').read_text(encoding='utf-8')
        (site / 'index.html').write_text(html.replace('{{{ SCRIPT }}}', '<script async src="index.js"></script>'), encoding='utf-8')
        print(f'Updated HTML host: {site / "index.html"}')
        return
    content = work / 'content'
    content.mkdir(parents=True, exist_ok=True)
    baseline = work / 'baseline-status.txt'
    if not baseline.exists():
        baseline.write_bytes(subprocess.check_output(['git', '-C', str(ROOT), 'status', '--porcelain=v1']))
    # Ship the existing editor icons, fonts, font licenses and shaders unchanged.
    runtime_assets = ROOT / 'Funke-Runtime/FunkeAssets'
    for name in ['Textures', 'Shaders', 'Fonts']:
        source = runtime_assets / name
        if source.exists():
            shutil.copytree(source, content / 'FunkeAssets' / name, dirs_exist_ok=True)
    # The original menu layout and font licenses ship with the editor assets.
    emsdk = web.find_emsdk(ROOT)
    ninja = web.find_ninja()
    if not emsdk or not ninja:
        raise RuntimeError('Installed Emscripten SDK and Ninja are required.')
    toolchain = web._toolchain(emsdk, ninja)
    sdl = web.build_sdl3(ROOT, toolchain, shared=False)
    dawn = web._build_emdawnwebgpu(ROOT, toolchain)
    cmake = web.find_cmake(ROOT)
    build = work / 'build'
    options = [f'-DFUNKE_ROOT={ROOT.as_posix()}', f'-DFUNKE_SDL3_DIR={sdl.as_posix()}',
               f'-DFUNKE_EMDAWNWEBGPU_PORT={(dawn / "emdawnwebgpu.port.py").as_posix()}',
               '-DFUNKE_WEBGPU_INCLUDE_DIRS=' + ';'.join((dawn / path).as_posix() for path in ['webgpu/include', 'webgpu_cpp/include']),
               f'-DFUNKE_EDITOR_CONTENT={content.as_posix()}', '-DCMAKE_BUILD_TYPE=Release',
               '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -DNDEBUG', '-DCMAKE_C_FLAGS_RELEASE=-O1 -DNDEBUG',
               '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON']
    web.run(toolchain.configure_command(cmake, HERE, build, options), env=toolchain.env, label='Configure Web editor')
    web.run([str(cmake), '--build', str(build), '--target', args.target, '--parallel', str(args.jobs)], env=toolchain.env, label='Build Web editor')
    if args.target == 'index':
        site = work / 'site'
        site.mkdir(exist_ok=True)
        for name in ['index.html', 'index.js', 'index.wasm', 'index.data']:
            shutil.copy2(build / name, site / name)
        shutil.copy2(runtime_assets / 'Textures/icon.png', site / 'icon.png')
        print(f'Web editor: {site / "index.html"}', flush=True)

if __name__ == '__main__':
    main()
