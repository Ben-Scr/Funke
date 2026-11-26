# Verbleibende Build-/Asset-Abnahme

Stand 26. November 2026. [Komplexitätsstufen und Gesamtübersicht](Funke_Agenten_Backlog.md). **Eine offene Aufgabe.** Fehlerweitergabe,
Pruningregeln und dauerhafte Dokumentation sind umgesetzt und unter
[Asset-Validierung](Documentation/Editor/Build-Asset-Validation.md) beschrieben.

<a id="task-asset-01"></a>

### ASSET-01 – Fehlerdarstellung und Export auf Desktop und Web abnehmen

**Komplexität:** K3

- [ ] Mit gemeinsam neu gebauter
  Engine, ScriptCore, CLI und Editor die Fälle unten ausführen. Konkrete
  Fehlerdarstellung im Build-Panel sowie CLI-Text/JSON kontrollieren. Einen
  gültigen Desktop-Export starten und einen Web-Export bis zum fertigen Ergebnis
  und Start über HTTP prüfen.

| Fall | Noch nachzuweisendes Gesamtergebnis |
|---|---|
| Referenziertes leeres/beschädigtes serialisiertes Asset | Export scheitert mit Projektpfad und echter Ursache in Editor und CLI. |
| Nachweislich unbenutztes ungültiges Asset | Bei gültigem Pruningplan ausgeschlossen; sonst gültiger Export gelingt. |
| Konservativ übernommenes ungültiges Asset | Export scheitert mit nachvollziehbarem Grund für die Übernahme. |
| Gültige Assets | Desktop und Web fertig exportieren und starten. |
| Fehlgeschlagener Export | Quelldateien/Metadaten und vorheriger erfolgreicher Export unverändert. |

Gezielte Regressionen von Serializer, Konvertierung und Pruning bestehen; sie
ersetzen diese Exportabnahme nicht. Für gültige, gestripte Desktop-Exporte mit Modellmaterialien liegt eine [Windows-Abnahme](Documentation/Engine-Core/3D-Asset-Workflow-Verification.md) vor. Die Fehlerfälle, das Build-Panel und der Web-Lauf dieser Matrix bleiben offen. Beim früheren vollständigen Neubau scheiterte der Git-Zugriff auf die ImGuizmo-Metadaten.
