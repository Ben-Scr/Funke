// Boots the .NET WebAssembly runtime, copies the game content into the virtual filesystem at /game
// and runs the generated host program, whose Main starts the engine. runMain (not runMainAndExit)
// keeps the runtime alive after Main returns, because the engine runs from requestAnimationFrame.
import { dotnet } from './_framework/dotnet.js';

const status = document.getElementById('status');
const canvas = document.getElementById('canvas');

// SDL hides every other page element once the canvas fills the page; messages bring the status back.
function showStatus(text, kind) {
  document.body.appendChild(status);
  status.classList.add(kind);
  status.textContent = text;
}

function reportError(text) {
  showStatus(text, 'error');
}

async function loadContent(Module) {
  const manifest = await (await fetch('game/funke-content.json', { cache: 'no-cache' })).json();
  for (const dir of manifest.directories) {
    Module.FS_createPath('/', `game/${dir}`, true, true);
  }
  let loaded = 0;
  let next = 0;
  let failed = false;
  // Connections drop now and then; retry before giving up on the whole game.
  const download = async (file) => {
    for (let attempt = 1; ; ++attempt) {
      try {
        const response = await fetch(`game/${file}`);
        if (!response.ok) throw new Error(`Missing game file: ${file} (${response.status})`);
        return new Uint8Array(await response.arrayBuffer());
      }
      catch (error) {
        if (attempt >= 3 || failed) throw error;
        await new Promise((resolve) => setTimeout(resolve, 250 * attempt));
      }
    }
  };
  // A bounded number of requests in flight: thousands at once exhaust the browser's buffers.
  const worker = async () => {
    while (next < manifest.files.length && !failed) {
      const file = manifest.files[next++];
      let data;
      try {
        data = await download(file);
      }
      catch (error) {
        failed = true;
        throw error;
      }
      const slash = file.lastIndexOf('/');
      Module.FS_createDataFile(slash < 0 ? '/game' : `/game/${file.slice(0, slash)}`,
        file.slice(slash + 1), data, true, true, true);
      if (!failed) status.textContent = `Loading… ${++loaded}/${manifest.files.length}`;
    }
  };
  await Promise.all(Array.from({ length: 16 }, worker));
}

async function start() {
  if (!navigator.gpu) {
    reportError('This browser does not support WebGPU. Use a current Chrome, Edge, Safari or Firefox.');
    return;
  }
  // Application.PersistentDataPath derives from $HOME; /libsdl is the IndexedDB-backed folder the
  // engine mounts before scripts run, so PlayerPrefs and SaveData survive reloads.
  const { Module, runMain } = await dotnet.withEnvironmentVariable('HOME', '/libsdl').create();
  Module.canvas = canvas;
  Module.funkeReportError = reportError;
  Module.funkeGameEnded = () => showStatus('The game has ended. Reload the page to play again.', 'ended');
  await loadContent(Module);
  status.textContent = '';
  await runMain();
}

start().catch((error) => {
  console.error(error);
  reportError(`The game failed to start: ${error.message ?? error}`);
});
