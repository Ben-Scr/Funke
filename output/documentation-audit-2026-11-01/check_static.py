from pathlib import Path
import importlib.util
import json
import re
import runpy

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
inventory = json.loads((OUT / 'checks.json').read_text(encoding='utf-8'))['inventory']
json_checked, fragments, json_errors = 0, 0, []
for rel in inventory:
    if rel.startswith('.claude/'):
        continue
    text = (ROOT / rel).read_text(encoding='utf-8-sig')
    for match in re.finditer(r'^[ \t]*```(jsonc?)\s*\n(.*?)^[ \t]*```', text, re.M | re.S):
        body = match.group(2).strip()
        # Strip comments only outside strings, for the deliberately annotated examples.
        body = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*.*?\*/', lambda m: '' if m.group().startswith(('/', '/*')) else m.group(), body, flags=re.S)
        if re.match(r'^"[^"\n]+"\s*:', body):
            body = '{' + body + '}'
            fragments += 1
        try:
            json.loads(body)
            json_checked += 1
        except json.JSONDecodeError as error:
            json_errors.append({'file': rel, 'line': text[:match.start()].count('\n')+1, 'error': str(error)})

validator = Path('C:/Users/benso/.codex/skills/.system/skill-creator/scripts/quick_validate.py')
spec = importlib.util.spec_from_file_location('skill_validator', validator)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
skill_errors = []
skills = list((ROOT / '.agents/skills').glob('*/SKILL.md'))
for path in skills:
    valid, message = module.validate_skill(path.parent)
    if not valid:
        skill_errors.append({'file': str(path.relative_to(ROOT)), 'error': message})

game = runpy.run_path(str(ROOT / '.agents/skills/funke-make-a-game/scripts/funke_game_tool.py'))
fixture_results = []
for label, components, should_pass in [
    ('screen UI', {'RectTransform2D': {}}, True),
    ('2D world canvas', {'RectTransform2D': {}, 'Transform2D': {}}, True),
    ('3D world canvas', {'RectTransform2D': {}, 'Transform3D': {}}, True),
    ('conflicting world transforms', {'Transform2D': {}, 'Transform3D': {}}, False),
]:
    report = game['Report'](ROOT)
    entity = {'uuid': '1', 'name': label, **components}
    game['validate_entities'](ROOT / 'fixture.scene', [entity], False, report, None, game['ScriptIndex'](), {})
    passed = (not report.errors) == should_pass
    fixture_results.append({'case': label, 'passed': passed, 'errors': report.errors})

result = {'json_examples_checked': json_checked, 'json_property_fragments': fragments, 'json_errors': json_errors,
          'skills_checked': len(skills), 'skill_errors': skill_errors, 'world_ui_validator': fixture_results}
(OUT / 'static-checks.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
print(json.dumps(result, ensure_ascii=True, indent=2))
raise SystemExit(bool(json_errors or skill_errors or any(not r['passed'] for r in fixture_results)))
