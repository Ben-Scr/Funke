export const allowedHosts = /^www\.vinted\.(de|fr|nl|be|it|es|at|pl|pt|com|co\.uk)$/;
export function profileParts(profile) {
  let u;try{u=new URL(profile);}catch{throw new Error("Bitte gib den vollständigen Link zu deinem Vinted-Profil ein.");}
  const match=u.pathname.match(/^\/member\/(\d+)(?:-|\/|$)/);
  if(u.protocol!=="https:" || !allowedHosts.test(u.hostname) || !match)throw new Error("Bitte verwende deinen Vinted-Profillink, zum Beispiel https://www.vinted.de/member/123456-name.");
  return {origin:u.origin,id:match[1]};
}
export function toItem(raw,origin) {
  if(!raw || !/^\d+$/.test(String(raw.id)) || typeof raw.title!=="string")throw new Error("Vinted liefert ein unbekanntes Artikelformat. Der Import wurde abgebrochen.");
  const price=Number(raw.price?.amount??raw.price?.value??raw.price??0);
  const photoList=raw.photos?.length?raw.photos:[raw.photo].filter(Boolean);
  const photos=photoList.map(p=>p?.full_size_url||p?.url).filter(p=>typeof p==="string"&&p.startsWith("https://"));
  return {id:`vinted:${new URL(origin).hostname}:${raw.id}`,title:raw.title,brand:raw.brand_title||raw.brand?.title||"",size:raw.size_title||raw.size?.title||"",price,description:raw.description||"",photos,status:raw.is_sold?"sold":raw.is_closed||raw.is_hidden?"archived":"active",category:raw.catalog_title||"Sonstiges",createdAt:raw.created_at_ts?new Date(raw.created_at_ts*1000).toISOString():new Date().toISOString(),queued:false,source:"vinted",url:`${origin}/items/${raw.id}`};
}
export async function collectInventory(request,origin,userId) {
  const result=[];const seen=new Set();
  for(let page=1;page<=100;page++){
    const body=await request(`/api/v2/users/${userId}/items?page=${page}&per_page=96`);
    if(!Array.isArray(body.items))throw new Error("Vinted hat sein Antwortformat geändert. Es wurden keine Daten überschrieben.");
    const fresh=body.items.filter(i=>!seen.has(i.id));
    if(body.items.length&&!fresh.length)throw new Error("Vinted wiederholt dieselbe Seite. Der Import wurde abgebrochen, statt ein unvollständiges Inventar zu speichern.");
    for(const raw of fresh){seen.add(raw.id);result.push(toItem(raw,origin));}
    if(!body.items.length || (body.pagination?.total_pages && page>=body.pagination.total_pages) || (!body.pagination?.total_pages&&body.items.length<96))return result;
  }
  throw new Error("Das Inventar überschreitet die Importgrenze. Es wurden keine Daten überschrieben.");
}
