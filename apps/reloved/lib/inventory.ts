export type Item = {id:string;title:string;brand:string;size:string;price:number;description:string;photos:string[];status:"active"|"draft"|"sold"|"archived";category:string;createdAt:string;queued:boolean;source:"local"|"vinted";url?:string};
export type Inventory = {demo:boolean;items:Item[];profile:string;lastSync:string|null};
export const money=(n:number)=>new Intl.NumberFormat("de-DE",{style:"currency",currency:"EUR"}).format(n);
export function normalizeItems(value:unknown):Item[]{
 if(!Array.isArray(value)||value.length>10000)throw new Error("Keine gültige Inventardatei (maximal 10.000 Artikel).");
 const ids=new Set<string>();
 return value.map(v=>{if(!v||typeof v.id!=="string"||v.id.length>150||ids.has(v.id)||typeof v.title!=="string"||!v.title.trim()||typeof v.price!=="number"||!Number.isFinite(v.price)||v.price<0||v.price>100000||!["active","draft","sold","archived"].includes(v.status))throw new Error("Die Datei enthält ungültige oder doppelte Artikel.");ids.add(v.id);
 const safePhoto=(p:unknown):p is string=>typeof p==="string"&&(/^https:\/\//.test(p)||/^data:image\/(jpeg|png|webp);base64,/.test(p));
 let url:string|undefined;try{const u=new URL(v.url);if(u.protocol==="https:"&&/^www\.vinted\.(de|fr|nl|be|it|es|at|pl|pt|com|co\.uk)$/.test(u.hostname))url=u.href;}catch{}
 return {id:v.id.slice(0,150),title:v.title.slice(0,100),brand:String(v.brand??"").slice(0,100),size:String(v.size??"").slice(0,30),price:v.price,description:String(v.description??"").slice(0,2000),photos:Array.isArray(v.photos)?v.photos.filter(safePhoto).slice(0,10):[],status:v.status,category:String(v.category??"Sonstiges").slice(0,100),createdAt:Number.isFinite(Date.parse(v.createdAt))?new Date(v.createdAt).toISOString():new Date().toISOString(),queued:v.queued===true,source:v.source==="vinted"?"vinted":"local",url};});
}
const photo=(id:string)=>`https://images.unsplash.com/${id}?auto=format&fit=crop&w=680&q=85`;
export const demoItems:Item[]=[
 ["Tailored Hose in zartem Rosé","ARKET","M / 38",65,"Hosen","photo-1594633312681-425c7b97ccd1","active"],
 ["Die perfekte Straight-Leg Jeans","LEVI’S","W28 / L30",42,"Hosen","photo-1542272604-787c3835535d","active"],
 ["Air Force 1 · Caramel","NIKE","39",48,"Schuhe","photo-1549298916-b41d501d3772","active"],
 ["Schultertasche · Black Leather","GUCCI","One Size",59,"Accessoires","photo-1548036328-c9fa89d128fa","draft"],
 ["Denimhemd mit kleinen Details","RALPH LAUREN","M",35,"Oberteile","photo-1596755094514-f87e34085b2c","active"],
 ["Strickponcho für kühle Tage","& OTHER STORIES","S / 36",38,"Oberteile","photo-1434389677669-e08b4cac3105","active"],
 ["Lederjacke · Vintage Edit","MANGO","M / 38",54,"Jacken & Mäntel","photo-1551028719-00167b16eac5","sold"],
 ["Sonnenbrille · The Weekend Edit","RAY-BAN","One Size",45,"Accessoires","photo-1511499767150-a48a237f0083","draft"]
].map((r,i)=>({id:`demo-${i}`,title:String(r[0]),brand:String(r[1]),size:String(r[2]),price:Number(r[3]),category:String(r[4]),photos:[photo(String(r[5]))],status:r[6] as Item["status"],description:"Ein gut erhaltenes Lieblingsstück aus meinem Kleiderschrank. Gepflegt, aus einem rauchfreien Haushalt. Bei Fragen schreib mir gerne!",createdAt:new Date(Date.UTC(2026,10,10-i)).toISOString(),queued:i===0||i===4,source:"local"}));
