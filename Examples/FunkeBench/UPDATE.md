# FunkeBench-Update vom 13. November 2026

Die Änderungen liegen in dieser geprüften Projektkopie unter `Examples/FunkeBench`. Das ursprüngliche Projekt unter `C:\Users\benso\Funke\Projects\FunkeBench` wurde nicht geändert: Der Ordner liegt außerhalb des beschreibbaren Arbeitsbereichs dieser Sitzung.

## Änderungen

- Create verwendet jetzt auch mit Jobs die Bulk-API in Blöcken von 1.024 Entities. Der bisherige Vergleich verwendete seriell Bulk-Aufzeichnung, parallel Einzelaufrufe.
- `bench.playback Automatic|Sequential|Parallel` trennt die Auswahl der nativen Ausführung von paralleler Aufzeichnung. Der angeforderte Modus steht im CSV-Messumfang; die Engine kann für ungeeignete Befehle weiterhin seriell arbeiten.
- 3D-Erstellung nutzt `Entity.Create3D`; lesender Zugriff auf 2D-Positionen markiert keine Transform-Daten mehr unnötig als verändert.
- Mengenlimits sind zwischen Oberfläche und Einstellungen einheitlich. Nur das für den ausgewählten Test relevante Samples-/Dauerfeld muss gültig sein. ECS-Ergebnisse melden keine Render-Animation mehr.
- Statistik weist negative, nicht endliche und überlaufende Messwerte zurück. Der veraltete Engine-Version-Stub in den Tests ist korrigiert.
- CSV-Exporte überschreiben keine vorherigen Ergebnisse. Exporte während einer laufenden Messung werden abgelehnt, damit sie deren Framezeiten nicht verändern.
- Der UI-Generator überschreibt keine Projekteinstellungen und löscht keine fremden Szenen mehr.
- Start- und Prüfscripte bauen gegen die ausgewählte ScriptCore-DLL. Die Laufzeitprüfung verwendet eine Runtime-Kopie, kontrolliert alle erwarteten Assertions und prüft reale Entity-IDs und Komponenten.

## Nachweise

| Prüfung | Ergebnis |
|---|---|
| FunkeBench Release-Build | 0 Fehler, 0 Warnungen |
| Statistik, Einstellungen, CSV | 39 Assertions bestanden |
| UI-Generator | Einstellungen, fremde Assets und stabile Szenen-IDs erhalten |
| Smoke | 13 Laufzeitprüfungen bestanden; alle fünf Szenarien seriell und mit Jobs |
| Creation | 15 Laufzeitprüfungen bestanden; Blockgrenzen, drei Playback-Modi, 100.000 Entities |
| Interaction | 14 Laufzeitprüfungen bestanden; UI-Klicks, Animation, VSync und Export |
| Upgrade.patch | `git apply --check` gegen den ursprünglichen Quellstand bestanden |

Die vorhandene Runtime war zunächst gegen andere Engine-Header gebaut als ihre DLL. Nach dem Neubau der Runtime bestanden alle drei Anwendungstests ohne Fehler im Log. Native Compilerwarnungen aus vorhandenen Engine-Headern bleiben bestehen. Diese Läufe prüfen Funktion, nicht einen belastbaren Performancegewinn. Laufzeitprotokolle und Screenshots liegen unter `Tests/Output`.

## Im ursprünglichen Projekt anwenden

`Upgrade.patch` enthält die 19 geänderten beziehungsweise neuen Quelldateien und Tests. Er lässt Szenen, Buildprofile und Projekteinstellungen unverändert. In einem Terminal mit Schreibzugriff auf das ursprüngliche Projekt:

```powershell
$benchProject = 'C:\Users\benso\Funke\Projects\FunkeBench'
$benchPatch = 'C:\Users\benso\source\repos\Pending\Index\Examples\FunkeBench\Upgrade.patch'
git -C $benchProject apply --check $benchPatch
if ($LASTEXITCODE -eq 0) {
    git -C $benchProject apply $benchPatch
}
```

Alternativ dieses ursprüngliche Projekt als Arbeitsbereich öffnen, damit die geprüften Änderungen dort angewendet werden können. Die Projektkopie lässt sich bereits über ihre Startskripte ausführen.
