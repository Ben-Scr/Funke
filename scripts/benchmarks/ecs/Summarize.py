import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

root = Path(__file__).resolve().parents[3]
out = root / "output" / "ecs-benchmark-2026-10-28"
groups = defaultdict(list)
sources = {}
for prefix in ("final-1000", "final-10000", "final-100000", "extra-1000", "extra-10000"):
    source = out / (prefix + ".json")
    if not source.exists() or not (out / (prefix + ".done")).exists():
        raise SystemExit(f"Incomplete benchmark: {prefix}")
    result = json.loads(source.read_text(encoding="utf-8-sig"))
    for sample in result["Samples"]:
        if sample["Operation"] == "CreatePlusCustom4Float":
            continue
        key = (sample["Operation"], sample["Mode"], sample["Count"])
        groups[key].append(sample)
        sources[key] = prefix

summary = []
for (operation, mode, count), samples in sorted(groups.items()):
    times = sorted(s["TotalMs"] for s in samples)
    assert len(times) == 9, (operation, mode, count, len(times))
    summary.append({
        "operation": operation, "mode": mode, "count": count, "samples": len(samples),
        "median_ms": statistics.median(times), "min_ms": min(times),
        "p95_nearest_rank_ms": times[math.ceil(len(times) * 0.95) - 1], "max_ms": max(times),
        "record_median_ms": statistics.median(s["RecordMs"] for s in samples),
        "playback_median_ms": statistics.median(s["PlaybackMs"] for s in samples),
        "managed_allocated_bytes_median": statistics.median(s["AllocatedBytes"] for s in samples),
        "gen0_total": sum(s["Gen0"] for s in samples),
        "gen1_total": sum(s["Gen1"] for s in samples),
        "gen2_total": sum(s["Gen2"] for s in samples), "source": sources[(operation, mode, count)]
    })

with (out / "summary.csv").open("w", newline="", encoding="utf-8-sig") as stream:
    writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
    writer.writeheader()
    writer.writerows(summary)
(out / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")

lines = [
    "FUNKE C# SCRIPTCORE / ECS BENCHMARK — 28.10.2026",
    "",
    "Hardware: AMD Ryzen 9 7900X, 12 Kerne / 24 Threads; 32 GiB Kingston DDR5,",
    "konfiguriert 6000 MT/s; Radeon RX 7800 XT; Windows 11 Pro 26200.",
    "Engine: Release x64; .NET 10.0.11; 16 native Job-Worker (Engine-Standard).",
    "Windows-Energieprofil: Ausbalanciert. Andere Desktop-Anwendungen blieben offen.",
    "",
    "METHODE",
    "3 Warm-ups und 9 Messungen je Kombination. Millisekunden pro kompletter Ner-Charge.",
    "Jede Entity-Anzahl in einem neuen Runtime-Prozess; Messprozesse liefen nacheinander.",
    "Zwischen Proben laeuft ein normaler Engine-Frame nach dem Cleanup, sodass native",
    "Transform-Dirty-Daten abgebaut werden. Setup, Validierung und Cleanup sind ungemessen.",
    "Echte C#-API, native Engine und native Job-Pool; keine Mocks. TieredCompilation=0,",
    "ReadyToRun=0, Diagnostics=0 nur in den Benchmark-Kindprozessen; optimierter warmer JIT.",
    "ECB und Puffer werden wiederverwendet. Record umfasst Clear und bei Jobs Schedule+Complete.",
    "Playback wird am Hauptthread ausgefuehrt; dessen Zeit umfasst native Ausfuehrung und",
    "bei parallelem Recording das Zusammenfuehren der Streams. Total umfasst beide Phasen.",
    "Die Mediane der Einzelphasen muessen sich nicht exakt zum Median der Summe addieren.",
    "p95 ist nearest-rank; bei nur 9 Proben entspricht es dem Maximum und ist keine stabile",
    "Schaetzung seltener Ausreisser. Alle einzelnen Proben stehen in den CSV/JSON-Dateien.",
    "Allokationen sind prozessweite verwaltete Bytes waehrend des Messfensters, keine nativen Bytes.",
    "",
    "FIXTURES UND VERGLEICHBARKEIT",
    "CreateDefaultTransform erzeugt auf allen Wegen eine Entity mit Transform2D.",
    "CreatePlus2Components erzeugt Transform2D + SpriteRenderer. Direkte Aufrufe geben",
    "C#-Wrapper zurueck. ECB/ECB_Parallel materialisieren diese erst zur ungemessenen Pruefung.",
    "ECB_Materialized misst auch Entity-Wrapper, bei CreatePlus2 auch beide Komponentenwrapper.",
    "ECB_DefaultConstruct verwendet CreateWith<T...>() statt AddComponent mit Struct-Payload.",
    "InstantiatePrefab nutzt ein warmes, flaches Prefab mit Transform2D + SpriteRenderer,",
    "ohne Kinder, Texturen, Physik, EntityScript oder interne Entity-Referenzen.",
    "InstantiateLiveEntity ist ein separater Clone-Test mit denselben zwei Komponenten.",
    "Add/RemoveExisting betrifft SpriteRenderer auf schon vorhandenen Transform-Entities.",
    "Custom4Float ist eine registrierte C#-IComponent mit X/Y/VelocityX/VelocityY (16 Byte).",
    "CreateTransformPlusCustom4Float enthaelt zusaetzlich auf allen Wegen Transform2D.",
    "DestroyNoWrappers wird zuerst im frischen Prozess gemessen, bevor Wrapper-Caches benutzt werden.",
    "DestroyCachedWrappers hat pro Entity zwei live gecachte C#-Komponentenwrapper.",
    "Destroy/Disable-ECB erhalten bereits vorbereitete Live-EntityRefs; deren Konvertierung ist Setup.",
    "Queries messen Snapshot-Aufbau plus volle Iteration. IJobQuery enthaelt Schedule+Complete.",
    "QueryRef hat einen sequentiellen C#-Body; der native Snapshot-Aufbau darf intern parallel laufen.",
    "QueryLight addiert (1,2); QueryTwoComponents liest Sprite-Alpha und aktualisiert Transform.",
    "QueryHeavy64 rechnet je Entity 64 Sin/Cos-Schritte und schreibt die Position zurueck.",
    "Das ist synthetische CPU-Arbeit, keine gemessene Gameplay-/Rendering-Frametime.",
    "",
    "GRENZEN",
    "Der C#-ECB unterstuetzt kein RemoveComponent und kein AddComponent an existierenden",
    "Live-Entities. Add ist nur fuer eigene Create/Instantiate-Refs verfuegbar. Direkte",
    "strukturelle Aenderungen wurden nicht unsicher von mehreren Threads ausgefuehrt.",
    "100000 Destroy mit live gecachten Wrappern wurde wegen der bereits bei 10000",
    "gemessenen quadratischen Skalierung nicht gemessen; dafuer wird kein Wert extrapoliert.",
    "Breite ECS-API-Abdeckung, keine Vollpruefung aller komponentenspezifischen Physik-,",
    "Script-Lifecycle-, Hierarchie- und Asset-Kosten. Das GPU-Modell bestimmt diese CPU-Zeiten kaum.",
    "",
    "DIAGNOSE / VERWORFENE VORLAEUFE",
    "smoke*, main* sind Entwicklungs-/Diagnoselaeufe und gehen nicht in summary.csv ein.",
    "Der erste main-run fuehrte alles in einem Frame aus und ist ausdruecklich superseded.",
    "Fruehe CreatePlusCustom4Float-Zeilen hatten unterschiedliche Transform-Ausstattung und",
    "sind verworfen; extra-* ersetzt sie durch CreateTransformPlusCustom4Float.",
    "Die direkte Wrapper-Invalidierung in Entity.cs scannt fuer jede Entity den globalen",
    "Wrapper-Store. Native/ECB-Notifications ueberspringen ihn bei Count==0; direkte",
    "Destroy-Aufrufe nicht. Auch ein zuvor geleerter Dictionary-Store kann deshalb den",
    "direkten Destroy-Pfad verlangsamen. Den detaillierten History-Probe separat beachten.",
    "",
    f"ERGEBNISSE: {len(summary)} Kombinationen, {sum(s['samples'] for s in summary)} gemessene Proben.",
    ""
]
for count in (1000, 10000, 100000):
    lines += [f"N = {count}",
              f"{'Operation / Modus':76} {'Median ms':>11} {'Min ms':>11} {'Max/p95 ms':>12} {'Record ms':>11} {'Playback ms':>12} {'Alloc KiB':>11}"]
    for row in summary:
        if row["count"] != count:
            continue
        label = row["operation"] + " / " + row["mode"]
        lines.append(f"{label:76} {row['median_ms']:11.4f} {row['min_ms']:11.4f} {row['max_ms']:12.4f} {row['record_median_ms']:11.4f} {row['playback_median_ms']:12.4f} {row['managed_allocated_bytes_median']/1024:11.2f}")
    lines.append("")
(out / "Benchmark-Bericht.txt").write_text("\n".join(lines), encoding="utf-8-sig")
print(f"Wrote {len(summary)} rows to {out / 'summary.csv'}")
