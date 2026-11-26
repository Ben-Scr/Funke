const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const { unzipSync } = require('./Browser/zip.js');
const out = path.resolve(__dirname, '../../../output/web-editor/qa/managed');
fs.mkdirSync(out, { recursive: true });
const source = speed => `using Funke;
public class PlayerController : EntityScript {
    public override void OnUpdate() {
        var body = GetComponent<Rigidbody2D>();
        if (body != null) body.LinearVelocity = new Vector2(${speed}f, body.LinearVelocity.Y);
    }
}`;

(async () => {
    const browser = await chromium.launch({ channel: 'msedge', headless: true });
    let server;
    try {
        const context = await browser.newContext({ viewport: { width: 1440, height: 900 }, colorScheme: 'dark' });
        const page = await context.newPage();
        page.setDefaultTimeout(30000);
        const errors = [];
        page.on('pageerror', e => errors.push(e.stack));
        page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
        const state = () => page.evaluate(() => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])));
        const ready = async () => {
            await page.waitForFunction(() => window.Module?.funkeReady, null, { timeout: 45000 });
            await page.waitForFunction(() => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])).busy === '');
        };
        await page.goto(process.env.FUNKE_EDITOR_URL || 'http://127.0.0.1:4186');
        await ready();
        assert.equal((await state()).scripting, true);
        page.on('dialog', dialog => dialog.accept('PlayerController'));
        await page.locator('#scripts-open').click();
        await page.locator('#script-new').click();
        // The supplied movement example must compile against the actual ScriptCore API.
        const compile = async expected => {
            await page.locator('#script-compile').click();
            await page.waitForFunction(() => !document.getElementById('script-compile').disabled);
            const message = await page.locator('#compiler-output').textContent();
            console.log(message);
            assert.match(message, expected);
        };
        await compile(/Kompilierung erfolgreich/);
        await page.locator('#script-code').fill(source(1));
        await compile(/Kompilierung erfolgreich/);
        await page.locator('#script-entity').selectOption({ label: 'Falling Box' });
        await page.locator('#script-attach').click();
        await page.waitForFunction(() => document.getElementById('compiler-output').textContent.includes('Skript zugewiesen'));
        await page.screenshot({ path: path.join(out, 'script-compiled.png') });
        await page.getByRole('button', { name: 'Skripte schließen' }).click();
        const run = async () => {
            await page.mouse.click(700, 41);
            await page.waitForFunction(() => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])).playing);
            await page.waitForTimeout(750);
            const x = (await state()).entities.find(e => e.name === 'Falling Box').x;
            await page.mouse.click(659, 41);
            await page.waitForFunction(() => !JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])).playing);
            await page.waitForTimeout(250);
            assert.equal((await state()).entities.find(e => e.name === 'Falling Box').x, -1);
            return x;
        };
        const first = await run();
        assert.ok(first > -0.7 && first < 0.3, `Initial C# velocity did not run: ${first}`);
        await page.locator('#scripts-open').click();
        await page.locator('#script-code').fill(source(4));
        await compile(/Kompilierung erfolgreich/);
        await page.getByRole('button', { name: 'Skripte schließen' }).click();
        const changed = await run();
        assert.ok(changed > first + 1, `Edited C# was not loaded: ${changed} <= ${first}`);
        await page.locator('#scripts-open').click();
        await page.locator('#script-code').fill(source(4) + '\nTHIS IS INVALID C#');
        await compile(/Kompilierung fehlgeschlagen[\s\S]*CS\d+/);
        await page.screenshot({ path: path.join(out, 'compiler-error.png') });
        await page.getByRole('button', { name: 'Skripte schließen' }).click();
        const retained = await run();
        assert.ok(retained > 1, `Compiler error lost working code: ${retained}`);
        await page.locator('#scripts-open').click();
        await page.locator('#script-code').fill(source(4));
        await compile(/Kompilierung erfolgreich/);
        await page.getByRole('button', { name: 'Skripte schließen' }).click();
        await page.reload(); await ready();
        assert.ok((await run()) > 1, 'Saved C# assembly did not restore after reload');
        await page.locator('#web-help summary').click();
        const assetBytes = Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jhBQAAAAASUVORK5CYII=', 'base64');
        await page.locator('#asset-files').setInputFiles({ name: 'test-pixel.png', mimeType: 'image/png', buffer: assetBytes });
        await page.waitForFunction(() => document.getElementById('notice').textContent.includes('Assets nach'));
        const backupWait = page.waitForEvent('download');
        await page.locator('#project-export').click();
        const backup = await backupWait;
        const backupPath = path.join(out, 'project.zip'); await backup.saveAs(backupPath);
        const backupFiles = unzipSync(fs.readFileSync(backupPath));
        assert.deepEqual(Buffer.from(backupFiles['Assets/Imported/test-pixel.png']), assetBytes);
        assert.match(Buffer.from(backupFiles['Assets/Scripts/PlayerController.cs']).toString(), /4f/);
        await page.locator('#scripts-open').click();
        await page.locator('#script-code').fill(source(2));
        await compile(/Kompilierung erfolgreich/);
        await page.getByRole('button', { name: 'Skripte schließen' }).click();
        await page.locator('#web-help summary').click();
        const restoreNavigation = page.waitForEvent('framenavigated', frame => frame === page.mainFrame());
        await page.locator('#project-file').setInputFiles(backupPath);
        await restoreNavigation; await ready();
        assert.ok((await run()) > 1, 'Project restore did not restore the working script assembly');
        await page.locator('#scripts-open').click();
        assert.match(await page.locator('#script-code').inputValue(), /4f/);
        await page.emulateMedia({ colorScheme: 'light' });
        await page.screenshot({ path: path.join(out, 'scripts-light.png') });
        await page.emulateMedia({ colorScheme: 'dark' });
        await page.getByRole('button', { name: 'Skripte schließen' }).click();
        await page.locator('#build-open').click();
        await page.locator('#game-title').fill('Funke CSharp Test');
        const downloaded = page.waitForEvent('download', { timeout: 90000 });
        await page.locator('#game-export').click();
        const zip = await downloaded;
        const archive = path.join(out, 'game.zip'); await zip.saveAs(archive);
        await page.screenshot({ path: path.join(out, 'export-complete.png') });
        const exported = path.join(out, 'exported-' + Date.now());
        for (const [name, bytes] of Object.entries(unzipSync(fs.readFileSync(archive)))) {
            const target = path.resolve(exported, name);
            assert.ok(target.startsWith(exported + path.sep));
            fs.mkdirSync(path.dirname(target), { recursive: true }); fs.writeFileSync(target, bytes);
        }
        assert.ok(fs.existsSync(path.join(exported, 'game.zip')));
        assert.ok(!fs.existsSync(path.join(exported, 'workspace.js')));
        server = http.createServer((req, res) => {
            try {
                const pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname);
                const file = path.resolve(exported, '.' + (pathname === '/' ? '/index.html' : pathname));
                if (!file.startsWith(exported + path.sep)) throw Error('Invalid path');
                const mime = { '.js': 'text/javascript', '.wasm': 'application/wasm', '.css': 'text/css', '.html': 'text/html', '.json': 'application/json' };
                res.setHeader('Content-Type', mime[path.extname(file)] || 'application/octet-stream'); res.end(fs.readFileSync(file));
            } catch { res.writeHead(404); res.end(); }
        });
        await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
        const origin = `http://127.0.0.1:${server.address().port}`;
        const player = await context.newPage();
        await player.route('**/*', route => {
            if (new URL(route.request().url()).origin !== origin) { errors.push('External export dependency: ' + route.request().url()); return route.abort(); }
            return route.continue();
        });
        player.on('pageerror', e => errors.push(e.stack));
        player.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
        await player.goto(origin);
        await player.waitForFunction(() => window.Module?.funkeReady, null, { timeout: 45000 });
        await player.waitForTimeout(750);
        const playerState = await player.evaluate(() => JSON.parse(Module.ccall('FunkeWebEditorState', 'string', [], [])));
        console.log('Standalone game', playerState);
        assert.equal(playerState.player, true); assert.equal(playerState.playing, true); assert.equal(playerState.scripting, true);
        assert.ok(playerState.entities.find(e => e.name === 'Falling Box').x > 0, 'Exported C# did not move the player');
        assert.equal(await player.locator('#web-tools').count(), 0);
        await player.screenshot({ path: path.join(out, 'standalone-game.png') });
        fs.writeFileSync(path.join(out, 'errors.json'), JSON.stringify(errors, null, 2));
        assert.deepEqual(errors, []);
        console.log('Passed: native editor, real C#, reload, compiler error recovery, persistence, ZIP export, independent Web player.');
    } finally { if (server) server.close(); await browser.close(); }
})().catch(error => { console.error(error); process.exitCode = 1; });
