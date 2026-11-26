# Ausbauplan: C#-Utilities mit Schwerpunkt 3D

Stand 26. November 2026. [Komplexitätsstufen und Gesamtübersicht](Funke_Agenten_Backlog.md). **2 offene nummerierte Aufgaben.** Die umgesetzten Bewegungs-, Orientierungs-, Geometrie-, Zufalls- und Gizmo-APIs sind in der [3D-Dokumentation](Documentation/Engine-Core/3D.md#c-movement-and-coordinate-utilities) beschrieben. Die Paketideen weiter unten sind Vorschläge.

## API-Regeln vor der Umsetzung

- **Koordinaten:** Rechtshändig, lokale Blickrichtung `-Z`, oben `+Y`. Öffentliche Winkel standardmäßig in Grad; Radiantvarianten tragen den Zusatz `Radians`.
- **Raum:** `Transform3D.Position`, `Rotation` und `Scale` sind lokal. Zielpunkte von `LookAt3D`/`Face3D` sind Weltpunkte. Lokale Varianten oder `Space.Local` müssen ausdrücklich benannt sein.
- **Lesen und Schreiben:** In Anlehnung an die bestehende 2D-API berechnet `LookAt3D` eine Rotation; `Face3D` wendet sie an. Optionale Component-Komfortmethoden delegieren an dieselbe Implementierung.
- **Aktualität:** Neue Gameplay-Helfer müssen Änderungen am Objekt und seinen Eltern im selben Aufrufablauf berücksichtigen. Bestehende gecachte Renderwerte nicht stillschweigend mit aktuellen Hierarchiewerten mischen.
- **Orientierung versus Skalierung:** `WorldRotation` beschreibt komponierte Rotationen ohne Spiegelung/Scherung. `Forward` stammt dagegen aus der Weltmatrix. Unter negativer oder nichtuniformer Elternskalierung sind diese Begriffe nicht austauschbar. Punkt-/Vektortransformationen verwenden die volle Matrix; Orientierungshelfer verwenden den Quaternion-Vertrag.
- **Zeit:** Interpolationsfaktor `t`, Winkelgeschwindigkeit in Grad/Sekunde und Dämpfungsrate sind getrennte Parameter. Zustandslose Mathematik erhält `deltaTime` explizit und liest nicht versteckt `Time.DeltaTime`.
- **Fehler:** Try-Methoden melden unlösbare Fälle, beispielsweise eine singuläre Elternmatrix. Mutierende Helfer lassen den bisherigen Zustand dann unverändert. Für ungültige Argumente und nicht endliche Werte gilt ein dokumentierter, konsistenter Vertrag.

<a id="task-utl-007"></a>

### UTL-007 – Kamera-Komfort

**Komplexität:** K3 · **Priorität:** P1

- [ ] Screen-/Viewport-Konvertierung, `ScreenToWorldPoint` mit explizit definierter Kameratiefe, `TryWorldToScreenPoint`-Komfort, World-to-UI-Anbindung.

**Abnahme:** Bestehende Projektion wiederverwenden, hinter Kamera von außerhalb Viewport unterscheiden; resize, orthografisch und Editor-Game-View testen. Bei der UI-Anbindung Canvas-/RectTransform-Raum beachten.

<a id="task-utl-008"></a>

### UTL-008 – 3D-Physikabfragen

**Komplexität:** K4 · **Priorität:** P1

- [ ] Zuerst `SphereCast`, `CapsuleCast`, `OverlapSphere`; danach `BoxCast`, Box-/Capsule-Overlap, `RaycastAll`.

**Abnahme:** Vorhandenes Physikbackend/Jolt anbinden. Layer, Trigger, Self-/Body-Ausschluss, Startüberlappung und Trefferreihenfolge definieren. Für Mehrfachtreffer wiederverwendbare Puffer/Span plus explizite Overflow-Meldung. Live-Szenen mit dünnen Wänden, Compound-Collidern und geneigten Flächen prüfen.

## Optionale Funke-Packages

Die im ursprünglichen Audit geprüfte [Registry](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/main/index.json) listet unter anderem `Funke.Tween` 1.0.0 sowie `Funke.Pathfinding` und `Funke.Pathfinding3D` jeweils 0.1.0. Registry-Version, aktueller Source-Stand und tatsächlich getestetes Release-Archiv getrennt behandeln.

| Komplexität | Paket | Entscheidung | Konkreter nächster Schritt |
| --- | --- | --- | --- |
| K2 | [Funke.Tween](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/main/packages/Funke.Tween/Source/Shortcuts3D.cs) | Vorhandenes Paket erweitern. 3D-Position, Eulerrotation, Skalierung und weitere Shortcuts stehen bereits im Source. | Quaternion-Tweener für kürzesten Rotationsweg, explizite Welt-/Lokalvarianten und `LookAt`-Tween ergänzen. Bestehende Euler-Tweens für vollständige 360°-Drehungen erhalten. Lifecycle-Fixes des Source-Stands vor einem neuen Release prüfen. |
| K2 | `Funke.Noise` **(Vorschlag, neu)** | Optionales Paket für 2D/3D-prozedurale Inhalte. | FastNoiseLite kapseln; Seed, Frequenz, Fraktale und Domain Warp anbieten. `PerlinNoise2D` kompatibel erhalten, damit bestehende Welten nicht unbemerkt andere Ergebnisse liefern. |
| K3 | [Funke.Pathfinding3D](https://github.com/Ben-Scr/Funke-PackageRegistry/blob/main/packages/Funke.Pathfinding3D/README.md) | Für Grid-/Waypoint-, Boden- und Flugnavigation vorhandenes Paket verwenden. | UTL-008 für echte Körper-Clearance anbinden; Follower mit Transform-/Controller-Verträgen abstimmen. Der aktuelle Ground-Baker nutzt eine Höhe pro XZ-Spalte und ist kein Dreiecks-Navmesh; Crowd Avoidance ist ebenfalls nicht enthalten. |
| K3 | `Funke.Splines` **(Vorschlag, neu)** | Als separates C#-Paket sinnvoll, sobald Kamera-/Schienen-/Patrouillenpfade gebraucht werden. | Bézier und Catmull–Rom, Position/Tangente, Distanz→Parameter über Bogenlängentabelle, konstante Geschwindigkeit und geschlossene Pfade. Danach Asset-/Editorunterstützung und Tween-Adapter. `Graph` bleibt die vorhandene skalare Kurve. |
| K4 | `Funke.Navigation` **(Arbeitstitel, neu)** | Erst für Anforderungen jenseits der bestehenden Grid-/Waypoint-Navigation. | Navmesh-Bake, Abfrage, Off-Mesh-Links und später Crowd Avoidance separat evaluieren. An bestehende Backlog AIN-003/004 und gemeinsame Agent-/Follower-APIs anschließen. |

Ein weiteres allgemeines `Funke.Utilities`-Paket ist für `LookAt`, Vector3-Mathematik und Raumkonvertierungen nicht sinnvoll: Diese Funktionen sollen jedem C#-Spiel direkt zur Verfügung stehen. Ein neues Camera-Paket ist für die vorhandene Follow-Basis ebenfalls nicht erforderlich.

## Externe Bibliotheken: gezielte Kandidaten

| Bibliothek | Eignung | Empfehlung / offene Integrationsprüfung |
| --- | --- | --- |
| [System.Numerics](https://learn.microsoft.com/en-us/dotnet/api/system.numerics.matrix4x4?view=net-10.0) | .NET-eigene Quaternion-/Matrixmathematik; wird in Funke.Quaternion schon verwendet. | Für interne Berechnungen weiterverwenden. Funke-Typen als öffentliche API erhalten und Matrix-/Multiplikationskonventionen gegen GLM testen. Kein zusätzliches Math-NuGet für LookAt nötig. `CreateLookAt` erstellt eine View-Matrix, keine direkt verwendbare Objektrotation. |
| [FastNoiseLite](https://github.com/Auburn/FastNoiseLite) | C#-Implementierung, 2D-/3D-Sampling, mehrere Noise-Verfahren und Domain Warp; MIT. | Erster Kandidat für `Funke.Noise`. Gepinnten Source-Stand mit Lizenzhinweisen kapseln; Seed-Stabilität, Performance und konkrete Zielplattformen prüfen. |
| [DotRecast](https://github.com/ikpil/DotRecast) | C#-Port von Recast/Detour mit Navmesh-Bake, Queries und optionalen Crowd-/Tile-Modulen; zlib. | Prototyp für ein optionales Navmesh-Paket, sobald Grid-Navigation nicht genügt. Nur benötigte Laufzeitmodule exportieren; Bake/Demo nicht ungeprüft mitliefern. Speicher/GC, AOT, Export und Szenenwechsel messen. |
| [Recast/Detour nativ](https://github.com/recastnavigation/recastnavigation) | Native Alternative mit zlib-Lizenz. | Gegen DotRecast anhand derselben Testszene vergleichen, falls native Verarbeitung und Speicherbedarf entscheidend sind. Dafür native Paketgrenze, Bindingpflege und Plattformbuilds einplanen. Nicht beide Backends im ersten Ausbau integrieren. |

**Integrationsvertrag:** Funke-Abhängigkeiten gehören in `dependencies`; NuGet-Referenzen gegebenenfalls in `layers.csharp.nuget`, das der aktuelle Loader unterstützt. Versionen pinnen, Drittanbieter-Typen hinter Funke-Adaptern halten und die tatsächliche Auslieferung abhängiger Assemblies prüfen. Desktop-.NET-Erfolg allein belegt weder Mono/AOT noch Android/iOS/Web. Das gilt besonders für Bibliotheken mit weiteren Runtime-Abhängigkeiten.

## Abschlusskriterien je Implementierungsschritt

- [ ] Gezielte Erweiterung vorhandener `MathTests`, `QuaternionTests`, `CameraFollowTests`, `Rendering3DTests` beziehungsweise native Transform-/Physiktests; neue Testdateien nur für neue eigenständige Verträge.
- [ ] Neue native Slots in C++/C# positionsgleich ergänzen, Interop-Versionen gemeinsam erhöhen, Guard und Modul-Stubs registrieren; passende native/managed Binaries zusammen prüfen. Reine Managed-Mathematik benötigt keine ABI-Änderung.
- [ ] Eine kleine 3D-Abnahmeszene mit Zielausrichtung, gedrehtem/skaliertem Parent, Boden-Picking und Orbit. Für Shape-Casts zusätzlich reale Jolt-Kollisionen testen.
- [ ] Öffentliche APIs und Beispiele in [3D](Documentation/Engine-Core/3D.md), [Scripting](Documentation/Scripting/Scripting.md), [Camera Follow](Documentation/Engine-Core/Camera-Follow.md) und bei Bedarf [Physics 3D](Documentation/Engine-Core/Physics-3D.md) dokumentieren; C#-Scripting-Skill entsprechend aktualisieren.
- [ ] Betroffene Ziele prüfen: reine Mathematik mit ScriptCore-Tests, neue native Bindings mit betroffenen nativen Tests und passendem Runtime-Lauf.

Erledigte Aufgaben dürfen aus dieser Backlog entfernt werden, sobald ihre dauerhaften API-Verträge und relevanten Einschränkungen in der Engine-Dokumentation stehen.
