import argparse
import json
import math
import re
import statistics
from pathlib import Path


ROOT = Path(__file__).resolve().parents[3]
DEFAULT_OUTPUT = ROOT / "output" / "ecs-benchmark-2026-10-28"
TARGETS = (
    ("CreatePlus2Components", "ECB_DefaultConstruct", 100000),
    ("AddComponentExisting", "ECB", 100000),
    ("RemoveComponentExisting", "ECB", 100000),
)
PHASES = ("preflight_ms", "prepare_ms", "allocate_ms", "metadata_ms", "execute_ms", "finalize_ms", "deferred_ms")
SEGMENT = re.compile(r"\[ECSBENCH\] (START|RESULT) (\w+) / (\w+) / N=(\d+)")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def load_profile(folder, prefix):
    require(prefix and all(character.isascii() and (character.isalnum() or character in "-_") for character in prefix),
            f"Ungueltiger Lauf-Praefix: {prefix!r}")
    done = folder / f"{prefix}.done"
    require(done.is_file() and done.read_text(encoding="utf-8-sig").strip() == "PASS",
            f"Profiling-Lauf noch nicht erfolgreich abgeschlossen: {done}")
    require(not (folder / f"{prefix}.failed").exists(), f"Profiling-Lauf fehlgeschlagen: {prefix}")
    json_path = folder / f"{prefix}.json"
    metadata = json.loads(json_path.read_text(encoding="utf-8-sig"))
    require(metadata["Warmups"] == 3 and metadata["Repetitions"] == 9,
            f"{prefix}: Erwartet werden 3 Warm-ups und 9 Messungen.")
    log_path = folder / f"{prefix}-stdout.log"
    lines = log_path.read_text(encoding="utf-8-sig").splitlines()
    require(any("[ECSBENCH] ALL PASS" in line for line in lines), f"{prefix}: Abschlussmarker fehlt im Log.")
    require(not any("[ECSBENCH] FAIL" in line for line in lines), f"{prefix}: Fehler im Benchmark-Log.")
    segments = {}
    active = None
    entries = []
    for line_number, line in enumerate(lines, 1):
        match = SEGMENT.search(line)
        if match:
            kind, operation, mode, count = match.groups()
            key = (operation, mode, int(count))
            if kind == "START":
                require(active is None, f"{prefix}:{line_number}: Neuer START vor dem vorherigen RESULT.")
                active = key
                entries = []
            else:
                require(active == key, f"{prefix}:{line_number}: RESULT passt nicht zum START.")
                if key in TARGETS:
                    require(key not in segments, f"{prefix}: Doppelter Abschnitt: {key}")
                    segments[key] = entries
                active = None
            continue
        if active in TARGETS and "[EcbTiming] " in line:
            payload = line.split("[EcbTiming] ", 1)[1]
            fields = dict(token.split("=", 1) for token in payload.split())
            require(set(fields) == {"entities", "commands", *PHASES},
                    f"{prefix}:{line_number}: Unerwartete Timing-Felder.")
            record = {"line": line_number, "entities": int(fields["entities"]), "commands": int(fields["commands"])}
            for phase in PHASES:
                record[phase] = float(fields[phase])
                require(math.isfinite(record[phase]) and record[phase] >= 0,
                        f"{prefix}:{line_number}: Ungueltiger Wert fuer {phase}.")
            entries.append(record)
    require(active is None, f"{prefix}: Unvollstaendiger letzter Abschnitt.")
    require(set(segments) == set(TARGETS), f"{prefix}: Erwartete Benchmark-Abschnitte fehlen.")

    actions = {}
    for key in TARGETS:
        samples = [sample for sample in metadata["Samples"]
                   if (sample["Operation"], sample["Mode"], sample["Count"]) == key]
        require(len(samples) == 9 and sorted(sample["Sample"] for sample in samples) == list(range(9)),
                f"{prefix}/{key}: Genau 9 eindeutige Benchmark-Messungen erforderlich.")
        entries = segments[key]
        if key[0] == "CreatePlus2Components":
            pattern = ((100000, 200000), (0, 100000))
            selected = [entry for entry in entries if (entry["entities"], entry["commands"]) == pattern[0]]
        else:
            fixture_commands = 100000 if key[0] == "AddComponentExisting" else 200000
            pattern = ((100000, fixture_commands), (0, 100000), (0, 100000))
            live_entries = [entry for entry in entries if (entry["entities"], entry["commands"]) == (0, 100000)]
            require(len(live_entries) == 24, f"{prefix}/{key}: Erwartet werden 12 Aktionen und 12 Cleanups.")
            selected = live_entries[::2]
        actual_pattern = [(entry["entities"], entry["commands"]) for entry in entries]
        require(actual_pattern == list(pattern) * 12,
                f"{prefix}/{key}: Timing-Reihenfolge oder Anzahl von Setup/Aktion/Cleanup stimmt nicht.")
        require(len(selected) == 12, f"{prefix}/{key}: Genau 12 Aktionsmessungen erforderlich.")
        actions[key] = selected[3:]
    return {"prefix": prefix, "log_path": str(log_path), "json_path": str(json_path), "actions": actions}


def compare_profiles(before, after):
    comparisons = []
    for operation, mode, count in TARGETS:
        key = (operation, mode, count)
        old_samples = before["actions"][key]
        new_samples = after["actions"][key]
        phases = []
        for phase in PHASES:
            old_median = statistics.median(sample[phase] for sample in old_samples)
            new_median = statistics.median(sample[phase] for sample in new_samples)
            phases.append({
                "phase": phase,
                "before_median_ms": old_median,
                "after_median_ms": new_median,
                "delta_ms": new_median - old_median,
                "change_pct": (new_median / old_median - 1) * 100 if old_median > 0 else None,
                "phase_speedup": old_median / new_median if new_median > 0 else None,
            })
        comparisons.append({
            "operation": operation, "mode": mode, "count": count,
            "action_samples": 12, "discarded_warmups": 3, "measured_samples": 9,
            "before_measured_log_lines": [sample["line"] for sample in old_samples],
            "after_measured_log_lines": [sample["line"] for sample in new_samples],
            "phases": phases,
        })
    return {
        "unit": "ms per complete batch",
        "diagnostic_only": True,
        "note": "Native Diagnosemessung mit Logging; Phasenmediane erklaeren Kostenanteile, keine normalen Benchmark-Gesamtzeiten.",
        "before_log": before["log_path"], "after_log": after["log_path"],
        "before_run_json": before["json_path"], "after_run_json": after["json_path"],
        "comparisons": comparisons,
    }


def format_report(result):
    lines = [
        "FUNKE — NATIVER PHASENVERGLEICH (DIAGNOSE)", "",
        "N = 100000. Genau 12 Aktionsmessungen je Operation; die ersten 3 sind Warm-ups.",
        "Angegeben ist pro Phase der Median der letzten 9 Messungen in Millisekunden pro Charge.",
        "START/RESULT begrenzen die Abschnitte. Create: entities=100000, commands=200000.",
        "Add/Remove: entities=0, commands=100000; jede zweite passende Zeile ist Cleanup",
        "und wird ausgeschlossen. Anzahl und Reihenfolge von Setup/Aktion/Cleanup sind validiert.",
        "Native Timing-Ausgabe und Logging beeinflussen den Lauf. Die Phasen sind erklaerend;",
        "Gesamtzeiten dieses Profiling-Laufs sind nicht mit den normalen Benchmark-Laeufen vergleichbar.",
        "Phasenmediane lassen sich nicht zu einem gemessenen Gesamtmedian addieren.",
        f"Vorher: {result['before_log']}", f"Nachher: {result['after_log']}", "",
    ]
    for comparison in result["comparisons"]:
        lines += [f"{comparison['operation']} / {comparison['mode']}",
                  f"{'Phase':16} {'Vorher ms':>12} {'Nachher ms':>12} {'Delta ms':>12} {'Faktor':>10}"]
        for phase in comparison["phases"]:
            speedup = f"{phase['phase_speedup']:.3f}x" if phase["phase_speedup"] is not None else "--"
            lines.append(f"{phase['phase']:16} {phase['before_median_ms']:12.6f} {phase['after_median_ms']:12.6f} "
                         f"{phase['delta_ms']:12.6f} {speedup:>10}")
        lines.append("")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description="Vergleicht vollstaendige native ECB-Diagnoselaeufe.")
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--before", default="native2-profile-before")
    parser.add_argument("--after", default="native2-profile-verified")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    folder = args.output_dir.resolve()
    before = load_profile(folder, args.before)
    after = load_profile(folder, args.after)
    result = compare_profiles(before, after)
    if args.dry_run:
        print("Beide Profile vollstaendig: 3 Operationen, je 12 Aktionen und 9 ausgewertete Phasenmessungen. Keine Dateien geschrieben.")
        return
    json_path = folder / "Native-Phasenvergleich.json"
    text_path = folder / "Native-Phasenvergleich.txt"
    require(not json_path.exists() and not text_path.exists(), "Phasenvergleich existiert bereits; keine Datei wurde ueberschrieben.")
    json_path.write_text(json.dumps(result, indent=2, allow_nan=False), encoding="utf-8")
    text_path.write_text(format_report(result), encoding="utf-8-sig")
    print(f"3 native Phasenvergleiche geschrieben: {text_path}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise SystemExit(f"Phasenauswertung abgebrochen: {error}") from error
