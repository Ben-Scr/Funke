// PLAYWRIGHT_MODULE can point at an already-installed Playwright; no downloads required.
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const out = path.resolve(__dirname, '../../../output/web-editor/qa');
fs.mkdirSync(out, {recursive: true});
(async () => {
  const browser = await chromium.launch({channel: 'msedge', headless: true});
  const errors = [];
  try {
    const page = await browser.newPage({viewport: {width: 1440, height: 900}, colorScheme: 'dark'});
    page.setDefaultTimeout(20000);
    page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
    page.on('pageerror', e => errors.push(e.stack));
    const state = () => page.evaluate(() => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])));
    const ready = async () => {
      await page.waitForFunction(() => window.Module?.funkeReady, null, {timeout: 45000});
      await page.waitForFunction(() => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])).busy === '');
      await page.waitForTimeout(700);
    };
    await page.goto(process.env.FUNKE_EDITOR_URL || 'http://127.0.0.1:4185');
    await ready();
    console.log('initial', await state());
    await page.mouse.click(110, 212);
    await page.waitForTimeout(600);
    await page.keyboard.down('Control');
    await page.mouse.click(1320, 201);
    await page.keyboard.up('Control');
    await page.keyboard.press('Control+a');
    await page.keyboard.type('-2.5');
    await page.keyboard.press('Enter');
    await page.waitForTimeout(300);
    assert.equal((await state()).entities.find(e => e.name === 'Falling Box').x, -2.5);
    await page.screenshot({path: path.join(out, 'selected.png')});
    await page.mouse.click(700, 41);
    await page.waitForTimeout(2400);
    console.log('playing', await state());
    assert.equal((await state()).playing, true);
    assert.ok((await state()).entities.find(e => e.name === 'Falling Box').y < -2);
    await page.screenshot({path: path.join(out, 'playing.png')});
    await page.mouse.click(659, 41);
    await page.waitForTimeout(1100);
    console.log('stopped', await state());
    assert.equal((await state()).playing, false);
    assert.equal((await state()).entities.find(e => e.name === 'Falling Box').y, 2);
    await page.keyboard.press('Control+s');
    await page.waitForTimeout(1000);
    console.log('saved', await page.evaluate(() => Module.FS.readdir('/libsdl/FunkeWebEditor/Assets/Scenes')));
    assert.ok((await page.evaluate(() => Module.FS.readdir('/libsdl/FunkeWebEditor/Assets/Scenes'))).includes('BrowserScene.scene'));
    await page.reload();
    await ready();
    console.log('reloaded', await state());
    assert.equal((await state()).entities.find(e => e.name === 'Falling Box').x, -2.5);
    await page.screenshot({path: path.join(out, 'reloaded.png')});
    await page.locator('#web-help summary').click();
    const [download] = await Promise.all([
      page.waitForEvent('download'),
      page.getByRole('button', {name: 'Szene exportieren'}).click(),
    ]);
    const exported = path.join(out, download.suggestedFilename());
    await download.saveAs(exported);
    assert.ok(fs.statSync(exported).size > 500);
    await page.locator('#scene-file').setInputFiles(exported);
    await page.waitForTimeout(800);
    console.log('after import', await state(), await page.locator('#notice').textContent(), errors);
    await page.waitForFunction(() => {
      const state = JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], []));
      return !state.busy && state.scenePath.includes('/Import-');
    });
    const imported = await state();
    console.log('imported', imported);
    assert.equal(imported.entities.find(e => e.name === 'Falling Box').x, -2.5);
    await page.waitForTimeout(500);
    await page.reload();
    await ready();
    assert.equal((await state()).scenePath, imported.scenePath);
    assert.equal((await state()).entities.find(e => e.name === 'Falling Box').x, -2.5);
    console.log('import restored after reload');
    console.log('errors', errors);
    fs.writeFileSync(path.join(out, 'browser-errors.json'), JSON.stringify(errors, null, 2));
    assert.deepEqual(errors, []);
  } finally { await browser.close(); }
})().catch(e => { console.error(e); process.exitCode = 1; });
