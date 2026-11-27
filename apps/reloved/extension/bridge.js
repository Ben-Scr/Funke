const actions = new Set(["ping", "sync", "prepare", "detail"]);
let pairingToken;
chrome.runtime.onMessage.addListener((message,sender,respond)=>{
  if(message?.channel!=="reloved:approve"||sender.id!==chrome.runtime.id||sender.url!==chrome.runtime.getURL("popup.html"))return;
  chrome.runtime.sendMessage({action:"pair"}).then(response=>{
    if(typeof response?.result?.token!=="string")throw new Error(response?.error||"Die Freigabe ist fehlgeschlagen.");
    pairingToken=response.result.token;
    respond({paired:true,expiresAt:response.result.expiresAt});
  }).catch(error=>respond({paired:false,error:error.message||"Die Freigabe ist fehlgeschlagen."}));
  return true;
});
window.addEventListener("message", async event => {
  if(event.source !== window || event.origin !== location.origin || event.data?.channel !== "reloved:request" || !actions.has(event.data.action) || typeof event.data.id !== "string") return;
  const {id,action,payload}=event.data;
  try {
    const response = await chrome.runtime.sendMessage({action,payload,token:pairingToken});
    window.postMessage({channel:"reloved:response",id,...response},location.origin);
  } catch {window.postMessage({channel:"reloved:response",id,error:"Reloved Connect wurde aktualisiert. Bitte lade diese Seite neu."},location.origin);}
});
