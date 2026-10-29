const fs = require('node:fs');
const path = require('node:path');

async function main() {
  let input = '';
  for await (const chunk of process.stdin) input += chunk;
  const options = JSON.parse(input);
  let playwright;
  try { playwright = require('playwright'); }
  catch { playwright = require('playwright-core'); }
  const messages = [];
  const errors = [];
  let browser;
  let page;
  try {
    browser = await playwright.chromium.launch({
      headless: !options.headed,
      executablePath: options.executable || undefined,
      channel: options.channel || undefined,
      args: options.browserArgs,
    });
    page = await browser.newPage({ viewport: { width: options.width, height: options.height } });
    await page.route('**/favicon.ico', route => route.fulfill({ status: 204 }));
    page.on('console', message => {
      messages.push(`[${message.type()}] ${message.text()}`);
      if (message.type() === 'error') errors.push(message.text());
    });
    page.on('pageerror', error => errors.push(error.message));
    page.on('crash', () => errors.push('Browser renderer crashed.'));
    page.on('response', response => {
      if (response.status() >= 400 && !new URL(response.url()).pathname.endsWith('/favicon.ico'))
        errors.push(`HTTP ${response.status()}: ${response.url()}`);
    });
    await page.goto(options.url, { waitUntil: 'domcontentloaded', timeout: options.timeout * 1000 });
    if (!await page.evaluate(async () => Boolean(navigator.gpu && await navigator.gpu.requestAdapter())))
      throw new Error('The selected browser cannot create a WebGPU adapter.');
    const deadline = Date.now() + options.timeout * 1000;
    let readySince = 0;
    while (Date.now() < deadline) {
      if (errors.length) throw new Error(errors.join('\n'));
      const status = await page.locator('#status.error').allTextContents();
      if (status.length) throw new Error(status.join('\n'));
      if (options.markers.every(marker => messages.some(message => message.includes(marker)))) {
        readySince ||= Date.now();
        if (Date.now() - readySince >= options.settle * 1000) {
          await page.locator('#canvas').screenshot({ path: path.join(options.output, 'game.png') });
          if (errors.length) throw new Error(errors.join('\n'));
          return;
        }
      }
      await page.waitForTimeout(100);
    }
    throw new Error(`Timed out waiting for script markers: ${options.markers.join(', ')}`);
  } finally {
    fs.writeFileSync(path.join(options.output, 'game.log'), messages.concat(errors.map(error => `[error] ${error}`)).join('\n'));
    if (page && !fs.existsSync(path.join(options.output, 'game.png')))
      await page.screenshot({ path: path.join(options.output, 'failure.png') }).catch(() => {});
    if (browser) await browser.close();
  }
}

main().catch(error => { console.error(error.stack || String(error)); process.exitCode = 1; });
