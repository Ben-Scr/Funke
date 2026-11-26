import { ready, download, unpack, writeFile, runtimeURL } from './boot.js';

const $ = id => document.getElementById(id);
const text = new TextEncoder();
const decode = new TextDecoder();
const root = '/libsdl/FunkeWebEditor';
const scriptDialog = $('scripts-dialog');
const buildDialog = $('build-dialog');
let currentFile = '', dirty = false, referencesReady = false, compiling = false, exporting = false;
const state = () => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], []));
const checkEdit = () => { const s = state(); if (s.playing || s.busy) throw Error('Bitte zuerst den Play-Modus beenden und warten, bis die Szene geladen ist.'); return s; };
const native = (name, types = [], values = []) => { const error = Module.ccall(name, 'string', types, values); if (error) throw Error(error); };
const persist = () => new Promise((resolve, reject) => Module.FS.syncfs(false, error => error ? reject(error) : resolve()));
const readText = path => Module.FS.readFile(path, {encoding:'utf8'});
const exists = path => Module.FS.analyzePath(path).exists;
function walk(path) {
  if (!exists(path)) return [];
  return Module.FS.readdir(path).filter(n => n !== '.' && n !== '..').flatMap(name => {
    const file = path + '/' + name;
    return Module.FS.isDir(Module.FS.stat(file).mode) ? walk(file) : [file];
  });
}
function projectFiles(includeBinaries = true) {
  return Object.fromEntries(walk(root).filter(path => !path.includes('/Logs/') && (includeBinaries || !path.startsWith(root+'/bin/')))
    .map(path => [path.slice(root.length+1), Module.FS.readFile(path)]));
}
function saveDownload(name, bytes, type = 'application/zip') {
  const url = URL.createObjectURL(new Blob([bytes], {type}));
  const a = document.createElement('a'); a.href=url; a.download=name; a.click();
  setTimeout(() => URL.revokeObjectURL(url), 30000);
}
const zip = files => new Promise((resolve, reject) => fflate.zip(files, {level: 1}, (error, bytes) => error ? reject(error) : resolve(bytes)));
async function action(work, target = 'notice') {
  try { await work(); } catch (error) { $(target).textContent = error.message || String(error); }
}
async function saveScene() { checkEdit(); native('FunkeWebEditorSave'); await persist(); }
function scriptPaths() { return walk(root+'/Assets').filter(p => p.endsWith('.cs')).sort(); }
function populateScripts(select = currentFile) {
  $('script-list').replaceChildren(...scriptPaths().map(path => new Option(path.slice((root+'/Assets/').length), path)));
  if (select && scriptPaths().includes(select)) $('script-list').value = select;
}
function loadScript(path) {
  currentFile = path || ''; $('script-code').value = path ? readText(path) : ''; dirty = false;
  $('script-dirty').textContent = ''; $('script-code').disabled = !path;
  const match = $('script-code').value.match(/\bclass\s+([A-Za-z_]\w*)\s*:\s*(?:Funke\.)?EntityScript\b/);
  if (match) $('script-class').value = match[1];
}
async function saveScript() {
  if (!currentFile) return;
  writeFile(currentFile, text.encode($('script-code').value)); await persist();
  dirty = false; $('script-dirty').textContent = 'Gespeichert';
}
function updateEntities() {
  const selected = $('script-entity').value;
  $('script-entity').replaceChildren(...state().entities.map(entity => new Option(entity.name, entity.id)));
  if (state().entities.some(entity => entity.id === selected)) $('script-entity').value = selected;
}
async function openScripts(path) {
  checkEdit(); if (dirty) await saveScript(); populateScripts(path);
  loadScript(path || $('script-list').value); updateEntities();
  $('web-help').open = false; if (!scriptDialog.open) scriptDialog.showModal();
}
Module.funkeOpenScript = path => action(() => openScripts(path), 'compiler-output');
Module.funkeOpenBuild = () => action(openBuild, 'build-status');
async function compile(release = false) {
  checkEdit(); if (compiling) throw Error('Der Compiler arbeitet bereits.');
  compiling = true; $('script-compile').disabled = true;
  try {
    await saveScript();
    $('compiler-output').textContent = referencesReady ? 'C# wird kompiliert …' : 'C#-Referenzen werden einmalig geladen …';
    if (!referencesReady) { unpack(await download('references.zip'), '/references'); referencesReady=true; }
    await new Promise(resolve => setTimeout(resolve, 30));
    const result = JSON.parse(FunkeManaged.Compile(release));
    const details = (result.diagnostics || []).map(d => `${d.file}:${d.line}:${d.column} ${d.code}: ${d.message}`).join('\n');
    $('compiler-output').textContent = result.success
      ? `Kompilierung erfolgreich · ${result.files} Datei(en).${release?'':' Die neue Fassung ist geladen.'}\n${details}`
      : `Kompilierung fehlgeschlagen. Die letzte funktionierende Fassung bleibt erhalten.\n${result.error || ''}\n${details}`;
    if (!result.success) throw Error('C#-Fehler: Bitte die Meldungen im Skript-Dialog korrigieren.');
    await persist(); return result;
  } catch (error) {
    if (!$('compiler-output').textContent.startsWith('Kompilierung fehlgeschlagen')) $('compiler-output').textContent=error.message || String(error);
    throw error;
  } finally { compiling = false; $('script-compile').disabled = false; }
}
function example(name) {
  return `using Funke;\n\npublic class ${name} : EntityScript\n{\n    public float Speed = 5f;\n    public float JumpSpeed = 7f;\n\n    public override void OnUpdate()\n    {\n        var body = GetComponent<Rigidbody2D>();\n        if (body == null) return;\n        float direction = 0;\n        if (Input.GetKey(KeyCode.A) || Input.GetKey(KeyCode.Left)) direction -= 1;\n        if (Input.GetKey(KeyCode.D) || Input.GetKey(KeyCode.Right)) direction += 1;\n        var velocity = body.LinearVelocity;\n        velocity.X = direction * Speed;\n        if (Input.GetKeyDown(KeyCode.Space) && Mathf.Abs(velocity.Y) < 0.1f)\n            velocity.Y = JumpSpeed;\n        body.LinearVelocity = velocity;\n    }\n}\n`;
}
async function newScript() {
  checkEdit(); if (dirty) await saveScript();
  const name = prompt('Name der C#-Klasse', 'PlayerController'); if (name == null) return;
  if (!/^[A-Za-z_]\w*$/.test(name)) throw Error('Bitte einen C#-Klassennamen ohne Leerzeichen verwenden.');
  const path=root+'/Assets/Scripts/'+name+'.cs'; if (exists(path)) throw Error('Diese Skriptdatei existiert bereits.');
  writeFile(path,text.encode(example(name))); await persist(); populateScripts(path); loadScript(path);
  $('compiler-output').textContent = 'Beispiel: A/D oder Pfeiltasten bewegen, Leertaste springt. Weise das Skript einem Objekt mit Rigidbody2D zu.';
}
async function openBuild() {
  checkEdit(); await saveScene();
  const scenes=walk(root+'/Assets/Scenes').filter(p=>p.endsWith('.scene'));
  $('startup-scene').replaceChildren(...scenes.map(path=>new Option(path.split('/').at(-1).slice(0,-6), path)));
  $('startup-scene').value=state().scenePath;
  $('web-help').open=false;if(!buildDialog.open)buildDialog.showModal();
}
async function exportGame() {
  checkEdit(); if (exporting) return; exporting=true; $('game-export').disabled=true;
  try {
    await saveScene(); $('build-status').textContent='C# wird für das Spiel kompiliert …';
    const compiled=await compile(true);
    const title=$('game-title').value.trim();
    if (!title || /[\\/:*?"<>|]/.test(title)) throw Error('Bitte einen Spielnamen ohne Dateipfad-Sonderzeichen eingeben.');
    const scene=$('startup-scene').value; if(!scene || !exists(scene))throw Error('Bitte eine vorhandene Startszene wählen.');
    const project=JSON.parse(readText(root+'/project.json'));
    project.name=title; project.startupScene=scene.split('/').at(-1).slice(0,-6); project.lastOpenedScene=project.startupScene;
    project.buildProfile='Release'; project.showRuntimeStats=false; project.showRuntimeLogs=false;
    const game=projectFiles(false);
    for(const file of Object.keys(game))if(file.endsWith('.cs')||file.endsWith('.cs.meta'))delete game[file];
    game['project.json']=text.encode(JSON.stringify(project));
    game[`bin/Release/${title}.dll`]=Module.FS.readFile(compiled.output);
    const files={'game.zip':await zip(game)};
    const manifest=JSON.parse(decode.decode(await download('runtime-files.json')));
    const names=manifest.filter(path=>path.startsWith('_framework/')||path.startsWith('Licenses/')||['editor-content.zip','boot.js','browser.css','zip.js','fflate-LICENSE.txt','icon.png'].includes(path));
    let next=0, complete=0;
    await Promise.all(Array.from({length:4},async()=>{while(next<names.length){const path=names[next++];files[path]=await download(path);$('build-status').textContent=`Laufzeit wird gepackt … ${++complete}/${names.length}`;}}));
    files['index.html']=text.encode(`<!doctype html><html lang="de"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Funke-Spiel</title><link rel="icon" href="icon.png"><link rel="stylesheet" href="browser.css"></head><body data-mode="player"><canvas id="canvas" tabindex="0" aria-label="Spiel" oncontextmenu="event.preventDefault()"></canvas><div id="status" role="status">Spiel wird geladen …</div><script src="zip.js"></script><script type="module" src="boot.js"></script></body></html>`);
    files['start-local.py']=text.encode('from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler\nfrom pathlib import Path\nimport os, webbrowser\nos.chdir(Path(__file__).resolve().parent)\nwebbrowser.open("http://localhost:8000")\nprint("Spiel: http://localhost:8000 — Strg+C beendet den Server.")\nThreadingHTTPServer(("127.0.0.1", 8000), SimpleHTTPRequestHandler).serve_forever()\n');
    files['README.txt']=text.encode(`${title}\n\nWEB-SPIEL\nZIP vollständig entpacken. Mit installiertem Python: python start-local.py\nOder alle Dateien unverändert auf statisches HTTPS-Webhosting laden.\nindex.html nicht per file:// öffnen. WebGPU ist erforderlich.\nDas Spiel lädt keine Dateien von Schneidertools oder einem CDN.\n\nC# wurde mit FUNKE_BUILD, FUNKE_BUILD_RELEASE und FUNKE_WEB kompiliert.\nDie Laufzeit enthält die originalen Funke-Engine-Bestandteile und .NET/Mono.\nLizenzhinweise befinden sich unter Licenses/ und fflate-LICENSE.txt.\n`);
    $('build-status').textContent='ZIP wird erstellt …';
    saveDownload(title+'.zip',await zip(files));$('build-status').textContent='Fertig. Das Spiel-ZIP wurde heruntergeladen.';
  } finally { exporting=false;$('game-export').disabled=false; }
}
function validateProject(bytes) {
  let total=0;
  const files=fflate.unzipSync(bytes,{filter(file){
    total+=file.originalSize;
    if(total>128*1024*1024)throw Error('Das entpackte Projekt darf höchstens 128 MB groß sein.');
    if(file.name.startsWith('/')||file.name.includes('\\')||file.name.split('/').some(p=>p==='..'||p==='.'))throw Error('Ungültiger Projektpfad.');
    return !file.name.endsWith('/');
  }});
  if(!files['project.json']||!Object.keys(files).some(p=>p.startsWith('Assets/Scenes/')&&p.endsWith('.scene')))throw Error('Das ZIP enthält kein Funke-Projekt mit Szene.');
  const project=JSON.parse(decode.decode(files['project.json']));if(typeof project.name!=='string'||!project.name.trim()||/[\\/:*?"<>|]/.test(project.name)||typeof project.startupScene!=='string')throw Error('Ungültige Projektdaten.');
  return files;
}
function removeTree(path) {
  for(const name of Module.FS.readdir(path).filter(n=>n!=='.'&&n!=='..')){const full=path+'/'+name;if(Module.FS.isDir(Module.FS.stat(full).mode)){removeTree(full);Module.FS.rmdir(full);}else Module.FS.unlink(full);}
}

await ready;
$('scripts-open').onclick=()=>action(()=>openScripts());
$('build-open').onclick=()=>action(openBuild);
$('script-new').onclick=()=>action(newScript,'compiler-output');
$('script-code').oninput=()=>{dirty=true;$('script-dirty').textContent='Ungespeichert';};
$('script-list').onchange=()=>action(async()=>{if(dirty)await saveScript();loadScript($('script-list').value);},'compiler-output');
$('script-save').onclick=()=>action(saveScript,'compiler-output');
$('script-compile').onclick=()=>compile().catch(()=>{});
$('script-attach').onclick=()=>action(async()=>{await compile();native('FunkeWebEditorAttachScript',['number','string'],[Number($('script-entity').value),$('script-class').value.trim()]);await saveScene();$('compiler-output').textContent='Skript zugewiesen und Szene gespeichert. ▶ startet das Spiel.';},'compiler-output');
$('game-export').onclick=()=>action(exportGame,'build-status');
for(const dialog of document.querySelectorAll('dialog')){
  dialog.querySelector('.close').onclick=()=>dialog.close();
  dialog.addEventListener('close',()=>{if(dialog===scriptDialog&&dirty)action(saveScript);Module.canvas.focus();});
  // Text input belongs to the browser editor, not SDL's shortcuts underneath it.
  for(const event of ['keydown','keyup','keypress','pointerdown','pointerup','wheel'])dialog.addEventListener(event,e=>e.stopPropagation());
}
scriptDialog.addEventListener('keydown',event=>{
  if((event.ctrlKey||event.metaKey)&&event.key==='s'){event.preventDefault();action(saveScript,'compiler-output');}
  if((event.ctrlKey||event.metaKey)&&event.key==='Enter'){event.preventDefault();compile().catch(()=>{});}
  if(event.target===$('script-code')&&event.key==='Tab'){event.preventDefault();const a=event.target;const start=a.selectionStart;a.setRangeText('    ',start,a.selectionEnd,'end');a.dispatchEvent(new Event('input'));}
});
$('save').onclick=()=>action(async()=>{await saveScene();$('notice').textContent='Im Browser gespeichert.';});
$('export').onclick=()=>action(async()=>{await saveScene();const s=state();saveDownload(s.scene+'.scene',Module.FS.readFile(s.scenePath),'application/octet-stream');});
$('import').onclick=()=>action(()=>{checkEdit();$('scene-file').click();});
$('scene-file').onchange=event=>action(async()=>{const file=event.target.files[0];event.target.value='';if(!file)return;checkEdit();if(!file.name.endsWith('.scene')||file.size>16*1024*1024)throw Error('Bitte eine .scene-Datei bis 16 MB wählen.');await saveScene();const path=root+'/Assets/Scenes/Import-'+Date.now()+'.scene';writeFile(path,new Uint8Array(await file.arrayBuffer()));native('FunkeWebEditorOpen',['string'],[path]);$('web-help').open=false;Module.canvas.focus();});
$('project-export').onclick=()=>action(async()=>{await saveScene();if(dirty)await saveScript();saveDownload('Funke-Projekt.zip',await zip(projectFiles()));$('notice').textContent='Projekt mit Szenen, Assets und Quellcode gesichert.';});
$('project-import').onclick=()=>action(()=>{checkEdit();$('project-file').click();});
$('project-file').onchange=event=>action(async()=>{const file=event.target.files[0];event.target.value='';if(!file)return;checkEdit();if(file.size>64*1024*1024)throw Error('Projekt-ZIP darf höchstens 64 MB groß sein.');const files=validateProject(new Uint8Array(await file.arrayBuffer()));await saveScene();if(dirty)await saveScript();saveDownload('Funke-Projekt-vor-Import.zip',await zip(projectFiles()));removeTree(root);for(const[path,data]of Object.entries(files))writeFile(root+'/'+path,data);await persist();location.reload();});
$('script-import').onclick=()=>action(()=>{checkEdit();$('script-file').click();},'compiler-output');
$('script-file').onchange=event=>action(async()=>{checkEdit();for(const file of event.target.files){if(!/^[\w.-]+\.cs$/i.test(file.name)||file.size>1024*1024)throw Error('Bitte .cs-Dateien mit einfachem Dateinamen bis 1 MB wählen.');const path=root+'/Assets/Scripts/'+file.name;if(exists(path))throw Error(file.name+' existiert bereits.');writeFile(path,new Uint8Array(await file.arrayBuffer()));}event.target.value='';await persist();populateScripts();loadScript($('script-list').value);},'compiler-output');
$('assets-import').onclick=()=>action(()=>{checkEdit();$('asset-files').click();});
$('asset-files').onchange=event=>action(async()=>{checkEdit();for(const file of event.target.files){if(!file.name||file.name.includes('/')||file.name.includes('\\')||file.size>16*1024*1024)throw Error('Bitte Assets bis 16 MB pro Datei wählen.');const path=root+'/Assets/Imported/'+file.name;if(exists(path))throw Error(file.name+' existiert bereits.');writeFile(path,new Uint8Array(await file.arrayBuffer()));}event.target.value='';await persist();$('notice').textContent='Assets nach Assets/Imported importiert.';});
window.addEventListener('beforeunload',event=>{if(dirty||exporting){event.preventDefault();event.returnValue='';}});
