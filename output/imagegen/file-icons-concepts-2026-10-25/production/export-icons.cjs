const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('C:/Users/benso/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');

const assets = {
  file_cs: 'C:/Users/benso/.codex/generated_images/01a16ff5-97eb-7be3-a946-8a48a4e30f29/exec-6007b138-fdf1-4dfa-9943-b652a6a4a829.png',
  file_txt: 'C:/Users/benso/.codex/generated_images/01a16ff5-97eb-7be3-a946-8a48a4e30f29/exec-c945b60c-3a10-431b-9b79-45a5ae4be115.png',
  file_scene: 'C:/Users/benso/.codex/generated_images/01a16ff5-97eb-7be3-a946-8a48a4e30f29/exec-96365dc4-d336-457f-9e99-8684d6d59c1c.png',
};
const sizes = [16, 32, 64, 128, 256];

async function main() {
  const overlays = [];
  const labels = [];
  let row = 0;
  for (const [name, source] of Object.entries(assets)) {
    const folder = path.join(__dirname, name);
    await fs.mkdir(folder, { recursive: true });
    const master = path.join(folder, name + '_master.png');
    await fs.copyFile(source, master);
    const metadata = await sharp(master).metadata();
    if (!metadata.hasAlpha) throw new Error('Missing alpha: ' + name);
    let x = 120;
    for (const size of sizes) {
      const file = path.join(folder, `${name}_${size}.png`);
      await sharp(master).resize(size, size, { fit: 'contain', kernel: 'lanczos3', background: '#00000000' }).png().toFile(file);
      const exported = await sharp(file).metadata();
      if (exported.width !== size || exported.height !== size || !exported.hasAlpha) throw new Error('Invalid export: ' + file);
      for (let theme = 0; theme < 2; theme++) {
        const y = row * 310 + 38;
        const input = await sharp(file).flatten({ background: theme ? '#ededed' : '#25272b' }).png().toBuffer();
        overlays.push({ input, left: x + theme * 740, top: y + Math.floor((256 - size) / 2) });
        labels.push(`<text x="${x + theme * 740 + size / 2}" y="${y + 277}" text-anchor="middle">${size}px</text>`);
      }
      x += size + 20;
    }
    labels.push(`<text x="16" y="${row * 310 + 168}">${name.replace('file_', '.')}</text>`);
    row++;
    console.log(name + ': exported ' + sizes.join(', ') + ' px');
  }
  const labelSvg = Buffer.from(`<svg width="1480" height="930"><g fill="#aeb5bf" font-size="16" font-family="Arial">${labels.join('')}</g></svg>`);
  overlays.push({ input: labelSvg, left: 0, top: 0 });
  await sharp({ create: { width: 1480, height: 930, channels: 4, background: '#25272b' } }).composite(overlays).png().toFile(path.join(__dirname, 'export-preview.png'));
}

main().catch(error => { console.error(error); process.exitCode = 1; });
