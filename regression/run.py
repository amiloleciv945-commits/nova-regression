"""Run configured FMI 2.0 CS cases with the NOVA library in this checkout."""

import argparse
import csv
import ctypes
from datetime import datetime
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import time
import traceback
import xml.etree.ElementTree as ET
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CASES = Path(__file__).resolve().parent / "cases"
LABELS = {"passed": "比较通过", "unverified": "运行完成，待参考数据核验",
          "compared": "已比较，待确认通过标准",
          "failed": "失败", "timeout": "超时", "review": "运行完成，日志需检查"}


def save_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False), encoding="utf-8")


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def positive(value, name):
    require(type(value) in (float, int) and math.isfinite(value) and value > 0,
            f"{name} 必须是有限的正数")


def validate_case(case):
    require(re.fullmatch(r"[A-Za-z0-9_-]+", case["id"]) is not None, "案例 id 只能使用字母、数字、下划线和短横线")
    positive(case["step_size"], "step_size")
    require(type(case["steps"]) is int and case["steps"] > 0, "steps 必须是正整数")
    positive(case.get("timeout_seconds", 60), "timeout_seconds")
    require(case["models"] and case["outputs"], "必须提供模型和输出变量")
    require(len(set(case["outputs"])) == len(case["outputs"]), "输出变量重复")
    variables, hashes = {}, {}
    for name, relative in case["models"].items():
        require(re.fullmatch(r"[A-Za-z0-9_-]+", name) is not None, f"实例名无效：{name}")
        path = (ROOT / relative).resolve()
        require(path.is_file(), f"缺少模型：{path}")
        with zipfile.ZipFile(path) as archive:
            root = ET.fromstring(archive.read("modelDescription.xml"))
            require(root.get("fmiVersion") == "2.0" and root.find("CoSimulation") is not None,
                    f"{name}：第一版脚本接收 FMI 2.0 Co-Simulation 模型")
            target = "win64" if os.name == "nt" else "linux64"
            require(any(n.startswith(f"binaries/{target}/") for n in archive.namelist()),
                    f"{name} 缺少 {target} 模型动态库")
            variables[name] = {v.get("name"): v for v in root.findall("./ModelVariables/ScalarVariable")}
        hashes[name] = {"file": relative, "sha256": digest(path)}

    def variable(endpoint, causality=None):
        instance, name = endpoint.split("::", 1)
        require(instance in variables and name in variables[instance], f"变量不存在：{endpoint}")
        node = variables[instance][name]
        require(node.find("Real") is not None, f"第一版只配置实数变量：{endpoint}")
        if causality:
            require(node.get("causality") == causality, f"{endpoint} 必须是 {causality} 变量")
        return node

    destinations = set()
    for source, destination in case.get("connections", []):
        variable(source, "output")
        variable(destination, "input")
        require(destination not in destinations, f"输入端重复连接：{destination}")
        destinations.add(destination)
    for output in case["outputs"]:
        variable(output)
    for endpoint, value in case.get("parameters", {}).items():
        node = variable(endpoint, "parameter")
        require(node.get("variability") in ("fixed", "tunable"), f"参数不可配置：{endpoint}")
        require(type(value) in (int, float) and math.isfinite(value), f"参数值无效：{endpoint}")
    require(math.isfinite(case["step_size"] * case["steps"]), "终止时间无效")
    return hashes


def read_numeric_csv(path, columns):
    with path.open(encoding="utf-8-sig", newline="") as stream:
        reader = csv.DictReader(stream, skipinitialspace=True)
        require(reader.fieldnames is not None and len(set(reader.fieldnames)) == len(reader.fieldnames),
                f"CSV 表头为空或重复：{path}")
        require(all(c in reader.fieldnames for c in columns), f"CSV 缺少要求的列：{path}")
        rows = []
        for row in reader:
            require(None not in row and all(v is not None for v in row.values()), f"CSV 列数不一致：{path}")
            numbers = [float(row[c]) for c in columns]
            require(all(math.isfinite(v) for v in numbers), f"CSV 含非有限数值：{path}")
            rows.append(numbers)
    require(rows, f"CSV 无数据：{path}")
    require(all(b[0] > a[0] for a, b in zip(rows, rows[1:])), f"时间未严格递增：{path}")
    return rows


def compare(actual_path, reference, outputs, detail_path=None):
    # A reference must be supplied explicitly; never generate a passing baseline from the new run.
    reference_path = (ROOT / reference["file"]).resolve()
    atol, rtol = reference.get("atol"), reference.get("rtol")
    require((atol is None) == (rtol is None), "atol和rtol须同时提供，或同时留空仅计算误差")
    assessed = atol is not None
    if assessed:
        for name, value in (("atol", atol), ("rtol", rtol)):
            require(type(value) in (float, int) and math.isfinite(value) and value >= 0,
                    f"{name} 必须是非负有限数")
    selected = reference.get("variables", outputs)
    require(isinstance(selected, list) and selected and len(set(selected)) == len(selected)
            and all(c in outputs for c in selected), "比较变量必须是outputs中的非空、不重复子集")
    alignment = reference.get("time_alignment", "strict")
    require(alignment in ("strict", "actual_times"), "未知time_alignment；支持strict或actual_times")
    reference_hash = digest(reference_path)
    if reference.get("sha256"):
        require(reference_hash == reference["sha256"], "参考文件SHA256与配置不符，需核对参考版本")
    mapping = reference.get("columns", {})
    require(all(c in selected for c in mapping), "列映射包含未选中的比较变量")
    actual = read_numeric_csv(actual_path, ["time", *selected])
    expected = read_numeric_csv(reference_path, [reference.get("time_column", "time"),
                                                *(mapping.get(c, c) for c in selected)])
    same_time = lambda a, b: math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-10)
    if alignment == "strict":
        require(len(actual) == len(expected), "参考结果与本次结果行数不同，需确认采样时间")
        matched = expected
    else:
        matched, index = [], 0
        for row in actual:
            while index < len(expected) and expected[index][0] < row[0] and not same_time(expected[index][0], row[0]):
                index += 1
            require(index < len(expected) and same_time(row[0], expected[index][0]),
                    f"参考结果时间网格缺少本次时间点{row[0]:.12g}；不插值或平移时间")
            require(index + 1 == len(expected) or not same_time(row[0], expected[index + 1][0]),
                    f"参考时间匹配不唯一：{row[0]:.12g}")
            matched.append(expected[index])
            index += 1
    maxima = dict.fromkeys(selected, 0.0)
    rms = dict.fromkeys(selected, 0.0)
    max_times = dict.fromkeys(selected, actual[0][0])
    counts = dict.fromkeys(selected, 0)
    details = []
    violations = 0
    for row, ref in zip(actual, matched):
        require(same_time(row[0], ref[0]),
                "参考结果时间网格不一致；本脚本未进行插值")
        for column, value, target in zip(selected, row[1:], ref[1:]):
            error = abs(value - target)
            require(math.isfinite(error), "误差计算溢出")
            limit = atol + rtol * abs(target) if assessed else None
            require(limit is None or math.isfinite(limit), "容差计算溢出")
            if error > maxima[column]:
                maxima[column], max_times[column] = error, row[0]
            rms[column] = math.hypot(rms[column], error / math.sqrt(len(actual)))
            if assessed and error > limit:
                violations += 1
                counts[column] += 1
            details.append([row[0], ref[0], column, value, target, error, limit,
                            int(error > limit) if assessed else None])
    require(all(math.isfinite(v) for v in rms.values()), "均方根误差计算溢出")
    if detail_path is not None:
        with detail_path.open("w", encoding="utf-8-sig", newline="") as stream:
            csv.writer(stream).writerows([["time", "reference_time", "variable", "actual", "reference",
                                           "abs_error", "allowed_error", "exceeds_tolerance"], *details])
    return {"passed": violations == 0 if assessed else None,
            "violations": violations if assessed else None,
            "violations_by_variable": counts if assessed else None,
            "max_abs_error": maxima, "rmse": rms, "max_abs_error_time": max_times,
            "samples": len(actual), "reference_samples": len(expected),
            "unused_reference_samples": len(expected) - len(matched),
            "max_time_difference": max(abs(a[0] - b[0]) for a, b in zip(actual, matched)),
            "variables": selected, "columns": {c: mapping.get(c, c) for c in selected},
            "time_alignment": alignment, "reference_source": reference.get("source", "未说明"),
            "reference_sha256": reference_hash, "atol": atol, "rtol": rtol}


def plot_comparison(folder):
    """Render with Windows' built-in chart library; numerical results do not depend on it."""
    script = Path(__file__).with_name("plot_comparison.ps1")
    shell = Path(os.environ.get("SystemRoot", "C:/Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe"
    if os.name != "nt" or not shell.is_file():
        return {"error": "自动绘图当前需Windows PowerShell和.NET图表库；比较明细CSV仍可使用"}
    proc = subprocess.run([str(shell), "-NoProfile", "-NonInteractive", "-File", str(script),
                           "-Folder", str(folder)], capture_output=True, timeout=30)
    (folder / "plot.log").write_bytes(proc.stdout + proc.stderr)
    charts = sorted(p.name for p in folder.glob("comparison-*.png"))
    if proc.returncode or not charts:
        return {"error": "对比曲线未生成，详见plot.log；数值比较记录已保留", "script_sha256": digest(script)}
    return {"files": charts, "script_sha256": digest(script)}


def worker(case_path, folder):
    if os.name == "nt":
        ctypes.windll.kernel32.SetErrorMode(3)  # Let the parent report native crashes without a blocking dialog.
    result = {"status": "failed", "stage": "检查案例", "python": sys.version,
              "platform": platform.platform(), "elapsed_seconds": 0.0,
              "runner_sha256": digest(Path(__file__))}
    started = time.perf_counter()
    try:
        case = json.loads(case_path.read_text(encoding="utf-8-sig"))
        result.update(id=case["id"], name=case.get("name", case["id"]),
                      config_sha256=digest(case_path), models=validate_case(case),
                      settings_source=case.get("settings_source", ""))
        result["output_ranges"] = {}
        os.chdir(ROOT)
        sys.path.insert(0, str(ROOT))
        result["stage"] = "加载 NOVA"
        from nova_sim_py import NovaEngine, NovaLib, NovaSimulationStructure
        result["library_version"] = str(NovaLib.version())
        library = ROOT / "nova_sim_py/binaries" / ("libnovac.dll" if os.name == "nt" else "libnovac.so")
        result["library_sha256"] = digest(library)
        result["bindings_sha256"] = {p.name: digest(p) for p in sorted((ROOT / "nova_sim_py").glob("*.py"))}
        NovaLib.set_log_level("info")
        with NovaSimulationStructure() as structure:
            result["stage"] = "加载模型和连接"
            for name, path in case["models"].items():
                structure.add_model(name, path)
            for source, destination in case.get("connections", []):
                structure.make_real_connection(source, destination)
            parameters = case.get("parameters", {})
            if parameters:
                structure.add_parameter_set("regression", {k: float(v) for k, v in parameters.items()})
            with NovaEngine(structure=structure, step_size=case["step_size"]) as sim:
                result["stage"] = "初始化"
                require(sim.init(parameter_set="regression" if parameters else None), "NOVA 初始化返回失败")
                result["stage"] = "步进和记录"
                with (folder / "result.csv").open("w", encoding="utf-8", newline="") as stream:
                    writer = csv.writer(stream)
                    writer.writerow(["time", *case["outputs"]])
                    current = 0.0
                    for index in range(case["steps"] + 1):
                        if index:
                            current = sim.step(1)
                        require(math.isfinite(current) and math.isclose(
                            current, index * case["step_size"], rel_tol=1e-9, abs_tol=1e-10),
                            f"第 {index} 步返回时间不正确：{current}")
                        values = [sim.get_real(*name.split("::", 1)) for name in case["outputs"]]
                        for name, value in zip(case["outputs"], values):
                            require(value is not None, f"第 {index} 步无法读取 {name}")
                            require(math.isfinite(value), f"第 {index} 步 {name} 出现 NaN 或无穷值")
                            bounds = result["output_ranges"].setdefault(name, {
                                "min": value, "max": value, "first": value, "last": value})
                            bounds["min"] = min(bounds["min"], value)
                            bounds["max"] = max(bounds["max"], value)
                            bounds["last"] = value
                        writer.writerow([current, *values])
                result["stage"] = "终止"
                sim.terminate()
        result.update(samples=case["steps"] + 1, final_time=current, step_size=case["step_size"])
        reference = case.get("reference")
        if reference:
            result["stage"] = "比较参考结果"
            shutil.copyfile((ROOT / reference["file"]).resolve(), folder / "reference.csv")
            result["comparison"] = compare(folder / "result.csv",
                                           {**reference, "file": str(folder / "reference.csv")},
                                           case["outputs"], folder / "comparison.csv")
            save_json(folder / "comparison.json", result["comparison"])
            verdict = result["comparison"]["passed"]
            result["status"] = "compared" if verdict is None else ("passed" if verdict else "failed")
            if verdict is None:
                result["note"] = "已计算参考数据差异；未配置允许误差，未判定通过"
            if result["status"] == "failed":
                result["error"] = "存在超过配置容差的数据点"
            try:
                result["plots"] = plot_comparison(folder)
            except Exception as exc:
                result["plots"] = {"error": f"绘图未完成：{exc}；数值比较记录已保留"}
        else:
            result.update(status="unverified", note="未提供已确认的参考结果，未判定数值精度合格")
        result["stage"] = "完成"
    except Exception as exc:
        result.update(status="failed", error=f"{type(exc).__name__}: {exc}")
        traceback.print_exc()
    result["elapsed_seconds"] = round(time.perf_counter() - started, 6)
    save_json(folder / "result.json", result)
    return 1 if result["status"] == "failed" else 0


def run_one(case_path, folder, timeout):
    folder.mkdir()
    shutil.copyfile(case_path, folder / "case.json")
    env = dict(os.environ, PYTHONIOENCODING="utf-8", PYTHONUTF8="1")
    started = time.perf_counter()
    with (folder / "run.log").open("w", encoding="utf-8") as log:
        try:
            proc = subprocess.run([sys.executable, str(Path(__file__).resolve()), "--worker",
                                   str(folder / "case.json"), str(folder)],
                                  cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=timeout)
            report = folder / "result.json"
            result = json.loads(report.read_text(encoding="utf-8")) if report.exists() else {
                "status": "failed", "stage": "子进程", "error": "进程退出且未产生结果记录"}
            result["return_code"] = proc.returncode
            if proc.returncode != 0:
                result["status"] = "failed"
        except subprocess.TimeoutExpired:
            result = {"status": "timeout", "stage": "子进程", "error": f"超过 {timeout} 秒，已停止此案例"}
    result["total_seconds"] = round(time.perf_counter() - started, 6)
    log_text = (folder / "run.log").read_text(encoding="utf-8", errors="replace")
    alerts = [line for line in log_text.splitlines()
              if re.search(r"\[(warning|warn|error|critical)\]|\[FMI2 Logger\].+assert\(", line, re.I)]
    if alerts:
        result["log_alerts"] = alerts
        if result["status"] in ("passed", "unverified", "compared"):
            result["status"] = "review"
    save_json(folder / "result.json", result)
    return result


def main():
    parser = argparse.ArgumentParser(description="NOVA 案例批量运行与结果比较")
    parser.add_argument("--case", action="append", help="案例 id，可重复指定；省略则运行全部配置")
    parser.add_argument("--list", action="store_true", help="只列出案例")
    parser.add_argument("--worker", nargs=2, help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.worker:
        return worker(*(Path(p).resolve() for p in args.worker))
    specs = {p.stem: p for p in sorted(CASES.glob("*.json"))}
    if args.list:
        for name, path in specs.items():
            case = json.loads(path.read_text(encoding="utf-8-sig"))
            print(f"{name}: {case.get('name', name)}")
        return 0
    selected = list(dict.fromkeys(args.case or specs))
    if not selected or any(name not in specs for name in selected):
        parser.error("案例为空或不存在；先使用 --list 查看")
    run_dir = ROOT / "results/regression" / datetime.now().strftime("%Y%m%d-%H%M%S-%f")
    run_dir.mkdir(parents=True)
    results = []
    for name in selected:
        path = specs[name]
        try:
            config = json.loads(path.read_text(encoding="utf-8-sig"))
            timeout = config.get("timeout_seconds", 60)
            positive(timeout, "timeout_seconds")
            result = run_one(path, run_dir / name, timeout)
        except Exception as exc:
            result = {"status": "failed", "error": f"{type(exc).__name__}: {exc}"}
        result["case"] = name
        results.append(result)
        print(f"{name}: {LABELS[result['status']]}", flush=True)
    save_json(run_dir / "summary.json", results)
    with (run_dir / "summary.csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.writer(stream)
        writer.writerow(["案例", "状态", "样本数", "终止时间", "总耗时秒", "说明"])
        for r in results:
            writer.writerow([r["case"], LABELS[r["status"]], r.get("samples", ""),
                             r.get("final_time", ""), r.get("total_seconds", ""), r.get("error", r.get("note", ""))])
    lines = ["# 仿真运行记录", "", f"运行目录：{run_dir.name}", "",
             "| 案例 | 状态 | 数据行数 | 仿真终止时间（秒） |", "| --- | --- | ---: | ---: |"]
    for r in results:
        final_time = r.get("final_time")
        lines.append(f"| {r['case']} | {LABELS[r['status']]} | {r.get('samples', '')} | "
                     f"{format(final_time, '.10g') if final_time is not None else ''} |")
    lines.extend(["", "## 输出变量范围", "", "以下数值便于检查异常增长，合格范围需结合模型及参考数据判断。", "",
                  "| 案例 | 变量 | 最小值 | 最大值 | 最终值 |", "| --- | --- | ---: | ---: | ---: |"])
    for r in results:
        for name, bounds in r.get("output_ranges", {}).items():
            lines.append(f"| {r['case']} | {name} | {bounds['min']:.6g} | {bounds['max']:.6g} | {bounds['last']:.6g} |")
    lines.extend(["", "## 参考数据比较", "",
                  "仅比较配置中选定的变量。未配置容差时只计算误差，不判定通过。", "",
                  "| 案例 | 变量 | 比较点数 | 最大绝对差 | 均方根误差 | 最大差异时刻（秒） |",
                  "| --- | --- | ---: | ---: | ---: | ---: |"])
    for r in results:
        comparison = r.get("comparison")
        if comparison:
            for name, error in comparison["max_abs_error"].items():
                lines.append(f"| {r['case']} | {name} | {comparison['samples']} | {error:.6g} | "
                             f"{comparison['rmse'][name]:.6g} | {comparison['max_abs_error_time'][name]:.10g} |")
    for r in results:
        comparison = r.get("comparison")
        if comparison:
            tolerance_text = ("允许误差尚未约定，本次只计算差异。" if comparison["atol"] is None else
                              f"绝对容差：{comparison['atol']}；相对容差：{comparison['rtol']}。")
            lines.extend(["", f"### {r['case']} 比较依据", "",
                          f"参考来源：{comparison['reference_source']}。参考文件SHA256：{comparison['reference_sha256']}。",
                          f"参考记录{comparison['reference_samples']}行，匹配{comparison['samples']}个时间点；"
                          f"时间对齐模式为{comparison['time_alignment']}，没有插值或平移。",
                          tolerance_text,
                          f"[逐点比较CSV]({r['case']}/comparison.csv)；[比较汇总JSON]({r['case']}/comparison.json)。"])
            for chart in r.get("plots", {}).get("files", []):
                lines.extend(["", f"![{r['case']}对比曲线]({r['case']}/{chart})"])
            if r.get("plots", {}).get("error"):
                lines.extend(["", r["plots"]["error"]])
    lines.extend(["", "## 运行设置与问题", ""])
    for r in results:
        detail = r.get("error", r.get("note", ""))
        if r.get("settings_source"):
            detail += "；" + r["settings_source"]
        lines.append(f"- {r['case']}：{detail or LABELS[r['status']]}")
        if r.get("log_alerts"):
            lines.append(f"  - 记录到 {len(r['log_alerts'])} 条告警，见 [{r['case']}/run.log]({r['case']}/run.log)。")
    lines.extend(["", "每个案例文件夹保存 case.json（配置）、result.csv（时序数据）、"
                  "result.json（运行状态、版本和模型校验值）及 run.log（进程日志）。", "",
                  "“待参考数据核验”表示模型已完成运行，数值精度尚未验证。"
                  "耗时包含脚本和加载开销，不能直接用于合同效率对比。", ""])
    (run_dir / "report.md").write_text("\n".join(lines), encoding="utf-8")
    print(f"结果目录：{run_dir}")
    # Exit 2 means execution completed but numerical acceptance still needs review.
    return 1 if any(r["status"] in ("failed", "timeout") for r in results) else (
        2 if any(r["status"] != "passed" for r in results) else 0)


if __name__ == "__main__":
    raise SystemExit(main())
