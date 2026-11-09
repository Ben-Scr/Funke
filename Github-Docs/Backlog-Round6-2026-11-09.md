# Backlog-Runde 6 — 9. November 2026

Bearbeitet: **INP-003 — erledigt im unveröffentlichten Paketquellstand**.
Engine auf bestehendem `master`, Paketrepository auf bestehendem `main`; vorhandene
Änderungen wurden beibehalten. Keine Branches, Commits, Pushes oder Veröffentlichungen.
Die Implementierung liegt im benachbarten `Index-PackageRegistry`.

## Ursache und neues Verhalten

Schema-1-Profile adressierten einen Override über Map, Action und Listenindex. Nach
Umordnung konnte derselbe Index einen anderen Standard bezeichnen. Schema 2 speichert
stattdessen `bindingId`. Laufzeit-Overrides und Tastenerfassung ändern die Identität
eines Slots nicht. Der Loader fällt niemals auf dessen heutige Position zurück.

Code kann mit `Bind("keyboard", InputBinding.Key(KeyCode.Space))` stabile, innerhalb
der Action eindeutige IDs vergeben. Assets speichern dieselbe optionale `id`-Eigenschaft.
Der vorhandene Editor erzeugt IDs für neue Bindings und Vorlagen; Verschieben,
Controländerungen, Undo/Redo und Wiederherstellung bewahren sie. Die Konfliktsuche
vergleicht weiterhin physische Belegungen unabhängig von ihren IDs.

Alte Code-Definitionen und Assets ohne IDs bleiben lesbar. Sie verwenden einen
versionierten SHA-256-Fingerabdruck ihrer unveränderten Standardfelder. Dadurch bleiben
eindeutige Standards auch nach Umordnung zuordenbar. Geänderte Standards erhalten eine
andere Identität; identische anonyme Standards werden beim Speichern beziehungsweise
Adressieren eines Overrides als mehrdeutig abgelehnt. Für dauerhaft änderbare Standards
sind explizite IDs vorgesehen. Der Fingerabdruck ist durch einen festen Testwert gegen
unbeabsichtigte Formatänderungen abgesichert und kulturunabhängig.

## Kontrollierter Altprofilimport

Nichtleere Schema-1-Profile benötigen eine ausdrückliche Tabelle von historischen
`LegacyBindingSlot(Action, Index)`-Werten auf aktuelle IDs. Ein `null`-Ziel verwirft einen
entfernten Slot ausdrücklich. Die ursprüngliche Reihenfolge muss aus dem ausgelieferten
Spielstand bekannt sein; bei mehreren früheren Layouts wählt das Spiel die passende
Tabelle über seine eigene Speicher-/Releaseversion. Aus aktuellen Indizes wird keine
historische Zuordnung geraten.

Fehlende Zuordnungen, unbekannte oder mehrdeutige Ziele und mehrere alte Slots mit
demselben Ziel führen zu einem Fehler ohne Änderung aktiver Belegungen oder einer
laufenden Tastenerfassung. Leere Altprofile bleiben ohne Tabelle als Reset importierbar.
Erneutes Speichern schreibt Schema 2. Die neue PlayerPrefs-Überladung bietet denselben
Import; sie schreibt den Speicher nicht automatisch um.

Vollständige Validierung der Hülle, Identitäten und anwendbaren Bindings findet vor
der ersten Mutation statt. Schema-2-Einträge für entfernte Actions/IDs sowie ausdrücklich
verworfene Alt-Slots dürfen weiterhin obsolete Payloads enthalten. Sie werden nicht
angewendet. Grenzen bleiben 1 MiB JSON-Zeichen, 4096 Einträge und Tiefe 16.

Map-/Actionnamen bleiben Teil der Identität und erfordern bei Umbenennung eine eigene
Migration. Die README erklärt außerdem: Reset ändert die Live-Belegung; erst Speichern
ersetzt gespeicherte Overrides. Löschen eines gespeicherten Profils setzt die Live-Belegung
nicht zurück. Ältere Paketversionen können Schema 2 nicht lesen.

## Geänderte Dateien im Paketrepository

- `packages/Funke.Input/Source/InputBindingIdentity.cs` — ID-Prüfung und stabiler Fallback.
- `packages/Funke.Input/Source/InputBinding.cs` — optionale persistierte ID.
- `packages/Funke.Input/Source/InputAction.cs` — IDs, Eindeutigkeit und Erhalt bei Overrides.
- `packages/Funke.Input/Source/InputActionMap.Persistence.cs` — Schema 2 und Altprofilimport.
- `packages/Funke.Input/Source/InputActionAsset.cs` — Prüfung doppelter/ungültiger IDs.
- `packages/Funke.Input/Source/InputBindingConflicts.cs` — physischer Konfliktvergleich.
- `packages/Funke.Input/Source/Editor/InputActionEditorDocument.cs` — IDs für Vorlagen.
- `packages/Funke.Input/Source/Editor/InputActionEditorWindow.cs` — IDs für neue Bindings,
  aktualisierte Hinweise zu Umordnung und alten Standards.
- `packages/Funke.Input/README.md` — Identitätsvertrag und Migrationsbeispiel.
- `tests/Funke.Input.Tests/PersistenceTests.cs` — bestehende Altprofilprüfungen mit
  ausdrücklicher historischer Tabelle; Schema-2-Ausgabe.
- `tests/Funke.Input.Tests/BindingIdentityTests.cs` — 38 neue Fälle.

## Prüfung und Grenzen

**283/283 Tests bestanden, keine Skips.** Die 38 neuen Fälle prüfen Code-/Assetumordnung,
entfernte und unbekannte Ziele, doppelte IDs, anonyme Mehrdeutigkeit, Kulturunabhängigkeit,
geänderte Standards, historischen Import, ungültige spätere Einträge, Rebinding sowie
Editor-Undo/Redo und Wiederherstellung. Die vorhandenen Prüfungen für ungültige
Binding-Payloads und Größenlimits bleiben aktiv. Alle Paketquellen einschließlich
Editorcode und Beispiele wurden gegen die echte Release-ScriptCore-Assembly kompiliert,
mit Warnungen als Fehlern. Gezielte Whitespace- und Berichtslinkprüfung bestanden.

Reproduktion im Paketrepository:

```powershell
dotnet test tests/Funke.Input.Tests/Funke.Input.Tests.csproj -c Release --no-restore --logger 'trx;LogFileName=input-final.trx' --results-directory ../Index/artifacts/backlog-round6/input-final
```

Lokale Belege im Enginecheckout: `artifacts/backlog-round6/input-final/input-final.trx`,
`verification.json`, `task.patch` und `baseline/`. Die Patchdatei zeigt ausschließlich
diese Runde gegenüber dem zuvor bereits veränderten Arbeitsstand; SHA-256-Werte beziehen
sich auf die geprüften Quellen und Assemblies. `verify.py` prüft diese Bilanz erneut.

Kein nativer Enginebuild, kein manueller Editor-/PlayerPrefs-Lauf und kein Geräte- oder
Exporttest: geändert wurden verwaltete Identitäts-, Persistenz- und Editor-Datenpfade,
deren Zustandsübergänge die Pakettests direkt ausführen. Daraus wird keine neue native
Plattformabnahme abgeleitet. **Funke.Input 0.2.0 bleibt unveröffentlicht**; bestehende
Registry-ZIPs und Katalogeinträge wurden nicht verändert. INP-008/INP-009 und der übrige
Backlog bleiben separat zu bearbeiten.
