const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('C:/Users/benso/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');
const root = path.resolve(__dirname, '../../..');
const canonical = path.join(root, 'Funke-Runtime/FunkeAssets/Textures/Editor/General');
const names = ['camera','audio_source','animator','script','particles','light_point','light_spot','light_sun','prefab','prefab_missing','collider_box_2d','collider_circle_2d','collider_polygon_2d','rigidbody_2d','open_folder','collider_box_3d','collider_sphere_3d','collider_capsule_3d','collider_mesh_3d','rigidbody_3d'];
const sizesFor = name => name === 'open_folder' ? [16,32,64,128,256] : [16,32,64,128];
const exportPath = (name,size) => path.join(__dirname,'exports',name,`${name}_${size}.png`);

async function exportIcons(selected) {
  const validation={};
  for(const name of selected) {
    const source=path.join(__dirname,'masters',name+'.png');
    const meta=await sharp(source).metadata();
    if(!meta.hasAlpha)throw new Error('Master has no transparency: '+source);
    await fs.mkdir(path.join(__dirname,'exports',name),{recursive:true});
    validation[name]=[];
    for(const size of sizesFor(name)) {
      const file=exportPath(name,size);
      await sharp(source).resize(size,size,{fit:'contain',kernel:'lanczos3',background:'#00000000'}).png().toFile(file);
      const stats=await sharp(file).stats(), out=await sharp(file).metadata();
      if(out.width!==size||out.height!==size||!out.hasAlpha||stats.channels[3].min!==0||stats.channels[3].max<240)throw new Error('Invalid export: '+file);
      validation[name].push({size,alphaMin:stats.channels[3].min,alphaMax:stats.channels[3].max});
    }
  }
  await fs.writeFile(path.join(__dirname,'validation.json'),JSON.stringify(validation,null,2)+'\n');
  console.log('Validated '+Object.values(validation).flat().length+' PNGs in '+selected.length+' icon families.');
}

async function preview(selected) {
  const width=1000, rows=Math.ceil(selected.length/5), height=rows*214;
  for(const theme of ['dark','light']) {
    const layers=[], labels=[];
    for(let i=0;i<selected.length;i++) {
      const name=selected[i], x=i%5*200, y=Math.floor(i/5)*214;
      layers.push({input:await fs.readFile(exportPath(name,128)),left:x+36,top:y+8});
      for(const [size,offset] of [[16,44],[32,72],[64,116]])layers.push({input:await fs.readFile(exportPath(name,size)),left:x+offset,top:y+146+Math.floor((64-size)/2)});
      labels.push(`<text x="${x+100}" y="${y+143}" text-anchor="middle">${name.replaceAll('_',' ')}</text>`);
    }
    layers.push({input:Buffer.from(`<svg width="${width}" height="${height}"><g fill="${theme==='dark'?'#d9dde4':'#333942'}" font-family="Arial" font-size="13">${labels.join('')}</g></svg>`),left:0,top:0});
    await sharp({create:{width,height,channels:4,background:theme==='dark'?'#24262b':'#eeeeee'}}).composite(layers).png().toFile(path.join(__dirname,`components-${theme}.png`));
  }
}

async function deploy() {
  const destinations=[canonical,...['bin/Release-windows-x86_64/Funke-Editor','bin/Dist-windows-x86_64/Funke-Editor','bin/Dist-windows-x86_64/Funke-Launcher'].map(p=>path.join(root,p,'FunkeAssets/Textures/Editor/General'))];
  for(const destination of destinations) {
    await fs.access(destination);
    let count=0;
    for(const name of names)for(const size of sizesFor(name)) {
      const source=exportPath(name,size), target=path.join(destination,name,`${name}_${size}.png`);
      await fs.copyFile(source,target);
      if(!(await fs.readFile(source)).equals(await fs.readFile(target)))throw new Error('Copy mismatch: '+target);
      count++;
    }
    console.log(`Verified ${count} replacements: ${destination}`);
  }
}

async function main() {
  if(process.argv.includes('--deploy'))return deploy();
  const selected=[];
  for(const name of names) {
    if(process.argv.includes('--available')) { try { await fs.access(path.join(__dirname,'masters',name+'.png')); } catch { continue; } }
    selected.push(name);
  }
  if(!selected.length)throw new Error('No masters available');
  await exportIcons(selected);
  await preview(selected);
}
main().catch(error=>{console.error(error);process.exitCode=1;});
