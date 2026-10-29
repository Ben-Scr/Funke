const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const sharp = require('C:/Users/benso/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');

const root = path.resolve(__dirname, '../../..');
const assetRoot = path.join(root, 'Funke-Runtime/FunkeAssets/Textures/Editor/FileIcons');
const destination = path.join(assetRoot, 'file_material');
const source = path.join(__dirname, 'file_material_master.png');
const sizes = [16, 32, 64, 128, 256];

async function main() {
  await fs.mkdir(destination, { recursive: true });
  const validation = [];
  for (const size of sizes) {
    const file = path.join(destination, `file_material_${size}.png`);
    await sharp(source).resize(size, size, { fit: 'contain', kernel: 'lanczos3', background: '#00000000' }).png().toFile(file);
    const meta = await sharp(file).metadata();
    const { data, info } = await sharp(file).ensureAlpha().raw().toBuffer({ resolveWithObject: true });
    let transparent = 0, opaque = 0, border = 0;
    for (let y = 0; y < info.height; y++) {
      for (let x = 0; x < info.width; x++) {
        const alpha = data[(y * info.width + x) * 4 + 3];
        if (alpha === 0) transparent++;
        if (alpha === 255) opaque++;
        if ((x === 0 || y === 0 || x === info.width - 1 || y === info.height - 1) && alpha > 32) border++;
      }
    }
    if (meta.width !== size || meta.height !== size || !meta.hasAlpha || !transparent || !opaque || border) {
      throw new Error('Invalid icon dimensions, transparency, or padding: ' + file);
    }
    validation.push({ size, transparent, opaque, borderPixelsAbove32: border, sha256: crypto.createHash('sha256').update(await fs.readFile(file)).digest('hex') });
  }

  for (const theme of ['dark', 'light']) {
    const layers = [], labels = [];
    let x = 24;
    for (const size of sizes) {
      layers.push({ input: await fs.readFile(path.join(destination, `file_material_${size}.png`)), left: x, top: 28 + Math.floor((256 - size) / 2) });
      labels.push(`<text x="${x + size / 2}" y="310" text-anchor="middle">${size}px</text>`);
      x += size + 24;
    }
    const neighbours = ['file_model', 'file_shader', 'file_material', 'file_dataasset', 'file_scene'];
    for (let i = 0; i < neighbours.length; i++) {
      const name = neighbours[i], left = 32 + i * 120;
      layers.push({ input: await fs.readFile(path.join(assetRoot, name, `${name}_64.png`)), left: left + 12, top: 354 });
      layers.push({ input: await fs.readFile(path.join(assetRoot, name, `${name}_16.png`)), left: left + 12, top: 448 });
      layers.push({ input: await fs.readFile(path.join(assetRoot, name, `${name}_32.png`)), left: left + 40, top: 440 });
      labels.push(`<text x="${left + 44}" y="433" text-anchor="middle">${name.replace('file_', '')}</text>`);
    }
    layers.push({ input: Buffer.from(`<svg width="640" height="496"><g fill="${theme === 'dark' ? '#d2d6df' : '#3e444e'}" font-family="Arial" font-size="13">${labels.join('')}</g></svg>`), left: 0, top: 0 });
    await sharp({ create: { width: 640, height: 496, channels: 4, background: theme === 'dark' ? '#24262b' : '#eeeeee' } }).composite(layers).png().toFile(path.join(__dirname, `preview-${theme}.png`));
  }
  await fs.writeFile(path.join(__dirname, 'validation.json'), JSON.stringify(validation, null, 2) + '\n');
  console.log(JSON.stringify(validation, null, 2));
}

main().catch(error => { console.error(error); process.exitCode = 1; });
