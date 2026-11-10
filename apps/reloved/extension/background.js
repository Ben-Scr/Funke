import {profileParts,collectInventory,allowedHosts,toItem} from "./api.js";
const appOrigins=new Set(["http://localhost:5173","http://127.0.0.1:5173","https://reloved-wardrobe-studio.rrainix.chatgpt.site"]);
const syncing=new Set();
chrome.runtime.onMessage.addListener((message,sender,respond)=>{
  route(message,sender).then(result=>respond({result})).catch(e=>respond({error:e.message||"Die Verbindung ist fehlgeschlagen."}));
  return true;
});
async function route(message,sender){
  const url=new URL(sender.url||"https://invalid.local");
  if(message.action==="pending"){
    if(!sender.tab || !allowedHosts.test(url.hostname) || url.pathname!=="/items/new")throw new Error("Nicht verfügbar.");
    const key=`prepare:${sender.tab.id}`;const data=(await chrome.storage.session.get(key))[key];
    if(!data||Date.now()-data.createdAt>30*60*1000)return null;
    return data.item;
  }
  if(!appOrigins.has(url.origin))throw new Error("Diese Seite ist nicht für Reloved Connect freigegeben.");
  if(message.action==="ping")return {version:chrome.runtime.getManifest().version};
  if(message.action==="detail"){
    const itemUrl=new URL(message.payload?.url);
    const id=itemUrl.pathname.match(/^\/items\/(\d+)(?:-|\/|$)/)?.[1];
    if(itemUrl.protocol!=="https:"||!allowedHosts.test(itemUrl.hostname)||!id)throw new Error("Ungültiger Artikellink.");
    const tabs=await chrome.tabs.query({url:`${itemUrl.origin}/*`});
    if(!tabs[0]?.id)throw new Error("Bitte öffne zuerst einen angemeldeten Vinted-Tab.");
    const [reply]=await chrome.scripting.executeScript({target:{tabId:tabs[0].id},world:"MAIN",func:async id=>{
      try{const r=await fetch(`/api/v2/items/${id}`,{credentials:"include",headers:{Accept:"application/json"},signal:AbortSignal.timeout(10000)});if(!r.ok)return null;return await r.json();}catch{return null;}
    },args:[id]});
    if(!reply?.result?.item)throw new Error("Vinted liefert die Artikeldetails momentan nicht über diese Schnittstelle. Ergänze fehlende Angaben bitte manuell.");
    return {item:toItem(reply.result.item,itemUrl.origin)};
  }
  if(message.action==="sync"){
    const {origin,id}=profileParts(message.payload?.profile);
    if(syncing.has(origin))throw new Error("Ein Abgleich läuft bereits. Bitte warte kurz.");
    const tabs=await chrome.tabs.query({url:`${origin}/*`});
    const tab=tabs.find(t=>t.active)||tabs[0];
    if(!tab?.id)throw new Error("Öffne Vinted im selben Browser, melde dich an und versuche es erneut.");
    syncing.add(origin);
    try {
      const items=await collectInventory(async path=>{
        const [response]=await chrome.scripting.executeScript({target:{tabId:tab.id},world:"MAIN",func:async path=>{
          try {
            const res=await fetch(path,{credentials:"include",headers:{Accept:"application/json"},signal:AbortSignal.timeout(15000)});
            if(!res.ok)return {error:res.status};
            if(!res.headers.get("content-type")?.includes("application/json"))return {error:"format"};
            return {body:await res.json()};
          } catch {return {error:"network"};}
        },args:[path]});
        if(response?.result?.error){const status=response.result.error;throw new Error(status===401?"Bitte melde dich erneut bei Vinted an.":status===403?"Vinted hat den Zugriff abgelehnt. Öffne den Vinted-Tab und kläre dort eine mögliche Sicherheitsabfrage.":status===429?"Vinted begrenzt gerade die Anfragen. Bitte warte und versuche es später erneut.":"Vinteds interne Schnittstelle antwortet nicht wie erwartet. Deine bestehenden Daten bleiben erhalten.");}
        if(!response?.result?.body)throw new Error("Keine Antwort vom Vinted-Tab.");
        return response.result.body;
      },origin,id);
      return {items};
    }finally{syncing.delete(origin);}
  }
  if(message.action==="prepare"){
    const item=message.payload?.item;
    if(!item||typeof item.title!=="string"||item.title.length>100||typeof item.price!=="number"||!Number.isFinite(item.price)||item.price<0)throw new Error("Bitte prüfe die Artikeldaten.");
    if(String(item.id).startsWith("demo-")||item.status==="sold")throw new Error("Beispielartikel und verkaufte Artikel können nicht erneut eingestellt werden.");
    let origin="https://www.vinted.de";
    if(item.url){const original=new URL(item.url);if(original.protocol!=="https:"||!allowedHosts.test(original.hostname))throw new Error("Ungültiger Vinted-Link.");origin=original.origin;}
    const tab=await chrome.tabs.create({url:"about:blank",active:true});
    await chrome.storage.session.set({[`prepare:${tab.id}`]:{createdAt:Date.now(),item:{title:item.title,description:String(item.description||"").slice(0,2000),price:item.price,brand:String(item.brand||""),size:String(item.size||""),photos:(item.photos||[]).filter(p=>typeof p==="string"&&(/^https:\/\//.test(p)||/^data:image\/(jpeg|png|webp);base64,/.test(p))).slice(0,10)}}});
    await chrome.tabs.update(tab.id,{url:`${origin}/items/new`});
    return {prepared:true};
  }
  throw new Error("Unbekannte Aktion.");
}
chrome.tabs.onRemoved.addListener(id=>chrome.storage.session.remove(`prepare:${id}`));
