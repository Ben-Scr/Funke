const fs = require('node:fs/promises');
const path = require('node:path');
const sharp = require('C:/Users/benso/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/sharp');
const root = path.resolve(__dirname, '../../..');
const source = 'C:/Users/benso/.codex/generated_images/01a16ff5-97eb-7be3-a946-8a48a4e30f29/exec-03a3977b-66fd-4076-9d79-4eec414d0853.png';
const sizes = [16,32,64,128,256];

async function main() {
  await fs.copyFile(source,path.join(__dirname,'file_model_master.png'));
  const layers=[], labels=[];
  let x=24;
  for(const size of sizes) {
    const file=path.join(__dirname,`file_model_${size}.png`);
    await sharp(source).resize(size,size,{fit:'contain',kernel:'lanczos3',background:'#00000000'}).png().toFile(file);
    const meta=await sharp(file).metadata(), stats=await sharp(file).stats();
    if(meta.width!==size || meta.height!==size || !meta.hasAlpha || stats.channels[3].min!==0 || stats.channels[3].max<240)throw new Error('Invalid export: '+file);
    layers.push({input:await fs.readFile(file),left:x,top:Math.floor((272-size)/2)});
    labels.push(`<text x="${x+size/2}" y="294" text-anchor="middle">${size}px</text>`);
    x+=size+24;
  }
  for(const theme of ['dark','light']) {
    const textLayer={input:Buffer.from(`<svg width="640" height="312"><g fill="${theme==='dark'?'#ccd2dc':'#39404a'}" font-size="14" font-family="Arial">${labels.join('')}</g></svg>`),left:0,top:0};
    await sharp({create:{width:640,height:312,channels:4,background:theme==='dark'?'#24262b':'#eeeeee'}}).composite([...layers,textLayer]).png().toFile(path.join(__dirname,`preview-${theme}.png`));
  }
  const destinations=['Funke-Runtime','bin/Release-windows-x86_64/Funke-Editor','bin/Dist-windows-x86_64/Funke-Editor','bin/Dist-windows-x86_64/Funke-Launcher'];
  for(const destination of destinations) {
    const folder=path.join(root,destination,'FunkeAssets/Textures/Editor/FileIcons/file_model');
    await fs.mkdir(folder,{recursive:true});
    for(const size of sizes) {
      const name=`file_model_${size}.png`;
      await fs.copyFile(path.join(__dirname,name),path.join(folder,name));
      if(!(await fs.readFile(path.join(__dirname,name))).equals(await fs.readFile(path.join(folder,name))))throw new Error('Copy mismatch: '+folder);
    }
    console.log('Verified 5 PNGs: '+folder);
  }
}
main().catch(error=>{console.error(error);process.exitCode=1;});
