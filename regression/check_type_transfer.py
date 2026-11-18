"""Exercise typed FMU connections using the nova723 Python API."""

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import traceback
import xml.etree.ElementTree as ET
import zipfile
from datetime import datetime

ROOT = Path(__file__).resolve().parents[1]
TYPES = {
    "integer": ("Integer", "int", "integer", [7, -3, 0, 2147483647, -2147483648, 42]),
    "boolean": ("Boolean", "bool", "bool", [True, False, True, True, False, True]),
    "string": ("String", "string", "string", ["hello", "", "two words", "中文", 'a,b"c', "x" * 256]),
}


def check(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def worker(args):
    result = {"status": "failed", "mode": args.worker, "samples": []}
    try:
        check(args.fmu.is_file(), f"FMU not found: {args.fmu}")
        with zipfile.ZipFile(args.fmu) as archive:
            description = ET.fromstring(archive.read("modelDescription.xml"))
            check(description.get("fmiVersion") == "1.0", "Expected the supplied FMI 1.0 identity model")
            variables = {v.get("name"): v for v in description.findall("./ModelVariables/ScalarVariable")}
            for prefix, (kind, _, _, _) in TYPES.items():
                for suffix, causality in (("In", "input"), ("Out", "output")):
                    v = variables[prefix + suffix]
                    check(v.get("causality") == causality and v.find(kind) is not None,
                          f"Unexpected interface: {prefix}{suffix}")
        result.update(fmu=str(args.fmu), fmu_sha256=digest(args.fmu), nova_root=str(args.nova_root))
        sys.path.insert(0, str(args.nova_root))
        import nova_sim_py as nova

        check(hasattr(nova, "NovaEngine") and hasattr(nova.NovaSimulationStructure, "make_string_connection"),
              "This test needs nova723's typed NovaEngine API. Supply its folder with --nova-root.")
        library = args.nova_root / "nova_sim_py/binaries" / ("libnovac.dll" if os.name == "nt" else "libnovac.so")
        result.update(library_sha256=digest(library), library_version=str(nova.NovaLib.version()),
                      bindings_sha256={p.name: digest(p) for p in sorted((args.nova_root / "nova_sim_py").glob("*.py"))})
        with nova.NovaSimulationStructure() as structure:
            for name in ("source", "sink"):
                structure.add_model(name, os.path.relpath(args.fmu, Path.cwd()))
            if args.worker == "connected":
                for prefix, (_, connection, _, _) in TYPES.items():
                    getattr(structure, "make_" + connection + "_connection")(
                        "source::" + prefix + "Out", "sink::" + prefix + "In")
            with nova.NovaEngine(structure=structure, step_size=0.1) as sim:
                check(sim.init(), "Initialization failed")
                initial = {prefix: getattr(sim, "get_" + api)("sink", prefix + "Out")
                           for prefix, (_, _, api, _) in TYPES.items()}
                checks = {prefix: [] for prefix in TYPES}
                for index in range(6):
                    for prefix, (_, _, api, values) in TYPES.items():
                        getattr(sim, "set_" + api)("source", prefix + "In", values[index])
                    time = sim.step()
                    check(math.isclose(time, (index + 1) * 0.1, abs_tol=1e-12), "Unexpected step time")
                    for prefix, (_, _, api, values) in TYPES.items():
                        read = getattr(sim, "get_" + api)
                        expected = values[index]
                        source = read("source", prefix + "Out")
                        sink_input = read("sink", prefix + "In")
                        sink_output = read("sink", prefix + "Out")
                        target = expected if args.worker == "connected" else initial[prefix]
                        passed = source == expected and sink_input == target and sink_output == target
                        checks[prefix].append(passed)
                        result["samples"].append(dict(time=time, type=prefix, expected=expected, source=source,
                                                       sink_input=sink_input, sink_output=sink_output, passed=passed))
                sim.terminate()
            result["types"] = {kind: all(values) for kind, values in checks.items()}
            result["status"] = "passed" if all(result["types"].values()) else "failed"
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
        traceback.print_exc()
    save(args.output / (args.worker + ".json"), result)
    return 0 if result["status"] == "passed" else 1


def main():
    parser = argparse.ArgumentParser(description="Check Integer, Boolean and String transfer between two identity FMUs")
    parser.add_argument("--nova-root", type=Path, default=ROOT, help="NOVA project folder (default: this checkout)")
    parser.add_argument("--fmu", type=Path, default=ROOT / "data/fmus/1.0/identity.fmu")
    parser.add_argument("--worker", choices=("connected", "disconnected"), help=argparse.SUPPRESS)
    parser.add_argument("--output", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    args.nova_root = args.nova_root.resolve()
    args.fmu = args.fmu.resolve()
    if args.worker:
        return worker(args)
    output = ROOT / "results/regression" / ("type-transfer-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f"))
    output.mkdir(parents=True)
    results = []
    for mode in ("connected", "disconnected"):
        command = [sys.executable, "-B", str(Path(__file__).resolve()), "--nova-root", str(args.nova_root),
                   "--fmu", str(args.fmu), "--worker", mode, "--output", str(output)]
        with (output / (mode + ".log")).open("w", encoding="utf-8") as log:
            try:
                proc = subprocess.run(command, cwd=ROOT, env=dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8"),
                                      stdout=log, stderr=subprocess.STDOUT, timeout=30)
                path = output / (mode + ".json")
                result = json.loads(path.read_text(encoding="utf-8")) if path.exists() else {"status": "failed"}
                result.update(mode=mode, return_code=proc.returncode)
                if proc.returncode:
                    result["status"] = "failed"
            except subprocess.TimeoutExpired:
                result = {"status": "timeout", "mode": mode}
        results.append(result)
        print(mode, result["status"], flush=True)
    save(output / "summary.json", {"script_sha256": digest(Path(__file__)), "results": results})
    print(output)
    return 0 if all(r["status"] == "passed" for r in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
