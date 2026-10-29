"""Validate and summarize the six frozen-fixture ECS comparison runs."""

import argparse
import csv
import hashlib
import json
import math
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path, PureWindowsPath
from statistics import median


OPERATIONS = ("Create2", "AddSprite", "RemoveSprite", "Destroy")
LABELS = {
    "Create2": "Create + Sprite",
    "AddSprite": "AddComponent",
    "RemoveSprite": "RemoveComponent",
    "Destroy": "Destroy",
}
MANAGED_MODES = ("ManagedDirect", "ManagedECBParallel")
NATIVE_MODES = ("NativeDirect", "NativeECBParallel", "NativeParallelRecordECB")
BINARY_NAMES = {
    "Funke-Runtime.exe", "Funke-Engine.dll", "Funke-ScriptCore.dll",
    "EcsNativeComparison.dll", "EcsNativeComparison-NativeScripts.dll",
}
FIXTURES = {
    "before": "EcsNativeComparison-20261029",
    "after": "EcsDirectParallel-20261029",
}
RUNS = tuple(
    (f"{version}-{language.lower()}-{size}", version, language, count)
    for size, count in (("100k", 100000), ("1m", 1000000))
    for version, language in (("before", "Managed"), ("after", "Managed"), ("after", "Native"))
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def stats(values):
    ordered = sorted(values)

    def percentile(fraction):
        position = fraction * (len(ordered) - 1)
        lower = math.floor(position)
        upper = math.ceil(position)
        return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)

    q1, q3 = percentile(0.25), percentile(0.75)
    return {
        "Median": median(ordered), "Minimum": ordered[0], "Maximum": ordered[-1],
        "Q1": q1, "Q3": q3, "IQR": q3 - q1, "Samples": len(ordered),
    }


def binary_map(machine, version, tag):
    binaries = {}
    for item in machine["Binaries"]:
        path = PureWindowsPath(item["Path"])
        require(path.name not in binaries, f"{tag}: doppelter Binaername {path.name}")
        require(FIXTURES[version] in path.parts, f"{tag}: unerwarteter Fixturepfad {path}")
        require(item["Algorithm"] == "SHA256", f"{tag}: Hashalgorithmus ist nicht SHA256")
        digest = item["Hash"].upper()
        require(len(digest) == 64 and all(c in "0123456789ABCDEF" for c in digest), f"{tag}: ungueltiger Hash")
        binaries[path.name] = digest
    require(set(binaries) == BINARY_NAMES, f"{tag}: unvollstaendige Binaerprovenienz")
    return binaries


def hardware_key(machine):
    cpu = machine["CPU"]
    memory = machine["Memory"]
    if isinstance(memory, dict):
        memory = [memory]
    return (
        cpu["Name"].strip(), cpu["NumberOfCores"], cpu["NumberOfLogicalProcessors"],
        tuple(sorted((module["Capacity"], module["ConfiguredClockSpeed"]) for module in memory)),
        machine["OS"]["Caption"], machine["OS"]["Version"], machine["PowerScheme"],
    )


def historical_source_proof(directory, hashes):
    snapshots = {
        "before": directory.parent / "ecs-native-comparison-2026-10-29" / "managed-1m-machine.json",
        "after": directory.parent / "ecs-direct-parallel-2026-10-29" / "validation-native-fixed-8192-machine.json",
    }
    proof = {}
    for version, path in snapshots.items():
        machine = read_json(path)
        require(binary_map(machine, version, path.name) == hashes[version], f"{version}: frische Binaries weichen von historischem Fixture-Snapshot ab")
        sources = [item for item in machine["Sources"] if PureWindowsPath(item["Path"]).name == "ManagedEcsBenchmark.cs"]
        require(len(sources) == 1, f"{version}: historischer C#-Harness-Sourcehash fehlt")
        proof[version] = {"MetadataFile": str(path), "ManagedHarnessSHA256": sources[0]["Hash"].upper()}
    require(proof["before"]["ManagedHarnessSHA256"] == proof["after"]["ManagedHarnessSHA256"], "Historische C#-Harness-Quellen unterscheiden sich")
    return proof


def read_run(directory, tag, version, language, count):
    require(not (directory / f"{tag}.failed").exists(), f"{tag}: Fehlermarker vorhanden")
    require((directory / f"{tag}.done").read_text(encoding="utf-8-sig").strip() == "PASS", f"{tag}: PASS fehlt")
    exit_state = read_json(directory / f"{tag}-exit.json")
    require(exit_state["ExitCode"] == 0, f"{tag}: Runtime-Exitcode ist nicht 0")
    machine = read_json(directory / f"{tag}-machine.json")
    require(machine["Language"] == language and machine["Count"] == count, f"{tag}: falsche Sprache/Anzahl")
    require(machine["Warmups"] == 3 and machine["Samples"] == 9, f"{tag}: erwartet 3 Warmups und 9 Messungen")
    require(machine.get("Purpose") == "Performance measurement", f"{tag}: keine Performance-Messung")
    require(not machine.get("ActiveCompilers"), f"{tag}: Compiler waehrend der Messung aktiv")
    expected_filter = "NativeDirect|NativeECBParallel|NativeParallelRecordECB" if language == "Native" else ""
    require(machine.get("Filter", "") == expected_filter, f"{tag}: unerwarteter Fallfilter")
    binaries = binary_map(machine, version, tag)
    modes = NATIVE_MODES if language == "Native" else MANAGED_MODES
    expected_cases = {(operation, mode) for operation in OPERATIONS for mode in modes}
    samples = defaultdict(dict)
    csv_path = directory / f"{tag}.csv"
    with csv_path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream)
        require(reader.fieldnames == ["Operation", "Mode", "Count", "Sample", "Warmup", "RecordMs", "PlaybackMs", "TotalMs"], f"{tag}: unerwartetes CSV-Schema")
        for row in reader:
            key = row["Operation"], row["Mode"]
            require(key in expected_cases, f"{tag}: unerwarteter Fall {key}")
            require(int(row["Count"]) == count, f"{tag}: falsche Entity-Anzahl")
            warmup, index = int(row["Warmup"]), int(row["Sample"])
            require(warmup in (0, 1), f"{tag}: ungueltiges Warmup-Flag")
            require(0 <= index < (3 if warmup else 9), f"{tag}: ungueltiger Sample-Index")
            sample_key = warmup, index
            require(sample_key not in samples[key], f"{tag}: doppeltes Sample {key}/{sample_key}")
            values = {name: float(row[name]) for name in ("RecordMs", "PlaybackMs", "TotalMs")}
            require(all(math.isfinite(value) and value >= 0 for value in values.values()), f"{tag}: ungueltige Zeit")
            if row["Mode"].endswith("Direct"):
                require(values["RecordMs"] == values["PlaybackMs"] == 0, f"{tag}: Direct-Fall mit ECB-Zeiten")
            else:
                require(abs(values["RecordMs"] + values["PlaybackMs"] - values["TotalMs"]) < 0.00001, f"{tag}: Gesamtzeit passt nicht zu Record + Playback")
            require(values["TotalMs"] > 0, f"{tag}: Gesamtzeit muss positiv sein")
            samples[key][sample_key] = values
    require(set(samples) == expected_cases, f"{tag}: unvollstaendige Fallmatrix")
    expected_indices = {(1, i) for i in range(3)} | {(0, i) for i in range(9)}
    results = []
    for operation in OPERATIONS:
        for mode in modes:
            case = samples[operation, mode]
            require(set(case) == expected_indices, f"{tag}: unvollstaendige Warmups/Messungen {operation}/{mode}")
            measured = [case[0, i] for i in range(9)]
            results.append({
                "Tag": tag, "Version": version, "Language": language,
                "Count": count, "Operation": operation, "Mode": mode,
                **{name: stats([sample[name] for sample in measured]) for name in ("RecordMs", "PlaybackMs", "TotalMs")},
                "MeasuredSamples": measured,
            })
    provenance = {
        "Tag": tag, "Fixture": FIXTURES[version], "Version": version,
        "CSV": str(csv_path), "CSV_SHA256": hashlib.sha256(csv_path.read_bytes()).hexdigest().upper(),
        "Metadata": machine, "Exit": exit_state, "ValidatedCases": len(expected_cases),
        "ValidatedWarmups": len(expected_cases) * 3, "ValidatedMeasuredSamples": len(expected_cases) * 9,
    }
    return results, provenance, binaries


def summarize(directory):
    require(not (directory / "TIMING-STATUS.txt").exists(), "Verzeichnis ist als ungeeignet fuer Performance-Vergleiche markiert")
    all_results, provenance, hashes = [], [], {}
    first_hardware = None
    for tag, version, language, count in RUNS:
        results, run, binaries = read_run(directory, tag, version, language, count)
        if version in hashes:
            require(hashes[version] == binaries, f"{tag}: Binaerdateien innerhalb derselben Version geaendert")
        hashes[version] = binaries
        hardware = hardware_key(run["Metadata"])
        if first_hardware is not None:
            require(hardware == first_hardware, f"{tag}: Hardware/OS/Energieschema unterscheidet sich")
        first_hardware = hardware
        all_results.extend(results)
        provenance.append(run)
    lookup = {(row["Version"], row["Count"], row["Operation"], row["Mode"]): row for row in all_results}
    direct, parallel = [], []
    for count in (100000, 1000000):
        for operation in OPERATIONS:
            before = lookup["before", count, operation, "ManagedDirect"]["TotalMs"]["Median"]
            after = lookup["after", count, operation, "ManagedDirect"]["TotalMs"]["Median"]
            native = lookup["after", count, operation, "NativeDirect"]["TotalMs"]["Median"]
            direct.append({
                "Count": count, "Operation": operation, "BeforeManagedMs": before,
                "AfterManagedMs": after, "ReductionPercent": (1 - after / before) * 100,
                "Speedup": before / after, "NativeDirectMs": native, "ManagedVsNativeRatio": after / native,
            })
            phases = {}
            for mode in ("ManagedECBParallel", "NativeECBParallel", "NativeParallelRecordECB"):
                row = lookup["after", count, operation, mode]
                phases[mode] = {name: row[name]["Median"] for name in ("RecordMs", "PlaybackMs", "TotalMs")}
            parallel.append({"Count": count, "Operation": operation, **phases})
    return {
        "CreatedUTC": datetime.now(timezone.utc).isoformat(),
        "Warmups": 3, "MeasuredSamplesPerCase": 9, "Unit": "ms",
        "Quantiles": "Lineare Interpolation bei (n - 1) * p; IQR = Q3 - Q1",
        "Method": "Separate eingefrorene Release-x64-Fixtures vor/nach den Aenderungen; C# SceneSystem und echtes C++ NativeScript; 3 Warmups und 9 Messungen pro Fall; Tabellen zeigen Mediane. Je ein dauerhafter Treiber bleibt bestehen. Create erzeugt Transform2D + SpriteRenderer. C# nutzt die native Struct-API AddNativeComponent/RemoveNativeComponent fuer SpriteRenderer, keine verwalteten Component-Wrapper.",
        "Limitations": [
            "Vorher/Nachher vergleicht die gesamten eingefrorenen Versionen. Runtime-, Engine-, ScriptCore- und Harness-Binaerdateien duerfen sich zwischen Versionen unterscheiden; dies isoliert keine einzelne Optimierung.",
            "Direct vergleicht reale oeffentliche APIs mit unterschiedlichen Identitaets-, Wrapper- und Dispatch-Kosten; dies ist kein reiner C#-gegen-C++-Sprachvergleich.",
            "ECB-Tabellen: C# und C++ NativeECBParallel zeichnen seriell auf und spielen parallel ab. NativeParallelRecordECB zeichnet ueber den JobSystem-Pool parallel auf und spielt parallel ab.",
            "Setup, Validierung, Cleanup und nachtraegliche C#-Entity-Wrapper-Erzeugung sind ausserhalb der Zeit. Native oeffentliche Create-Playback-Zeiten enthalten bereits die Aufloesung der erzeugten nativen Handles. Vorab berechnete Existing-Referenzen sind ausserhalb der C++-Aufzeichnungszeit.",
            "Der oeffentliche C++-ECB prueft Commands und uebersetzt sie beim Playback in das gemeinsame Wire-Format, inklusive Handle-zu-UUID-Aufloesung fuer Live-Targets. C# zeichnet dieses Format bereits direkt auf. C++ Clear durchlaeuft vorherige Commands zur Capture-Freigabe; C# setzt Zaehler zurueck. Diese zusaetzliche API-Arbeit ist im Code belegt, ihr einzelner Zeitanteil wurde nicht separat gemessen.",
            "Record/Playback/Total sind jeweils eigene Mediane; die Summe der Phasenmediane muss nicht dem Median der Gesamtzeit entsprechen.",
            "Binaerhashes sind innerhalb jeder Version ueber alle zugehoerigen Laeufe identisch. Die Sources-Eintraege beschreiben Dateien zum Messstart, nicht zwingend die Build-Eingaben eingefrorener DLLs.",
            "Historische Metadaten belegen fuer beide passenden Fixture-Binaersaetze denselben C#-Harness-Sourcehash. Aus historischen Dateien werden ausschliesslich diese Provenienzdaten verwendet, keine Laufzeiten.",
            "Getrennte Prozesse auf einem Desktop: Scheduler, Cache, Allocator und GC koennen die Messungen beeinflussen. Aus Medianspeedups allein folgt keine statistische Signifikanz.",
        ],
        "Validation": {"AllPASS": True, "AllExitCodesZero": True, "CompleteCases": True, "StableBinariesWithinVersions": True, "SameHardwareOSPowerScheme": True, "SameHistoricalManagedHarnessSource": True},
        "HistoricalManagedHarnessProvenance": historical_source_proof(directory, hashes),
        "BinariesByVersion": hashes, "Runs": provenance, "DirectComparison": direct,
        "ParallelECBComparison": parallel, "Statistics": all_results,
    }


def number(value, precision=3):
    return f"{value:.{precision}f}".replace(".", ",")


def render(report):
    machine = report["Runs"][0]["Metadata"]
    cpu = machine["CPU"]
    memory = machine["Memory"]
    if isinstance(memory, dict):
        memory = [memory]
    capacity = sum(item["Capacity"] for item in memory) / 1024 ** 3
    speeds = "/".join(str(speed) for speed in sorted({item["ConfiguredClockSpeed"] for item in memory}))
    lines = [
        "Funke ECS: frischer Vorher/Nachher- und C++-Vergleich",
        f"{cpu['Name'].strip()}; {cpu['NumberOfCores']} Kerne / {cpu['NumberOfLogicalProcessors']} Threads; {capacity:g} GiB RAM, {speeds} MT/s.",
        f"{machine['OS']['Caption']} {machine['OS']['Version']}; {machine['PowerScheme']}",
        "Alle 6 Laeufe: PASS, Exitcode 0; Fallmatrix, 3 Warmups und 9 Messungen/Fall vollstaendig geprueft.",
        report["Method"], "",
    ]
    for count in (100000, 1000000):
        lines.extend([
            f"Direkte Aufrufe: {count:,} Entities (ms, Median)".replace(",", "."),
            "Aktion | C# vorher | C# nachher | Zeitreduktion | Speedup | C++ direkt | C# / C++",
        ])
        for row in report["DirectComparison"]:
            if row["Count"] == count:
                lines.append(" | ".join([
                    LABELS[row["Operation"]], number(row["BeforeManagedMs"]), number(row["AfterManagedMs"]),
                    number(row["ReductionPercent"], 1) + " %", number(row["Speedup"], 2) + "x",
                    number(row["NativeDirectMs"]), number(row["ManagedVsNativeRatio"], 2) + "x",
                ]))
        lines.extend(["", f"Paralleles ECB: {count:,} Entities (Record / Playback / Gesamt in ms)".replace(",", "."),
                      "Aktion | C# seriell / parallel | C++ seriell / parallel | C++ parallel / parallel"])
        for row in report["ParallelECBComparison"]:
            if row["Count"] == count:
                phases = [" / ".join(number(row[mode][name]) for name in ("RecordMs", "PlaybackMs", "TotalMs"))
                          for mode in ("ManagedECBParallel", "NativeECBParallel", "NativeParallelRecordECB")]
                lines.append(" | ".join([LABELS[row["Operation"]], *phases]))
        lines.append("")
    lines.extend(report["Limitations"])
    lines.extend(["", "Streuung je Fall: Minimum / Maximum / IQR (ms); Quartile mit linearer Interpolation.",
                  "Tag | Aktion | Modus | Record min/max/IQR | Playback min/max/IQR | Gesamt min/max/IQR"])
    for row in report["Statistics"]:
        phases = [" / ".join(number(row[name][stat]) for stat in ("Minimum", "Maximum", "IQR"))
                  for name in ("RecordMs", "PlaybackMs", "TotalMs")]
        lines.append(" | ".join([row["Tag"], LABELS[row["Operation"]], row["Mode"], *phases]))
    lines.extend(["", "SHA256 der eingefrorenen Binaerdateien:"])
    for version, binaries in report["BinariesByVersion"].items():
        for name, digest in sorted(binaries.items()):
            lines.append(f"{version} | {name} | {digest}")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--results", type=Path, default=Path(__file__).resolve().parents[3] / "output" / "ecs-final-comparison-2026-10-29")
    parser.add_argument("--name", default="Vergleich-VM-aus-2026-10-29")
    args = parser.parse_args()
    require(Path(args.name).name == args.name and args.name not in (".", ".."), "Ungueltiger Reportname")
    directory = args.results.resolve()
    report = summarize(directory)
    rendered = render(report)
    (directory / f"{args.name}.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    (directory / f"{args.name}.txt").write_text(rendered, encoding="utf-8")
    print(rendered.split("Streuung je Fall:")[0].rstrip())
    print(f"\nVollstaendige Statistik und Rohsample-Provenienz: {directory / (args.name + '.json')}")


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, KeyError, TypeError) as error:
        raise SystemExit(f"Vergleich abgebrochen: {error}")
