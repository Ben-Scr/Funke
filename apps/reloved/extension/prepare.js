(async()=>{
  const {result:item}=await chrome.runtime.sendMessage({action:"pending"});
  if(!item)return;
  const host=document.createElement("div");host.style.cssText="position:fixed;right:18px;bottom:18px;width:340px;max-width:calc(100vw - 36px);z-index:2147483647";
  const root=host.attachShadow({mode:"closed"});
  const style=document.createElement("style");style.textContent="*{box-sizing:border-box}section{background:#fafbf6;border:1px solid #ced8c0;border-radius:15px;padding:22px;box-shadow:0 12px 55px #16211233;color:#35452b;font:13px system-ui;max-height:75vh;overflow:auto}h2{font-size:20px;margin:0 0 12px;letter-spacing:-.7px}p{font-size:12px;line-height:1.6;color:#7b896e}button{border:1px solid #d6dfca;border-radius:6px;padding:9px 12px;background:#e9efdf;color:#476230;cursor:pointer;margin:5px 5px 5px 0;font:12px system-ui}button.primary{background:#506540;color:white;width:100%;margin-top:12px}.close{float:right;border:0;background:none;margin:0;padding:0;font-size:20px}textarea{width:100%;border:1px solid #d6dfca;border-radius:5px;padding:8px;font:12px system-ui;color:#35452b}a{font-size:11px;color:#506540;display:inline-block;margin:7px 9px 0 0}strong{font-size:13px}small{font-size:11px;color:#879776}";root.append(style);
  const panel=document.createElement("section");root.append(panel);
  function el(tag,text){const e=document.createElement(tag);if(text)e.textContent=text;panel.append(e);return e;}
  el("button","×").addEventListener("click",()=>host.remove());panel.firstChild.className="close";
  el("h2","reloved · Dein Reupload");el("strong",item.title);el("p",`${item.brand} · ${item.size} · ${item.price.toFixed(2)} €`);
  el("p","Prüfe alle Angaben und lade deine Fotos hoch. Kategorie, Größe, Zustand und Versand wählst du direkt auf Vinted. Veröffentlicht wird nur durch dich.");
  const status=el("p","");status.setAttribute("role","status");
  function fill(){
    const fields=[['input[name="title"], input#title',item.title],['textarea[name="description"], textarea#description',item.description],['input[name="price"], input#price',String(item.price)]];
    let filled=0;
    for(const [selector,value] of fields){const field=document.querySelector(selector);if(!field||field.value)continue;const prototype=field.tagName==="TEXTAREA"?HTMLTextAreaElement.prototype:HTMLInputElement.prototype;Object.getOwnPropertyDescriptor(prototype,"value").set.call(field,value);field.dispatchEvent(new Event("input",{bubbles:true}));field.dispatchEvent(new Event("change",{bubbles:true}));filled++;}
    status.textContent=filled?`${filled} leere Felder vorausgefüllt. Bitte kontrolliere die Werte.`:"Keine passenden leeren Felder gefunden. Du kannst die Angaben unten kopieren.";
  }
  const fillButton=el("button","Leere Felder ausfüllen");fillButton.className="primary";fillButton.onclick=fill;
  for(const [label,value] of [["Titel",item.title],["Beschreibung",item.description],["Preis",String(item.price)]]){const button=el("button",`${label} kopieren`);button.onclick=async()=>{try{await navigator.clipboard.writeText(value);status.textContent=`${label} kopiert.`;}catch{status.textContent="Bitte Text manuell kopieren.";const t=el("textarea");t.value=value;t.select();}};}
  if(item.photos.length){el("p","Fotos öffnen und speichern, anschließend in Vinted hochladen:");for(const [index,url] of item.photos.entries()){const a=el("a",`Foto ${index+1} ↗`);a.href=url;a.target="_blank";a.rel="noopener noreferrer";if(url.startsWith("data:"))a.download=`reloved-foto-${index+1}.jpg`;}}
  el("p","Vorhandene Eingaben werden nicht überschrieben. Die alte Anzeige wird von Reloved nicht gelöscht.");document.documentElement.append(host);
})();
