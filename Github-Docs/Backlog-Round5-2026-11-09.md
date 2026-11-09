# Backlog-Runde 5 — 9. November 2026

Bearbeitet: **INP-005, NET-003, NET-004**. Engine `master` auf
`ed9552a2e0a8269dd6d5322271fae0983c3b5ad9`, Paketrepository auf bestehendem `main`,
jeweils mit vorhandenen lokalen Änderungen. Keine Branches, Commits, Pushes oder
Veröffentlichungen. Produktionsänderungen dieser Runde liegen im benachbarten
`Index-PackageRegistry`, nicht im nativen Enginecode.

## INP-005 — erledigt

`InputInteraction` ergänzt optionale Tap-/Hold-Zustandsautomaten für Button-Actions:
`Waiting`, `Started`, `Performed`, `Canceled` sowie framegebundene Übergangsflags.
Tap akzeptiert Loslassen bis einschließlich seiner Zeitgrenze. Hold löst beim ersten
noch gehaltenen Sample ab seiner Mindestdauer einmal aus; auf einem später beobachteten
Loslassen wird kein Hold nachgeholt. Beide verwenden die vorhandene injizierbare
unskalierte Zeitquelle. Mehrere Abfragen oder eine Map-Invalidierung schalten die
Interaktion im selben Frame nicht erneut weiter.

Deaktivierung, Rebinding, Override, Reset, Fokus-/Pausebehandlung im Beispiel und
Controllerverlust brechen offene Interaktionen ab. Eine gehaltene Taste darf nach
Reaktivierung nicht versehentlich starten. Ungültige oder rückwärts laufende Zeit sowie
Frame-Rücksprünge brechen ebenfalls ab. Bestehendes Polling und Ereignispuffer bleiben
unverändert; Interaktionen sind zusätzliche, explizit angelegte Laufzeitobjekte.

Dateien im Paketrepository:

- `packages/Funke.Input/Source/InputInteraction.cs` — neue API und Zustandsautomat.
- `packages/Funke.Input/Source/InputAction.cs` — Integration in Sampling und Lifecycle.
- `packages/Funke.Input/Examples/TapHoldInputExample.cs` — Fokus, Anwendungspause,
  Spielpause und Wiederaufnahme; injizierbare Quelle für die Beispielprüfung.
- `tests/Funke.Input.Tests/InteractionTests.cs` — 38 neue Fälle mit FakeInputSource.
- `packages/Funke.Input/README.md` — Zeit-, Lifecycle- und Nutzungskontrakt.

Abnahme: **245/245 Input-Tests bestanden**, keine Skips. Darin sind 38 neue Interaktions-
und Beispieltests enthalten: genaue Zeitgrenzen, Wiederholungsabfragen, ungültige Zeit,
Pause/Fokus, Rebinding, Controllerverlust, Disposen und erneute Aktivierung. Paketquellen
und Beispiele werden im Testprojekt gegen die echte Release-ScriptCore-Assembly gebaut.
Der Test benötigt keine Wartezeiten und keine nativen Input-Bindings.

Nicht ausgeführt: physische Controller-/OS-Fokusabnahme und ein nativer Editorlauf.
Die Aufgabe verlangt den deterministischen Quellen-/Zeitvertrag; reale Geräteerkennung
bleibt Aufgabe der vorhandenen Inputquelle. Fokus und Pause werden bewusst durch das
Spiel beziehungsweise das Beispiel gemeldet. Dauer und Interaktionsart sind nicht Teil
des Action-Asset- oder Rebinding-Profilschemas. Sourceversion **Funke.Input 0.2.0** bleibt
unveröffentlicht; bestehende Registry-Downloads erhalten diese Änderung noch nicht.

## NET-003 — vorhandene Umsetzung bestätigt, Mehrprozessnachweis ergänzt

Der bei Beginn bereits vorhandene Paketstand bietet `OwnerOnly`, `ServerOnly`,
`CustomAuthorization` und `RegisterPublicServerHandler`. Die Entscheidung verwendet
Transportabsender und aktuellen serverseitigen Owner vor Konstruktion und Deserialisierung
des Payloads. Im bisherigen Backlog war die vorhandene Abnahme aus
`packages/Funke.Networking/Tests/LoadBudgets.md` noch nicht verknüpft.

Neu ergänzt im Paketrepository:

- `tests/Funke.Networking.LoadTests/AuthorizationProbe.cs` — Client und Server in
  getrennten Prozessen, echte UDP-/WebSocket-Verbindungen, Produktions-Objektdispatch.
- `tests/Funke.Networking.LoadTests/Program.cs` — zusätzlicher `authorization`-Aufruf.
- `packages/Funke.Networking/Tests/LoadBudgets.md` — Reproduktion und aktuelle Ergebnisse.

**Vier erfolgreiche Zweiprozessläufe:** UDP/WebSocket jeweils als Dedicated Server und
Host. Fremde/fehlende Objekt-IDs, gefälschte Ownerangaben, Besitzerwechsel, eigene,
öffentliche, benutzerdefiniert autorisierte und serverexklusive Aktionen sowie Host-
Loopback geprüft. Dedicated Server: genau drei erlaubte Konstruktionen/Deserialisierungen;
Host: genau fünf. Unberechtigte Nachrichten lösen keine davon aus.

Die Probe verwendet kontrollierte verwaltete Objektdatensätze; sie behauptet keinen
nativen Entity-/Physiklauf. Die bestehenden Pakettests prüfen zusätzlich öffentliche
Ownerwechsel, Disconnect vor Dispatch und fehlschlagende Autorisierungspredikate.
Die API verspricht keine vollständige Cheat-Erkennung. Sourceversion
**Funke.Networking 1.0.1** ist weiterhin unveröffentlicht.

## NET-004 — vorhandene Umsetzung unter Last erneut abgenommen

Produktionscode und Budgets waren bereits vorhanden; kein weiterer Limitmechanismus
wurde eingeführt. **126/126 Networking-Tests bestanden**, einschließlich Rechteprüfung,
Handshake-/Queuegrenzen, fairer Verarbeitung, Disconnectpriorität, langsamer Empfänger
und großer zuverlässiger Nachrichten. Kein Skip.

Der vorhandene Lasttest wurde für UDP und WebSocket mit jeweils fünf getrennten
Workerprozessen erneut ausgeführt: regulärer Client, langsamer Empfänger, Floodingclient,
Server sowie 40 unvollständige Verbindungsversuche. Beide Läufe bestanden.

| Messung | UDP | WebSocket |
|---|---:|---:|
| Maximal verarbeitete Nachrichten pro Poll | 32 | 32 |
| Maximal gepufferte Nachrichten pro Peer | 128 | 128 |
| Maximal gepufferte Empfangsbytes pro Peer | 1.048.738 | 1.048.687 |
| Backpressure-Ablehnungen | 198 | 171 |
| Antworten beim regulären Client | 199 | 200 |
| Nahezu 1 MiB große zuverlässige Nachricht | empfangen | empfangen |
| Netzwerk-Arbeitszeit p95 / Maximum | 0,971 / 10,724 ms | 0,264 / 4,903 ms |

Überlastende Peers wurden mit `Receive queue or message limit exceeded` getrennt;
die regulären Clients blieben bereit. Speicher- und Zeitmessungen einschließlich
per-sekündlicher Heap-/Working-Set-Werte liegen als Rohdaten vor. Ein etwa elfsekündiger
Lauf belegt die geprüften Queuegrenzen, nicht generelle Leakfreiheit. Die Zeiten gelten
für den Netzwerkworker auf diesem Rechner, nicht als allgemeine Engine-Framerate.
Browser, TLS, Internetbedingungen und native Spielwelt waren nicht Teil dieser Abnahme.

## Belege und Buildumfang

Unter `artifacts/backlog-round5/` im Enginecheckout:

- `input-final/*.trx`, `network-tests/*.trx` — endgültige Pakettestergebnisse.
- `auth-{udp,ws}-{host,server}/server.json` und `client.json` — vier Rechteprüfungen.
- `network-load-{udp,ws}/*.json` — Server, regulärer/langsamer/Floodingclient und Handshakes.
- `baseline/` — Ausgangskopien der in dieser Runde weiterbearbeiteten Dateien.
- `verification.json` — Zusammenfassung und Hashes der verwendeten Dateien/Binaries.

Inkrementell gebaut: Input-Testprojekt samt Paketquellen/Beispielen,
Networking-Testprojekt und Networking-Lasttest, jeweils Release unter .NET 10.
Keine nativen Engineänderungen; kein eigener Enginebuild, Clean oder Premake-Neuaufbau.
Die erste Kompilierung der neuen Mehrprozessprobe fand eine falsche Callback-Signatur;
nach Korrektur bestanden Build und alle vier Läufe. Whitespace- und Berichtslinkprüfung
für das Delta dieser Runde bestanden. Der übrige Gesamtbacklog bleibt offen.
