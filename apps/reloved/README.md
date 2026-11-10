# Reloved — Dein Resale Studio

Eine lokale Web-App mit responsiver Oberfläche für Inventar, Entwürfe und die Vorbereitung neuer Vinted-Anzeigen. Chrome-/Edge-Erweiterung für reguläre Vinted-Konten enthalten. Kein Pro-Konto erforderlich; die tatsächliche Kompatibilität der internen Vinted-API muss mit einem angemeldeten Konto geprüft werden.

## Starten

Unter Windows `Start-Reloved.cmd` öffnen. Die App läuft unter **http://127.0.0.1:5173**. Voraussetzung: Node.js 24 oder neuer. Die vorbereitete Arbeitskopie enthält bereits alle Abhängigkeiten und den fertigen Build. Bei einer entpackten Kopie installiert der Starter die Abhängigkeiten mit npm. Den ursprünglichen Ordner bis zum Ende der Installation geöffnet lassen.

Alternativ: `npm ci`, `npm run build`, dann `node node_modules/vinext/dist/cli.js start --hostname 127.0.0.1 --port 5173`.

## Vinted verbinden

1. Den Ordner `extension` verwenden oder `public/reloved-connect.zip` entpacken.
2. In Chrome `chrome://extensions`, in Edge `edge://extensions` öffnen.
3. Entwicklermodus aktivieren, „Entpackte Erweiterung laden“, den Erweiterungsordner auswählen.
4. Vinted im selben Browser öffnen und normal anmelden. Den Tab geöffnet lassen.
5. Reloved neu laden. Unter „Einstellungen“ den Link zum eigenen Vinted-Profil eintragen und „Inventar abrufen“ auswählen.

Die Erweiterung unterstützt die Vinted-Websites für DE, FR, NL, BE, IT, ES, AT, PL, PT, COM und UK. Diese Hostliste ist keine Garantie für Verfügbarkeit in allen Märkten. Im eingebauten Codex-Browser lässt sich die Chrome-Erweiterung nicht nutzen; dafür die App in Chrome oder Edge öffnen.

## Funktionsumfang

- Artikel anlegen und bearbeiten; Fotos, Titel, Beschreibung, Preis, Marke, Größe, Kategorie und lokalen Status verwalten.
- Suche, Status- und Kategoriefilter, Preis-/Datumssortierung, Raster- und Listenansicht.
- Mehrfachauswahl für Reupload-Studio und lokales Archivieren.
- JSON-Backup exportieren und importieren. Imports werden vollständig validiert, bevor bestehende Daten verändert werden.
- Auswertung der tatsächlich gespeicherten Artikel und Angebotspreise; keine erfundenen Live-Statistiken.
- Inventarabruf über `/api/v2/users/{id}/items`, mit Pagination und Abbruch bei Fehlern. Nicht gelieferte Artikel werden niemals automatisch gelöscht oder als verkauft markiert.
- Beschreibung und Fotos bei Bedarf im Artikeleditor über `/api/v2/items/{id}` nachladen. Falls Vinted diese Schnittstelle nicht mehr unterstützt, ist manuelle Ergänzung erforderlich.
- Reupload-Assistent öffnet Vinteds Artikel-Formular. Titel, Beschreibung und Preis lassen sich in erkannte, leere Felder übertragen. Fotos lassen sich im Assistenten öffnen und herunterladen. Kategorie, Größe, Zustand, Fotos und endgültige Veröffentlichung erfolgen auf Vinted durch den Nutzer.

## Grenzen und Datenschutz

**Kein vollautomatischer Reupload.** Die Erweiterung löscht und veröffentlicht keine Anzeigen. Alte Anzeigen müssen vor dem erneuten Einstellen selbst auf Vinted entfernt werden. Der lokale Status wird nicht als Beleg einer erfolgreichen Vinted-Veröffentlichung behandelt.

Vinteds offizielle [Pro Integrations API](https://pro-docs.svc.vinted.com/) ist auf freigeschaltete Pro-Unternehmen beschränkt. Für normale Konten nutzt Connect daher die nicht offiziell dokumentierte Web-API. Die Implementierung des Artikelabrufs orientiert sich außerdem an [öffentlich einsehbarem Client-Code](https://github.com/kennyfitzgerald/vinted/blob/master/vinted_api.py). Endpunkte, Antworten und Formularfelder können sich ändern. HTTP 401, 403, 429 und unerwartete Antworten brechen den Abruf ab; keine Captcha- oder Zugriffssperren werden umgangen. Ohne angemeldetes Konto wurde keine erfolgreiche Live-Verbindung nachgewiesen.

Passwörter und Sitzungscookies werden weder ausgelesen noch exportiert. API-Anfragen laufen direkt im Vinted-Tab mit dessen bestehender Sitzung. Keine externen Backend-Dienste speichern Inventardaten. Die Erweiterung darf nur mit den fest eingetragenen Reloved-Adressen kommunizieren. Übertragene Reupload-Daten werden kurzzeitig im Sitzungsspeicher der Erweiterung gehalten und beim Schließen des Vinted-Tabs entfernt.

Das Inventar liegt im lokalen Browserspeicher. Es wird nicht geräteübergreifend synchronisiert. `localhost` und `127.0.0.1` haben getrennte Speicher; benutze immer **127.0.0.1:5173**. Browserdaten löschen entfernt das Inventar. Regelmäßig JSON exportieren. Fotos bis 1,5 MB pro Datei, maximal zehn je Artikel; bei erschöpftem Browserspeicher erscheint eine ausdrückliche Speicherwarnung. Externe Produktfotos und Schriftarten benötigen Internet. Die anfänglichen Produktfotos stammen von Unsplash und sind nur Beispieldaten.

## Entwicklung und Prüfung

`npm run dev` startet die Entwicklungsansicht. `node --test tests/inventory.test.mjs` prüft Importvalidierung, URL-Sicherheit, API-Mapping und Pagination. `node node_modules/typescript/bin/tsc --noEmit` prüft die Typen.

Am 10.11.2026 geprüft: Produktionsbuild, sieben automatisierte Tests, Suche, Bearbeitung, Speicherung nach Neuladen, Reupload-Dialog und Layout bei 390 px sowie Desktopbreite. Nicht verifiziert: Live-Login, tatsächliche Inventarantworten und Formularübertragung auf Vinted. Es wurde keine echte Anzeige verändert.

Die Anwendung bleibt in diesem Repository auf dem vorhandenen Branch. Es wurden keine zusätzlichen Branches und keine Commits erstellt. Eine private Hosting-Registrierung ist vorbereitet, aber nicht veröffentlicht; die lokale App benötigt sie nicht.
