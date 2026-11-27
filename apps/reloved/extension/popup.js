const approve = document.querySelector("#approve");
const status = document.querySelector("#status");

approve.addEventListener("click", async () => {
  approve.disabled = true;
  status.textContent = "Studio-Seite wird geprüft …";
  try {
    const [tab] = await chrome.tabs.query({active: true, currentWindow: true});
    if (!tab?.id) throw new Error("Kein aktiver Tab gefunden.");
    const response = await chrome.tabs.sendMessage(tab.id, {channel: "reloved:approve"});
    if (!response?.paired) throw new Error(response?.error || "Diese Seite ist kein unterstütztes Reloved Studio.");
    approve.textContent = "Seite verbunden";
    status.textContent = "Nur dieser Tab darf Reloved Connect jetzt verwenden.";
  } catch {
    status.textContent = "Öffne Reloved im aktiven Tab und versuche es erneut.";
    approve.disabled = false;
  }
});
