export const PAIRING_TTL_MS = 30 * 60 * 1000;

const appOrigins = new Set([
  "http://localhost:5173",
  "http://127.0.0.1:5173",
  "https://reloved-wardrobe-studio.rrainix.chatgpt.site",
]);

export function requireAppDocument(sender) {
  let url;
  try {
    url = new URL(sender.url);
  } catch {
    throw new Error("Diese Seite ist nicht für Reloved Connect freigegeben.");
  }
  const tabId = sender.tab?.id;
  if (!Number.isInteger(tabId) || sender.frameId !== 0 || typeof sender.documentId !== "string" || !appOrigins.has(url.origin)) {
    throw new Error("Diese Seite ist nicht für Reloved Connect freigegeben.");
  }
  return {tabId, documentId: sender.documentId};
}

export class PairingRegistry {
  constructor({storage = chrome.storage.session, now = () => Date.now(), createToken = () => crypto.randomUUID()} = {}) {
    this.storage = storage;
    this.now = now;
    this.createToken = createToken;
  }

  key(tabId) {
    return `pairing:${tabId}`;
  }

  async approve(sender) {
    const document = requireAppDocument(sender);
    const token = this.createToken();
    if (typeof token !== "string" || token.length < 32) throw new Error("Reloved Connect konnte keine sichere Freigabe erstellen.");
    const expiresAt = this.now() + PAIRING_TTL_MS;
    await this.storage.set({[this.key(document.tabId)]: {documentId: document.documentId, token, expiresAt}});
    return {token, expiresAt};
  }

  async authorize(sender, token) {
    const document = requireAppDocument(sender);
    const key = this.key(document.tabId);
    const pairing = (await this.storage.get(key))[key];
    if (!pairing || pairing.documentId !== document.documentId || pairing.expiresAt <= this.now() || typeof token !== "string" || pairing.token !== token) {
      throw new Error("Öffne Reloved Connect auf dieser Studio-Seite und wähle „Diese Seite verbinden“.");
    }
    return document;
  }

  removeTab(tabId) {
    return this.storage.remove(this.key(tabId));
  }
}
