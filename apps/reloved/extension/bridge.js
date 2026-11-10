const actions = new Set(["ping", "sync", "prepare", "detail"]);
window.addEventListener("message", async event => {
  if(event.source !== window || event.origin !== location.origin || event.data?.channel !== "reloved:request" || !actions.has(event.data.action) || typeof event.data.id !== "string") return;
  const {id,action,payload}=event.data;
  try {
    const response = await chrome.runtime.sendMessage({action,payload});
    window.postMessage({channel:"reloved:response",id,...response},location.origin);
  } catch {window.postMessage({channel:"reloved:response",id,error:"Reloved Connect wurde aktualisiert. Bitte lade diese Seite neu."},location.origin);}
});
