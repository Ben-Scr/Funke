const fs = require('node:fs/promises');
const path = require('node:path');
const crypto = require('node:crypto');
const sharp = require('C:/Users/benso/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');

const root = path.resolve(__dirname, '../../..');
const assetRoot = path.join(root, 'Funke-Runtime/FunkeAssets/Textures/Editor/FileIcons');
const names = ['file_json', 'file_xml', 'file_bin', 'file_font', 'file_audio', 'file_anim', 'file_prefab', 'file_dataasset', 'file_shader', 'file_fallback', 'folder_zip'];
const allNames = ['file_cs', 'file_txt', 'file_scene', ...names];
const sizes = [16, 32, 64, 128, 256];
const labelFor = { file_cs: 'C#', file_txt: 'Text', file_scene: 'Scene', file_json: 'JSON', file_xml: 'XML', file_bin: 'Binary', file_font: 'Font', file_audio: 'Audio', file_anim: 'Animation', file_prefab: 'Prefab', file_dataasset: 'Data Asset', file_shader: 'Shader', file_fallback: 'Generic file', folder_zip: 'ZIP' };

async function verify(file, size) {
  const meta = await sharp(file).metadata();
  if (meta.width !== size || meta.height !== size || !meta.hasAlpha) throw new Error('Invalid PNG dimensions or alpha: ' + file);
  const {data, info} = await sharp(file).ensureAlpha().raw().toBuffer({resolveWithObject:true});
  let transparent = 0, opaque = 0, border = 0;
  for (let y = 0; y < info.height; y++) {
    for (let x = 0; x < info.width; x++) {
      const alpha = data[(y * info.width + x) * 4 + 3];
      if (alpha === 0) transparent++;
      if (alpha === 255) opaque++;
      if ((x === 0 || y === 0 || x === info.width - 1 || y === info.height - 1) && alpha > 32) border++;
    }
  }
  if (!transparent || !opaque) throw new Error('Invalid transparent/opaque content: ' + file);
  return { size, transparent, opaque, borderPixelsAbove32: border, sha256: crypto.createHash('sha256').update(await fs.readFile(file)).digest('hex') };
}

async function exportIcons() {
  const report = {};
  for (const name of names) {
    const source = path.join(__dirname, 'masters', name + '.png');
    const meta = await sharp(source).metadata();
    if (!meta.hasAlpha) throw new Error('Missing source alpha: ' + name);
    const folder = path.join(__dirname, 'exports', name);
    await fs.mkdir(folder, { recursive: true });
    report[name] = [];
    for (const size of sizes) {
      if (size === 256 && name === 'file_prefab') continue;
      const file = path.join(folder, `${name}_${size}.png`);
      await sharp(source).resize(size, size, { fit: 'contain', kernel: 'lanczos3', background: '#00000000' }).png().toFile(file);
      report[name].push(await verify(file, size));
    }
  }
  await fs.writeFile(path.join(__dirname, 'validation.json'), JSON.stringify(report, null, 2) + '\n');
  console.log('Exported and verified ' + Object.values(report).flat().length + ' transparent PNGs.');
}

function iconFile(name, size) {
  return path.join(names.includes(name) ? path.join(__dirname, 'exports') : assetRoot, name, `${name}_${size}.png`);
}

async function overview() {
  const columns = 7, cellWidth = 200, cellHeight = 220;
  const overlays = [], labels = [];
  for (let i = 0; i < allNames.length; i++) {
    const name = allNames[i], x = (i % columns) * cellWidth, y = Math.floor(i / columns) * cellHeight;
    overlays.push({input: await fs.readFile(iconFile(name, 128)), left: x + 36, top: y + 12});
    labels.push(`<text x="${x + 100}" y="${y + 165}" text-anchor="middle">${labelFor[name]}</text>`);
    overlays.push({input:await fs.readFile(iconFile(name,16)),left:x+72,top:y+186});
    overlays.push({input:await fs.readFile(iconFile(name,32)),left:x+104,top:y+178});
  }
  const labelsSvg = Buffer.from(`<svg width="1400" height="440"><g fill="#d9dde4" font-family="Arial" font-size="16">${labels.join('')}</g></svg>`);
  overlays.push({ input:labelsSvg, left:0, top:0 });
  await sharp({create:{width:1400,height:440,channels:4,background:'#24262b'}}).composite(overlays).png().toFile(path.join(__dirname,'all-file-icons.png'));

  for (const background of ['dark', 'light']) {
    const composites = [], texts = [];
    for (let i = 0; i < names.length; i++) {
      let x = 160;
      const y = i * 284;
      texts.push(`<text x="16" y="${y+150}">${labelFor[names[i]]}</text>`);
      for (const size of sizes) {
        if(size===256 && names[i]==='file_prefab') continue;
        composites.push({input:await fs.readFile(iconFile(names[i],size)),left:x,top:y+Math.floor((256-size)/2)});
        texts.push(`<text x="${x+size/2}" y="${y+277}" text-anchor="middle">${size}</text>`);
        x += size+24;
      }
    }
    const height=names.length*284;
    composites.push({input:Buffer.from(`<svg width="800" height="${height}"><g fill="${background==='dark'?'#c9ced8':'#40454e'}" font-family="Arial" font-size="15">${texts.join('')}</g></svg>`),left:0,top:0});
    const sheet = await sharp({create:{width:800,height,channels:4,background:background==='dark'?'#24262b':'#ededed'}}).composite(composites).png().toBuffer();
    await fs.writeFile(path.join(__dirname,`sizes-${background}.png`),sheet);
    for(let page=0;page<Math.ceil(names.length/4);page++) {
      const top=page*4*284, pageHeight=Math.min(4*284,height-top);
      await sharp(sheet).extract({left:0,top,width:800,height:pageHeight}).png().toFile(path.join(__dirname,`sizes-${background}-${page+1}.png`));
    }
  }
}

async function deploy() {
  const destinations = [assetRoot, ...['bin/Release-windows-x86_64/Funke-Editor','bin/Dist-windows-x86_64/Funke-Launcher','bin/Dist-windows-x86_64/Funke-Editor'].map(dir=>path.join(root,dir,'FunkeAssets/Textures/Editor/FileIcons'))];
  for (const destination of destinations) {
    await fs.access(destination);
    let count=0;
    for(const name of names) {
      for(const size of sizes) {
        if(size===256 && name==='file_prefab')continue;
        const source=iconFile(name,size), target=path.join(destination,name,`${name}_${size}.png`);
        await fs.copyFile(source,target);
        if(!(await fs.readFile(source)).equals(await fs.readFile(target)))throw new Error('Copy mismatch: '+target);
        count++;
      }
    }
    console.log(`Verified ${count} replacements: ${destination}`);
  }
}

async function main() {
  if(process.argv.includes('--deploy')) { await deploy(); return; }
  await exportIcons();
  await overview();
}

main().catch(error=>{console.error(error);process.exitCode=1;});
