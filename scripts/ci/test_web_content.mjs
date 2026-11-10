import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import test from 'node:test';
import vm from 'node:vm';

const source = await readFile(new URL('../../platforms/Funke.Platform.Web/Template/dotnet/main.js', import.meta.url), 'utf8');
const loader = source.slice(source.indexOf('async function loadContent('), source.indexOf('\nasync function start('));

async function load(manifest, responses) {
  const files = new Map();
  const paths = [];
  const context = vm.createContext({
    status: {},
    setTimeout: callback => callback(),
    fetch: async url => url === 'game/funke-content.json'
      ? { json: async () => manifest }
      : { ok: responses.has(url), status: responses.has(url) ? 200 : 404,
          arrayBuffer: async () => Uint8Array.from(responses.get(url)).buffer },
  });
  vm.runInContext(loader, context);
  await context.loadContent({
    FS_createPath: (root, path) => paths.push(path),
    FS_createDataFile: (dir, name, data) => files.set(`${dir}/${name}`, [...data]),
  });
  return { files, paths };
}

test('legacy manifests still load loose files and empty files', async () => {
  const { files, paths } = await load({ directories: ['bin'], files: ['bin/Game.dll', 'empty'] },
    new Map([['game/bin/Game.dll', [1, 2]], ['game/empty', []]]));
  assert.deepEqual(files.get('/game/bin/Game.dll'), [1, 2]);
  assert.deepEqual(files.get('/game/empty'), []);
  assert.deepEqual(paths, ['game/bin']);
});

test('chunks reconstruct one original file alongside loose content', async () => {
  const { files } = await load({ directories: [], files: ['Assets.fbundle', 'project.json'],
    chunks: { 'Assets.fbundle': { size: 5, parts: ['part0', 'part1'] } } },
    new Map([['game/part0', [0, 1, 255]], ['game/part1', [3, 4]], ['game/project.json', [123, 125]]]));
  assert.deepEqual(files.get('/game/Assets.fbundle'), [0, 1, 255, 3, 4]);
  assert.deepEqual(files.get('/game/project.json'), [123, 125]);
  assert.equal(files.size, 2);
});

for (const [name, data, expected] of [
  ['missing', null, /Missing game file/],
  ['truncated', [1], /Incomplete game file/],
  ['oversized', [1, 2, 3], /Oversized game file/],
]) {
  test(`${name} chunk fails before exposing an invalid bundle`, async () => {
    await assert.rejects(load({ directories: [], files: ['Assets.fbundle'],
      chunks: { 'Assets.fbundle': { size: 2, parts: ['part0'] } } },
      new Map(data === null ? [] : [['game/part0', data]])), expected);
  });
}
