import argparse
import csv
import json
import math
import re
import statistics
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUTPUT = ROOT / "output" / "ecs-benchmark-2026-10-28"
MODES = ("Seq", "Parallel", "Auto")
FLAGS = ("EcbSerialKernels", "EcbParallelDiagnostics", "EcbTimings")
BINARIES = {"Funke-Runtime.exe", "Funke-Engine.dll", "Funke-ScriptCore.dll", "EcsBenchmark.dll"}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def validate_prefix(value):
    require(re.fullmatch(r"[A-Za-z0-9_-]+", value) is not None, f"Ungueltiger Praefix: {value!r}")


def enabled(value):
    require(value is None or isinstance(value, str), f"Ungueltiger Environment-Wert: {value!r}")
    return bool(value and value.startswith("1"))


def hash_map(entries):
    result = {}
    for entry in entries:
        name = entry["Path"].replace("\\", "/").rsplit("/", 1)[-1]
        digest = entry["Hash"].upper()
        require(name not in result, f"Doppelter Hash-Eintrag: {name}")
        require(entry["Algorithm"] == "SHA256" and re.fullmatch(r"[0-9A-F]{64}", digest),
                f"Ungueltiger SHA256-Eintrag: {name}")
        result[name] = digest
    return result


def as_list(value):
    return value if isinstance(value, list) else [value]


def hardware(machine):
    cpus = as_list(machine["Cpu"])
    memory = as_list(machine["Memory"])
    return {
        "cpus": sorted((cpu["Name"].strip(), cpu["NumberOfCores"], cpu["NumberOfLogicalProcessors"]) for cpu in cpus),
        "memory_bytes": sum(module["Capacity"] for module in memory),
        "memory_configured_rates": sorted({module["ConfiguredClockSpeed"] for module in memory}),
        "memory_types": sorted({module["SMBIOSMemoryType"] for module in memory if module.get("SMBIOSMemoryType") is not None}),
        "power_scheme": machine["PowerScheme"],
    }


def load_run(folder, prefix, role, final):
    validate_prefix(prefix)
    require(not (folder / f"{prefix}.failed").exists(), f"Fehlgeschlagener Lauf: {prefix}")
    require((folder / f"{prefix}.done").read_text(encoding="utf-8-sig").strip() == "PASS",
            f"Lauf nicht erfolgreich abgeschlossen: {prefix}")
    data = read_json(folder / f"{prefix}.json")
    machine = read_json(folder / f"{prefix}-machine.json")
    require(data["Warmups"] == 3 and data["Repetitions"] == 9, f"{prefix}: 3 Warm-ups und 9 Proben erforderlich.")
    require(isinstance(data["Samples"], list) and data["Samples"], f"{prefix}: Keine Messproben.")
    log = (folder / f"{prefix}-stdout.log").read_text(encoding="utf-8-sig")
    require("[ECSBENCH] ALL PASS" in log and "[ECSBENCH] FAIL" not in log, f"{prefix}: Kein erfolgreicher Log-Abschluss.")
    require("[EcbTiming]" not in log and "[EcbParallel]" not in log, f"{prefix}: Diagnose-Logging verfaelscht Zeitmessung.")
    provenance = {}
    for flag in FLAGS:
        if final:
            require(flag in data and flag in machine, f"{prefix}: Finale Environment-Provenienz fehlt: {flag}")
        values = [source[flag] for source in (data, machine) if flag in source]
        require(not values or all(value == values[0] for value in values), f"{prefix}: Environment-Metadaten widersprechen sich: {flag}")
        provenance[flag] = {"recorded": bool(values), "value": values[0] if values else None}
        if values:
            expected = role == "serial_kernels" and flag == "EcbSerialKernels"
            require(enabled(values[0]) == expected, f"{prefix}: {flag} passt nicht zur angegebenen Rolle {role}.")
    hashes = hash_map(machine["Binaries"])
    require(set(hashes) == BINARIES, f"{prefix}: Runtime/Engine/Core/Harness-Hashes unvollstaendig.")
    source_hashes = hash_map(machine["Sources"])
    require(bool(source_hashes), f"{prefix}: Keine Quell-Hashes.")
    return {
        "prefix": prefix, "role": role, "data": data,
        "binary_hashes": hashes, "source_hashes": source_hashes,
        "hardware": hardware(machine), "environment": provenance,
        "machine_path": str(folder / f"{prefix}-machine.json"),
        "log_path": str(folder / f"{prefix}-stdout.log"),
        "raw_sample_files": [str(folder / f"{prefix}{extension}") for extension in (".json", ".jsonl", ".csv")
                             if (folder / f"{prefix}{extension}").is_file()],
    }


def summarize(runs):
    groups = defaultdict(list)
    owners = {}
    reference = runs[0]
    configuration = ("Runtime", "Workers", "ProcessorCount", "StopwatchFrequency", "ServerGC", "TieredCompilation")
    for run in runs:
        for field in ("binary_hashes", "source_hashes", "hardware"):
            require(run[field] == reference[field], f"{run['prefix']}: Abweichende {field}; Laeufe nicht zusammenfassen.")
        require(all(run["data"].get(field) == reference["data"].get(field) for field in configuration),
                f"{run['prefix']}: Abweichende Runtime-/Worker-Konfiguration.")
        for sample in run["data"]["Samples"]:
            operation, mode, count = sample["Operation"], sample["Mode"], sample["Count"]
            require(isinstance(operation, str) and operation.startswith("Playback") and mode in MODES,
                    f"{run['prefix']}: Keine Playback-Modusmessung: {operation}/{mode}")
            require(type(count) is int and count > 0 and type(sample["Sample"]) is int,
                    f"{run['prefix']}: Ungueltige Anzahl oder Probennummer.")
            key = (run["role"], operation, mode, count)
            require(key not in owners or owners[key] is run, f"Doppelte Kombination in mehreren Laeufen: {key}")
            owners[key] = run
            for field in ("TotalMs", "RecordMs", "PlaybackMs", "AllocatedBytes", "Gen0", "Gen1", "Gen2"):
                value = sample[field]
                require(type(value) in (int, float) and math.isfinite(value) and value >= 0,
                        f"{run['prefix']}: Ungueltiger Messwert {field}={value!r}")
            groups[key].append(sample)
    rows = []
    for key, samples in sorted(groups.items()):
        role, operation, mode, count = key
        require(len(samples) == 9 and sorted(sample["Sample"] for sample in samples) == list(range(9)),
                f"{key}: Genau 9 eindeutige Proben 0..8 erforderlich.")
        times = [sample["PlaybackMs"] for sample in samples]
        rows.append({
            "role": role, "operation": operation, "mode": mode, "count": count, "samples": 9,
            "playback_median_ms": statistics.median(times), "playback_min_ms": min(times), "playback_max_ms": max(times),
            "total_median_ms": statistics.median(sample["TotalMs"] for sample in samples),
            "record_median_ms": statistics.median(sample["RecordMs"] for sample in samples),
            "managed_allocated_bytes_median": statistics.median(sample["AllocatedBytes"] for sample in samples),
            "gen0_total": sum(sample["Gen0"] for sample in samples),
            "gen1_total": sum(sample["Gen1"] for sample in samples),
            "gen2_total": sum(sample["Gen2"] for sample in samples),
            "source": owners[key]["prefix"], "raw_sample_files": owners[key]["raw_sample_files"],
        })
    lookup = {(row["role"], row["operation"], row["mode"], row["count"]): row for row in rows}
    for row in rows:
        seq = lookup.get((row["role"], row["operation"], "Seq", row["count"]))
        serial = lookup.get(("serial_kernels", row["operation"], row["mode"], row["count"])) if row["role"] == "normal" else None
        median = row["playback_median_ms"]
        row["seq_median_ms"] = seq["playback_median_ms"] if seq else None
        row["speedup_vs_seq"] = seq["playback_median_ms"] / median if seq and median > 0 else None
        row["serial_kernels_median_ms"] = serial["playback_median_ms"] if serial else None
        row["worker_speedup"] = serial["playback_median_ms"] / median if serial and median > 0 and row["mode"] != "Seq" else None
        row["serial_kernels_source"] = serial["source"] if serial else None
    return rows


def format_number(value, digits=4):
    return f"{value:.{digits}f}" if value is not None else "--"


def report(rows, runs, final):
    reference = runs[0]
    machine = reference["hardware"]
    lines = ["FUNKE ECS — VERGLEICH DER NATIVEN PLAYBACK-MODI",
             "Status: final freigegebene Laeufe" if final else "Status: Zwischenstand; keine finale Freigabe angegeben", "",
             "METHODE",
             "3 Warm-ups, danach genau 9 Messproben je Operation/Modus/Anzahl. Median, Minimum und Maximum",
             "beziehen sich auf PlaybackMs in Millisekunden fuer die ganze Charge, nicht auf einzelne Entities.",
             "PlaybackMs umfasst den synchronen C#-Playback-Aufruf einschliesslich Wirecopy und Interop.",
             "Aufzeichnung, Fixture-Setup, Ergebnispruefung und Cleanup sind nicht enthalten. Record ist hier eine leere Phase.",
             "Create/Add/Remove/Mixed verwenden einmal aufgezeichnete Buffer wiederholt; Live-Fixtures werden ausserhalb",
             "des Messfensters zurueckgesetzt. Destroy braucht pro Probe frische Entities und IDs im Setup.",
             "N bei Mixed bezeichnet N vorhandene plus N lokal erzeugte Entities, davon ueberlebt die Haelfte der lokalen Entities.",
             "",
             "Seq nutzt den bisherigen sequenziellen Pfad. Parallel kann neben Worker-Ausfuehrung auch",
             "einen anderen Batch-Algorithmus verwenden: Seq/Parallel ist daher kein reiner Thread-Speedup.",
             "Auto waehlt nach Eignung und Schwelle. Nicht unterstuetzte Batches, insbesondere gemischte Mutationen,",
             "fallen auf sequenzielle Ausfuehrung zurueck; der Modusname allein beweist keine Worker-Nutzung.",
             "Serial-Kernel-Kontrollen verwenden denselben Parallel-Modus mit FUNKE_ECB_SERIAL_KERNELS=1.",
             "Kontrollmedian / normaler Parallelmedian schaetzt den Worker-Anteil bei gleichem Batch-Algorithmus.",
             "Andere Last und Laufzeitstreuung bleiben moeglich; neun Proben sind keine Signifikanzpruefung.",
             "Kein Vergleich mit anderen Engines. Diagnoselaeufe mit EcbTiming/EcbParallel-Logging sind ausgeschlossen.", "",
             "HARDWARE UND KOHERENZ"]
    for name, cores, threads in machine["cpus"]:
        lines.append(f"CPU: {name}; {cores} Kerne, {threads} logische Prozessoren.")
    rates = ", ".join(str(rate) for rate in machine["memory_configured_rates"])
    memory_type = "DDR5" if machine["memory_types"] == [34] else "DDR4" if machine["memory_types"] == [26] else "Typ nicht eindeutig protokolliert"
    lines += [f"RAM: {machine['memory_bytes'] / 1024 ** 3:g} GiB {memory_type}; konfigurierte Datenrate laut System: {rates} MT/s.",
              f"Job-Worker: {reference['data']['Workers']}; .NET {reference['data']['Runtime']}.",
              f"Energieprofil: {machine['power_scheme']}",
              "Alle Eingaben: PASS, gleiche vier Binaer-Hashes, Quell-Hashes, Hardware und Runtime-/Worker-Konfiguration."]
    lines.extend(f"{name}: {digest}" for name, digest in sorted(reference["binary_hashes"].items()))
    if any(not all(item["recorded"] for item in run["environment"].values()) for run in runs):
        lines.append("Aeltere Eingaben haben keine vollstaendige Environment-Provenienz; Serial-Kernel-Rolle dort nur durch CLI angegeben.")
    normal_count = sum(row["role"] == "normal" for row in rows)
    control_count = len(rows) - normal_count
    lines += ["", "ERGEBNISSE", f"Normal: {normal_count} Kombinationen / {normal_count * 9} Messproben; "
              f"Kontrolle: {control_count} Kombinationen / {control_count * 9} Messproben."]
    for role in ("normal", "serial_kernels"):
        selected = [row for row in rows if row["role"] == role]
        if not selected:
            continue
        lines += ["", "Normale Ausfuehrung" if role == "normal" else "Kontrolle: serielle Kernels im Batch-Pfad",
                  f"{'Operation / N / Modus':56} {'Median ms':>11} {'Min ms':>11} {'Max ms':>11} {'Seq/Fall':>10} {'Kontrolle/Fall':>15} {'Alloc Bytes':>12}"]
        for row in sorted(selected, key=lambda row: (row["count"], row["operation"], MODES.index(row["mode"]))):
            label = f"{row['operation']} / {row['count']} / {row['mode']}"
            lines.append(f"{label:56} {row['playback_median_ms']:11.4f} {row['playback_min_ms']:11.4f} {row['playback_max_ms']:11.4f} "
                         f"{format_number(row['speedup_vs_seq'], 3):>10} {format_number(row['worker_speedup'], 3):>15} "
                         f"{row['managed_allocated_bytes_median']:12.0f}")
    lines += ["", "Faktoren >1 bedeuten schneller, <1 langsamer; -- bedeutet fehlender passender Vergleich.",
              "Kontrolle/Fall ist nur bei passender Serial-Kernel-Messung desselben Modus und derselben Anzahl vorhanden.",
              "", "QUELLEN / ROHDATEN"]
    for run in runs:
        lines += [f"{run['role']}: {run['prefix']}", f"  {run['machine_path']}", f"  {run['log_path']}"]
        lines.extend(f"  {path}" for path in run["raw_sample_files"])
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description="Vergleicht vollstaendige Seq/Parallel/Auto-Playback-Messungen.")
    parser.add_argument("prefixes", nargs="+", help="Normale Laeufe ohne serielle Kernel-Erzwingung")
    parser.add_argument("--serial-kernels-run", action="append", default=[], metavar="PREFIX",
                        help="Kontrolllauf mit FUNKE_ECB_SERIAL_KERNELS=1; wiederholbar")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--result-prefix", default="playback")
    parser.add_argument("--report-name", default="Playback-Bericht.txt")
    parser.add_argument("--final", action="store_true", help="Nur nach Freigabe: finale Kennzeichnung und strikte Environment-Provenienz")
    parser.add_argument("--dry-run", action="store_true", help="Eingaben pruefen, ohne Dateien zu schreiben")
    args = parser.parse_args()
    folder = args.output_dir.resolve()
    validate_prefix(args.result_prefix)
    require(not any(character in args.report_name for character in '/\\:') and Path(args.report_name).suffix.lower() == ".txt",
            "report-name muss ein einfacher .txt-Dateiname sein.")
    prefixes = args.prefixes + args.serial_kernels_run
    require(len(prefixes) == len(set(prefixes)), "Lauf-Praefixe duerfen nur einmal angegeben werden.")
    paths = [folder / f"{args.result_prefix}-summary.json", folder / f"{args.result_prefix}-summary.csv", folder / args.report_name]
    require(not any(path.exists() for path in paths), "Mindestens eine Ausgabedatei existiert bereits; neuen result-prefix/report-name waehlen.")
    runs = [load_run(folder, prefix, "normal", args.final) for prefix in args.prefixes]
    runs += [load_run(folder, prefix, "serial_kernels", args.final) for prefix in args.serial_kernels_run]
    rows = summarize(runs)
    text = report(rows, runs, args.final)
    result = {
        "status": "final" if args.final else "intermediate", "metric": "PlaybackMs", "unit": "ms per complete batch",
        "warmups": 3, "samples_per_combination": 9,
        "binary_hashes": runs[0]["binary_hashes"], "source_hashes": runs[0]["source_hashes"], "hardware": runs[0]["hardware"],
        "runs": [{key: value for key, value in run.items() if key != "data"} | {
            "runtime": run["data"]["Runtime"], "workers": run["data"]["Workers"], "utc_time": run["data"].get("UtcTime")
        } for run in runs], "rows": rows,
    }
    if args.dry_run:
        print(f"Dry run: {len(rows)} vollstaendige Kombinationen; keine Dateien geschrieben.")
        return
    paths[0].write_text(json.dumps(result, indent=2, allow_nan=False), encoding="utf-8")
    with paths[1].open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        for row in rows:
            writer.writerow({key: json.dumps(value) if isinstance(value, list) else value for key, value in row.items()})
    paths[2].write_text(text, encoding="utf-8-sig")
    print(f"{len(rows)} Kombinationen geschrieben: {paths[2]}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"Playback-Auswertung abgebrochen: {error}") from error
