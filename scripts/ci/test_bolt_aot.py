"""Tests for preparing Bolt modules in a platform package build."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "platform"))
from funke_platform_sdk import bolt_aot
from funke_platform_sdk.protocol import BuildRequest, BuilderError


class BoltAotTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.request = BuildRequest(self.root, self.root, 'Game', self.root / 'content', self.root,
                                    'Development', 'Game', '', '', '', None, 20)
        self.stage = self.root / '.funke-bolt'
        self.work = self.root / 'work'

    def stage_module(self, guid):
        module = self.stage / guid
        module.mkdir(parents=True)
        emitter = self.root / 'bolt'
        emitter.touch()
        (self.stage / 'compiler.json').write_text(json.dumps(str(emitter)))
        (self.stage / 'bindings.json').write_text('["FkEntityId"]')
        (module / 'module.json').write_text(json.dumps({'asset': 'Move.bolt', 'fields': [
            ['Speed', 'float', 'Speed', '1', 'a "quoted" tooltip', False, '', False]]}))

    def test_removing_last_script_clears_cmake_cache_option(self):
        self.assertEqual(bolt_aot.prepare_bolt(self.request, self.work, 'web'), ['-DFUNKE_BOLT_DIR='])

    def test_emitter_failure_never_publishes_an_incomplete_set(self):
        self.stage_module('11')
        with patch.object(bolt_aot, 'run', side_effect=BuilderError('emitter failed')):
            with self.assertRaisesRegex(BuilderError, 'emitter failed'):
                bolt_aot.prepare_bolt(self.request, self.work, 'web')
        self.assertEqual(list(self.work.iterdir()), [])

    def test_link_two_distinct_script_entry_points_and_reuse_identical_output(self):
        self.stage_module('11')
        self.stage_module('12')
        sdk = self.root / 'sdk'
        for file in ('lib/libBoltInterpreter.a', 'include/bolt/Version.hpp'):
            path = sdk / file
            path.parent.mkdir(parents=True)
            path.touch()
        def emit(cmd, label):
            self.assertEqual(cmd[3:6], ['--target', 'web', '--debug'])
            Path(cmd[-1]).write_text('int main() {return 0;}\n')
        with patch.object(bolt_aot, 'run', side_effect=emit), patch.dict(
                bolt_aot.os.environ, BOLT_WEB_SDK=str(sdk)):
            args = bolt_aot.prepare_bolt(self.request, self.work, 'web')
            self.assertEqual(args, bolt_aot.prepare_bolt(self.request, self.work, 'web'))
        generated = Path(args[0].split('=', 1)[1])
        for guid in ('11', '12'):
            module = (generated / f'Bolt{guid}.cpp').read_text()
            self.assertIn(f'#define bolt_generated FunkeBoltGenerated{guid}', module)
            self.assertIn(f'extern "C" int FunkeBoltRun{guid}()', module)
            self.assertIn(f'FunkeBoltRun{guid}', (generated / 'FunkeBoltModules.inc').read_text())
        self.assertIn('FkEntityId', (generated / 'FunkeBoltBindings.inc').read_text())

    def test_source_sdk_compiles_diagnostics_with_the_player_toolchain(self):
        self.stage_module('21')
        sdk = self.root / 'sdk'
        diagnostics = sdk / 'src/Diagnostics.cpp'
        for file in (sdk / 'include/bolt/Version.hpp', diagnostics):
            file.parent.mkdir(parents=True)
            file.touch()
        with (patch.object(bolt_aot, 'run', side_effect=lambda cmd, label: Path(cmd[-1]).write_text('')),
              patch.dict(bolt_aot.os.environ, BOLT_ANDROID_SDK=str(sdk))):
            args = bolt_aot.prepare_bolt(self.request, self.work, 'android-arm64')
        self.assertIn(f'-DFUNKE_BOLT_DIAGNOSTICS={diagnostics.as_posix()}', args)
        self.assertIn('-DFUNKE_BOLT_RUNTIME=', args)

    def test_target_verifies_nested_field_layout(self):
        checks = bolt_aot._layout_checks([{'name': 'Game.Motion', 'size': 16, 'alignment': 4, 'fields': [
            {'name': 'Position.X', 'size': 4, 'offset': 0}]}])
        self.assertIn('offsetof(bolt_generated::bC_Game_x2e_Motion, bF_Position.bF_X) == 0', checks)
        self.assertIn('->bF_Position.bF_X) == 4', checks)


if __name__ == '__main__':
    unittest.main()
