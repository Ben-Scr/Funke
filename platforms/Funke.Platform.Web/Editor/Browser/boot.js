import { dotnet } from './_framework/dotnet.js';

const status = document.getElementById('status');
const canvas = document.getElementById('canvas');
const player = document.body.dataset.mode === 'player';
const base = new URL('.', import.meta.url);
export const runtimeURL = path => new URL(path, base);
export function reportError(error) {
  document.body.append(status); status.classList.add('error'); status.textContent = String(error?.message || error);
}
export async function download(path) {
  const response = await fetch(runtimeURL(path));
  if (!response.ok) throw Error(`Datei nicht verfügbar: ${path} (${response.status})`);
  return new Uint8Array(await response.arrayBuffer());
}
export function writeFile(path, bytes) {
  Module.FS.mkdirTree(path.slice(0, path.lastIndexOf('/')));
  Module.FS.writeFile(path, bytes);
}
export function unpack(bytes, root) {
  const files = fflate.unzipSync(bytes);
  for (const [path, data] of Object.entries(files)) {
    if (path.endsWith('/')) continue;
    if (path.startsWith('/') || path.includes('\\') || path.split('/').some(p => p === '..' || p === '.')) throw Error('Ungültiger Pfad im ZIP.');
    writeFile(root + '/' + path, data);
  }
}
let readyResolve;
export const ready = new Promise(resolve => readyResolve = resolve);
async function start() {
  if (!navigator.gpu) throw Error('Funke benötigt einen Desktop-Browser mit WebGPU-Unterstützung.');
  const runtime = await dotnet.withEnvironmentVariable('HOME', '/libsdl').withModuleConfig({
    canvas, funkePlayer: player, funkeReportError: reportError,
    setStatus(text) {
      if (status.classList.contains('error')) return;
      status.textContent = text || '';
      if (window.Module?.funkeReady) {
        for (const element of document.querySelectorAll('nav,dialog,input[type=file]')) document.body.append(element);
        readyResolve();
      }
    }
  }).create();
  window.Module = runtime.Module;
  Module.canvas = canvas;
  Module.funkePlayer = player;
  Module.funkeReportError = reportError;
  window.FunkeManaged = (await runtime.getAssemblyExports('FunkeWebHost')).BrowserEditor;
  status.textContent = 'Editor-Assets werden geladen …';
  unpack(await download('editor-content.zip'), '/editor');
  if (player) {
    unpack(await download('game.zip'), '/game/project');
    document.title = JSON.parse(Module.FS.readFile('/game/project/project.json', {encoding:'utf8'})).name || 'Funke-Spiel';
  }
  await runtime.runMain();
  if (!player) await import('./workspace.js');
}
start().catch(reportError);
