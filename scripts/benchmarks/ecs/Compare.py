import argparse
import csv
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUTPUT = ROOT / "output" / "ecs-benchmark-2026-10-28"
DEFAULT_PREFIXES = ("optimized-verified-1000", "optimized-verified-10000", "optimized-verified-100000", "optimized-verified-history-10000")


def read_json(path):
    if not path.is_file():
        raise ValueError(f"Fehlende Datei: {path}")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def number(value, label):
    if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
        raise ValueError(f"Ungueltiger Messwert fuer {label}: {value!r}")
    return value


def validate_prefix(prefix):
    if not prefix or any(character not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-" for character in prefix):
        raise ValueError(f"Ungueltiger Praefix: {prefix!r}")


def load_run(folder, prefix):
    validate_prefix(prefix)
    done = folder / f"{prefix}.done"
    if (folder / f"{prefix}.failed").exists():
        raise ValueError(f"Fehlgeschlagener Benchmark: {prefix}")
    if not done.is_file() or done.read_text(encoding="utf-8-sig").strip() != "PASS":
        raise ValueError(f"Benchmark nicht erfolgreich abgeschlossen: {done}")
    source = folder / f"{prefix}.json"
    run = read_json(source)
    if run.get("Warmups") != 3 or run.get("Repetitions") != 9:
        raise ValueError(f"{prefix}: Erwartet werden 3 Warm-ups und 9 Messungen.")
    if not isinstance(run.get("Samples"), list) or not run["Samples"]:
        raise ValueError(f"{prefix}: Keine Rohmessungen vorhanden.")
    run["source"] = prefix
    run["json_path"] = str(source)
    run["raw_sample_files"] = [
        str(folder / f"{prefix}{extension}")
        for extension in (".json", ".jsonl", ".csv")
        if (folder / f"{prefix}{extension}").is_file()
    ]
    machine_path = folder / f"{prefix}-machine.json"
    run["machine_path"] = str(machine_path) if machine_path.is_file() else None
    run["machine"] = read_json(machine_path) if machine_path.is_file() else None
    return run


def summarize(runs, baseline, baseline_runs, baseline_path):
    groups = defaultdict(list)
    owners = {}
    for run in runs:
        for sample in run["Samples"]:
            key = (sample["Operation"], sample["Mode"], sample["Count"])
            if type(key[2]) is not int or key[2] <= 0:
                raise ValueError(f"Ungueltige Entity-Anzahl: {key}")
            if key in owners and owners[key] is not run:
                raise ValueError(f"Mehrere Laeufe enthalten dieselbe Kombination: {key}")
            owners[key] = run
            for field in ("TotalMs", "RecordMs", "PlaybackMs", "AllocatedBytes", "Gen0", "Gen1", "Gen2"):
                number(sample[field], f"{key}/{field}")
            groups[key].append(sample)

    rows = []
    for (operation, mode, count), samples in sorted(groups.items()):
        key = (operation, mode, count)
        if len(samples) != 9 or sorted(sample["Sample"] for sample in samples) != list(range(9)):
            raise ValueError(f"{key}: Genau 9 eindeutige Messungen (0 bis 8) erforderlich.")
        times = [sample["TotalMs"] for sample in samples]
        median = statistics.median(times)
        previous = baseline.get(key)
        baseline_median = previous["median_ms"] if previous else None
        baseline_run = baseline_runs[previous["source"]] if previous else None
        rows.append({
            "operation": operation,
            "mode": mode,
            "count": count,
            "samples": len(samples),
            "median_ms": median,
            "min_ms": min(times),
            "max_ms": max(times),
            "record_median_ms": statistics.median(sample["RecordMs"] for sample in samples),
            "playback_median_ms": statistics.median(sample["PlaybackMs"] for sample in samples),
            "managed_allocated_bytes_median": statistics.median(sample["AllocatedBytes"] for sample in samples),
            "gen0_total": sum(sample["Gen0"] for sample in samples),
            "gen1_total": sum(sample["Gen1"] for sample in samples),
            "gen2_total": sum(sample["Gen2"] for sample in samples),
            "baseline_median_ms": baseline_median,
            "speedup": baseline_median / median if baseline_median is not None and baseline_median > 0 and median > 0 else None,
            "source": owners[key]["source"],
            "raw_sample_files": owners[key]["raw_sample_files"],
            "baseline_source": previous["source"] if previous else None,
            "baseline_raw_sample_files": baseline_run["raw_sample_files"] if baseline_run else [],
            "baseline_summary_path": str(baseline_path),
            "baseline_input_path": previous.get("_baseline_input_path", str(baseline_path)) if previous else None,
        })
    return rows


def machine_lines(run):
    machine = run["machine"]
    lines = [f"{run['source']}: .NET {run.get('Runtime', 'unbekannt')}; "
             f"logische Prozessoren {run.get('ProcessorCount', 'unbekannt')}; "
             f"Job-Worker {run.get('Workers', 'unbekannt')}; UTC {run.get('UtcTime', 'unbekannt')}."]
    if not machine:
        return lines + ["Keine Hardware-Metadatendatei fuer diesen Lauf vorhanden."]
    cpu = machine.get("Cpu", {})
    cpus = cpu if isinstance(cpu, list) else [cpu]
    for item in cpus:
        lines.append(f"CPU: {item.get('Name', 'unbekannt').strip()}; "
                     f"Kerne {item.get('NumberOfCores', 'unbekannt')}; "
                     f"Threads {item.get('NumberOfLogicalProcessors', 'unbekannt')}.")
    memory = machine.get("Memory", [])
    memory = memory if isinstance(memory, list) else [memory]
    if memory and all("Capacity" in module for module in memory):
        gib = sum(module["Capacity"] for module in memory) / (1024 ** 3)
        clocks = sorted({str(module.get("ConfiguredClockSpeed", "unbekannt")) for module in memory})
        lines.append(f"RAM: {gib:g} GiB; konfigurierte Speicherraten laut System: {', '.join(clocks)}.")
    lines.append(f"Energieprofil: {machine.get('PowerScheme', 'unbekannt')}")
    lines.append(f"Metadaten mit Hardware, Git-Stand und Datei-Hashes: {run['machine_path']}")
    return lines


def make_report(rows, runs, baseline_runs, baseline_path):
    lines = [
        "FUNKE C# SCRIPTCORE / ECS — OPTIMIERUNGSVERGLEICH",
        "",
        "METHODE UND VERGLEICHBARKEIT",
        "Baseline und optimierte Laeufe: 3 Warm-ups, danach 9 Messungen je Kombination.",
        "Alle Zeitangaben sind Millisekunden fuer die komplette Charge mit N Entities, nicht pro Entity.",
        "Setup, Zustandspruefung und Cleanup liegen ausserhalb des Messfensters. Zwischen den",
        "Proben verarbeitet ein Engine-Frame die nach dem Cleanup verbleibenden Dirty-Daten.",
        "ECB und Puffer werden wiederverwendet. Record umfasst Clear und bei Jobs Schedule+Complete.",
        "Parallelisiert wird die Aufzeichnung. Playback erfolgt weiterhin auf einem Hauptthread",
        "und enthaelt bei paralleler Aufzeichnung auch das Zusammenfuehren der Befehlsstreams.",
        "Total umfasst Record und Playback; die Phasenmediane muessen sich nicht zum Totalmedian addieren.",
        "Direkte Aufrufe haben keine getrennten Record-/Playback-Phasen; deren 0-Werte bedeuten nicht null Arbeit.",
        "Allokationen sind prozessweite verwaltete Bytes im Messfenster, keine nativen Allokationen.",
        "Nur erfolgreich mit PASS abgeschlossene Laeufe werden ausgewertet; fehlende Daten brechen ab.",
        "",
        "Speedup = Baseline-Median / optimierter Median fuer identische Operation, Modus und Entity-Anzahl.",
        "Werte ueber 1 bedeuten schneller, unter 1 langsamer. Ohne passende Baseline bleibt das Feld leer.",
        "Entscheidend ist die gewaehlte Baseline: Nur dort ebenfalls gemessene Kombinationen erhalten einen Speedup.",
        "Ein Vergleich verschiedener API-Modi ist kein Vorher/Nachher-Gewinn desselben Modus.",
        "CreatePlus2Components/Direct liefert einen Entity-Wrapper und zwei Komponentenwrapper.",
        "ECB_Materialized misst dieselben Wrapper-Ausgaben und ist dafuer der faire Vergleich.",
        "ECB und ECB_Parallel ohne Materialisierung messen weniger verwaltete Rueckgabeobjekte.",
        "ECB_DefaultConstruct verwendet native Defaults; Payload-Adds uebertragen explizite Struct-Werte.",
        "Add/RemoveExisting betrifft SpriteRenderer auf vorhandenen Transform-Entities; Live-Refs",
        "werden im Setup vorbereitet. Die Wrapper-Variante von Add liefert zusaetzlich einen Wrapper.",
        "",
        "Die Ergebnisse gelten fuer diese API-Workloads und die protokollierte Hardware. Es gibt",
        "keine Vergleichsmessung gegen andere ECS-Engines und keine daraus abgeleitete Rangfolge.",
        "9 Proben beschreiben Streuung in diesem Lauf, keine belastbare Statistik seltener Ausreisser.",
        "Desktop-Hintergrundlast, Energieprofil und Laufzeitumgebung koennen Unterschiede beeinflussen.",
        "Ein gesamter Frame hat bei 60 FPS 16,67 ms und bei 120 FPS 8,33 ms; diese Batch-Zeiten",
        "sind kein vollstaendiger Gameplay-Frame mit Rendering, Physik und weiteren Systemen.",
        "",
        "OPTIMIERTE LAEUFE UND HARDWARE",
    ]
    for run in runs:
        lines.extend(machine_lines(run))
        lines.append("")
    if any(row["operation"] == "MixedStructuralLiveAndLocal" for row in rows):
        lines += [
            "MIXED-FIXTURE",
            "N bezeichnet vorhandene Entities; pro Charge werden zusaetzlich N lokale Entities aufgezeichnet.",
            "Deterministisch durchmischte Live-Refs, Add/Remove/Re-add und Destroy testen Befehlsreihenfolge.",
            "Jede bestehende Entity, jeder lokale Ueberlebende, verworfene IDs und die Szenenanzahl werden geprueft.",
            "",
        ]
    lines += [f"ERGEBNISSE: {len(rows)} Kombinationen, {sum(row['samples'] for row in rows)} Messproben.", ""]
    for count in sorted({row["count"] for row in rows}):
        lines += [f"N = {count}",
                  f"{'Operation / Modus':72} {'Median ms':>11} {'Min ms':>11} {'Max ms':>11} "
                  f"{'Record ms':>11} {'Playback ms':>12} {'Alloc Bytes':>12} {'Baseline ms':>12} {'Speedup':>10}"]
        for row in rows:
            if row["count"] != count:
                continue
            baseline = f"{row['baseline_median_ms']:.4f}" if row["baseline_median_ms"] is not None else "--"
            speedup = f"{row['speedup']:.3f}x" if row["speedup"] is not None else "--"
            label = f"{row['operation']} / {row['mode']}"
            lines.append(f"{label:72} {row['median_ms']:11.4f} {row['min_ms']:11.4f} {row['max_ms']:11.4f} "
                         f"{row['record_median_ms']:11.4f} {row['playback_median_ms']:12.4f} "
                         f"{row['managed_allocated_bytes_median']:12.0f} {baseline:>12} {speedup:>10}")
        lines.append("")
    lines += ["QUELLEN / ALLE ROHMESSUNGEN", f"Baseline-Zusammenfassung: {baseline_path}"]
    for label, source_runs in (("Baseline", baseline_runs.values()), ("Optimiert", runs)):
        for run in source_runs:
            lines.append(f"{label}: {run['source']}")
            lines.extend(f"  {path}" for path in run["raw_sample_files"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="Vergleicht abgeschlossene optimierte ECS-Laeufe mit summary.json.")
    parser.add_argument("prefixes", nargs="*", help="Lauf-Praefixe; Standard: die vier optimized-verified-Laeufe")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--baseline", type=Path, help="Baseline-summary.json; Standard: <output-dir>/summary.json")
    parser.add_argument("--baseline-run", action="append", default=[], metavar="PREFIX",
                        help="Zusaetzlicher Baseline-Lauf in output-dir; wiederholbar, nur fehlende Kombinationen.")
    parser.add_argument("--result-prefix", default="optimized",
                        help="Praefix fuer <prefix>-summary.json/.csv; Standard: optimized")
    parser.add_argument("--report-name", help="Berichtsdateiname mit .txt; Standard: Optimierungsbericht.txt bzw. <prefix>-Bericht.txt")
    parser.add_argument("--dry-run", action="store_true", help="Alle Eingaben pruefen und auswerten, ohne Dateien zu schreiben.")
    args = parser.parse_args()
    output = args.output_dir.resolve()
    baseline_path = (args.baseline or output / "summary.json").resolve()
    validate_prefix(args.result_prefix)
    report_name = args.report_name or ("Optimierungsbericht.txt" if args.result_prefix == "optimized" else f"{args.result_prefix}-Bericht.txt")
    if any(character in report_name for character in '/\\:') or Path(report_name).suffix.lower() != ".txt":
        raise ValueError("report-name muss ein einfacher Dateiname mit Endung .txt sein.")
    json_path = output / f"{args.result_prefix}-summary.json"
    csv_path = output / f"{args.result_prefix}-summary.csv"
    report_path = output / report_name
    baseline_rows = read_json(baseline_path)
    if not isinstance(baseline_rows, list) or not baseline_rows:
        raise ValueError(f"Leere oder ungueltige Baseline: {baseline_path}")
    baseline = {}
    for row in baseline_rows:
        key = (row["operation"], row["mode"], row["count"])
        if key in baseline or row["samples"] != 9:
            raise ValueError(f"Doppelte oder unvollstaendige Baseline-Kombination: {key}")
        number(row["median_ms"], f"Baseline/{key}")
        row["_baseline_input_path"] = str(baseline_path)
        baseline[key] = row
    baseline_runs = {prefix: load_run(baseline_path.parent, prefix)
                     for prefix in sorted({row["source"] for row in baseline_rows})}
    if len(set(args.baseline_run)) != len(args.baseline_run):
        raise ValueError("Zusaetzliche Baseline-Praefixe duerfen nicht doppelt vorkommen.")
    extra_runs = []
    for prefix in args.baseline_run:
        if prefix in baseline_runs:
            raise ValueError(f"Baseline-Lauf ist bereits in der Zusammenfassung enthalten: {prefix}")
        run = load_run(output, prefix)
        extra_runs.append(run)
        baseline_runs[prefix] = run
    for row in summarize(extra_runs, {}, {}, baseline_path):
        key = (row["operation"], row["mode"], row["count"])
        if key in baseline:
            raise ValueError(f"Zusaetzlicher Baseline-Lauf wuerde vorhandene Kombination ersetzen: {key}")
        row["_baseline_input_path"] = baseline_runs[row["source"]]["json_path"]
        baseline[key] = row
    prefixes = args.prefixes or DEFAULT_PREFIXES
    if len(set(prefixes)) != len(prefixes):
        raise ValueError("Lauf-Praefixe duerfen nicht doppelt vorkommen.")
    runs = [load_run(output, prefix) for prefix in prefixes]
    rows = summarize(runs, baseline, baseline_runs, baseline_path)
    report = make_report(rows, runs, baseline_runs, baseline_path)
    input_paths = {baseline_path}
    for run in list(baseline_runs.values()) + runs:
        input_paths.update(Path(path).resolve() for path in run["raw_sample_files"])
    if any(path.resolve() in input_paths for path in (json_path, csv_path, report_path)):
        raise ValueError("Ausgabedatei wuerde eine Baseline oder Rohmessung ueberschreiben; anderen result-prefix verwenden.")
    if args.dry_run:
        matches = sum(row["baseline_median_ms"] is not None for row in rows)
        print(f"Dry run: {len(rows)} Kombinationen, {matches} mit passender Baseline; keine Dateien geschrieben.")
        print(f"Geplante Ausgaben: {json_path}; {csv_path}; {report_path}")
        return

    json_path.write_text(json.dumps(rows, indent=2, allow_nan=False), encoding="utf-8")
    with csv_path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value) if isinstance(value, list) else value for key, value in row.items()})
    report_path.write_text(report, encoding="utf-8-sig")
    print(f"{len(rows)} Kombinationen ausgewertet: {report_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"Auswertung abgebrochen: {error}") from error
