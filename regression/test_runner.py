"""Checks for the runner itself; the toy CSVs below are not model acceptance data."""

import contextlib
import csv
import io
import json
import math
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import run


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.actual = self.root / "actual.csv"
        self.reference = self.root / "reference.csv"
        self.config = {"file": str(self.reference), "atol": 0.01, "rtol": 0.001}

    def csv(self, path, rows, header=("time", "value")):
        with path.open("w", encoding="utf-8", newline="") as f:
            csv.writer(f).writerows([header, *rows])

    def test_tolerance_at_zero_and_nonzero(self):
        self.csv(self.actual, [[0, 0.005], [1, 100.05]])
        self.csv(self.reference, [[0, 0], [1, 100]])
        self.assertTrue(run.compare(self.actual, self.config, ["value"])["passed"])

    def test_difference_fails(self):
        self.csv(self.actual, [[0, 1], [1, 10]])
        self.csv(self.reference, [[0, 0], [1, 0]])
        report = run.compare(self.actual, self.config, ["value"])
        self.assertFalse(report["passed"])
        self.assertEqual(report["violations"], 2)

    def test_time_grid_mismatch(self):
        self.csv(self.actual, [[0, 0], [1, 1]])
        self.csv(self.reference, [[0, 0], [2, 1]])
        with self.assertRaisesRegex(ValueError, "时间网格"):
            run.compare(self.actual, self.config, ["value"])

    def test_bad_csv_is_rejected(self):
        for rows in ([], [[0, "nan"]], [[0, "inf"]], [[0, 1], [0, 1]], [[0]]):
            with self.subTest(rows=rows):
                self.csv(self.actual, rows)
                with self.assertRaises(ValueError):
                    run.read_numeric_csv(self.actual, ["time", "value"])

    def test_reference_column_mapping(self):
        self.csv(self.actual, [[0, 1], [1, 2]])
        self.csv(self.reference, [[0, 1], [1, 2]], ("Time", "reference_value"))
        self.config.update(time_column="Time", columns={"value": "reference_value"})
        self.assertTrue(run.compare(self.actual, self.config, ["value"])["passed"])

    def test_reference_missing_or_short(self):
        self.csv(self.actual, [[0, 1], [1, 2]])
        with self.assertRaises(FileNotFoundError):
            run.compare(self.actual, self.config, ["value"])
        self.csv(self.reference, [[0, 1]])
        with self.assertRaisesRegex(ValueError, "行数"):
            run.compare(self.actual, self.config, ["value"])

    def test_negative_tolerance(self):
        self.config["rtol"] = -1
        with self.assertRaises(ValueError):
            run.compare(self.actual, self.config, ["value"])

    def test_measurement_without_tolerances_exports_errors_but_no_verdict(self):
        self.csv(self.actual, [[0, 3], [1, 4]])
        self.csv(self.reference, [[0, 0], [1, 0]])
        self.config.update(atol=None, rtol=None)
        detail = self.root / "comparison.csv"
        report = run.compare(self.actual, self.config, ["value"], detail)
        self.assertIsNone(report["passed"])
        self.assertIsNone(report["violations"])
        self.assertEqual(report["max_abs_error"], {"value": 4})
        self.assertAlmostEqual(report["rmse"]["value"], math.sqrt(12.5))
        self.assertEqual(report["max_abs_error_time"], {"value": 1})
        with detail.open(encoding="utf-8-sig") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual([r["allowed_error"] for r in rows], ["", ""])
        self.assertEqual([float(r["abs_error"]) for r in rows], [3, 4])

    def test_dense_reference_matches_actual_times_without_interpolation(self):
        self.csv(self.actual, [[0, 1], [0.2 + 1e-15, 3]])
        self.csv(self.reference, [[0, 1], [0.1, 900], [0.2, 3]])
        self.config.update(time_alignment="actual_times")
        report = run.compare(self.actual, self.config, ["value"])
        self.assertTrue(report["passed"])
        self.assertEqual(report["samples"], 2)
        self.assertEqual(report["reference_samples"], 3)
        self.assertEqual(report["unused_reference_samples"], 1)
        self.assertGreater(report["max_time_difference"], 0)

    def test_dense_reference_missing_endpoint_or_interior_point_fails(self):
        self.csv(self.actual, [[0, 1], [0.1, 2], [0.2, 3]])
        self.config.update(time_alignment="actual_times")
        for rows in ([[0, 1], [0.2, 3]], [[0, 1], [0.1, 2]], [[0.1, 2], [0.2, 3]]):
            with self.subTest(rows=rows):
                self.csv(self.reference, rows)
                with self.assertRaisesRegex(ValueError, "缺少本次时间点"):
                    run.compare(self.actual, self.config, ["value"])

    def test_ambiguous_reference_time_fails(self):
        self.csv(self.actual, [[0, 1], [1, 2]])
        self.csv(self.reference, [[0, 1], [5e-11, 100], [1, 2]])
        self.config.update(time_alignment="actual_times")
        with self.assertRaisesRegex(ValueError, "不唯一"):
            run.compare(self.actual, self.config, ["value"])

    def test_select_only_explicit_reference_variables(self):
        self.csv(self.actual, [[0, 1, 100], [1, 2, 200]], ("time", "value", "extra"))
        self.csv(self.reference, [[0, 1], [1, 2]], ("Time(s)", "ref"))
        self.config.update(variables=["value"], columns={"value": "ref"}, time_column="Time(s)")
        report = run.compare(self.actual, self.config, ["value", "extra"])
        self.assertTrue(report["passed"])
        self.assertEqual(report["variables"], ["value"])

    def test_invalid_comparison_options_fail(self):
        for change in ({"atol": None}, {"rtol": True}, {"variables": []},
                       {"variables": ["extra"]}, {"variables": ["value", "value"]},
                       {"time_alignment": "interpolate"}):
            with self.subTest(change=change), self.assertRaises(ValueError):
                run.compare(self.actual, {**self.config, **change}, ["value"])

    def test_reference_hash_mismatch_fails(self):
        self.csv(self.actual, [[0, 1]])
        self.csv(self.reference, [[0, 1]])
        self.config["sha256"] = "0" * 64
        with self.assertRaisesRegex(ValueError, "SHA256"):
            run.compare(self.actual, self.config, ["value"])

    def test_error_or_tolerance_overflow_fails(self):
        self.csv(self.actual, [[0, 1e308]])
        self.csv(self.reference, [[0, -1e308]])
        with self.assertRaisesRegex(ValueError, "误差计算溢出"):
            run.compare(self.actual, self.config, ["value"])
        self.csv(self.reference, [[0, 1e308]])
        self.config["rtol"] = 10
        with self.assertRaisesRegex(ValueError, "容差计算溢出"):
            run.compare(self.actual, self.config, ["value"])

    def test_crash_and_timeout_are_recorded(self):
        case = self.root / "case.json"
        case.write_text("{}", encoding="utf-8")
        helper = self.root / "child.py"
        for body, timeout, expected in (("import os; os._exit(7)", 10, "failed"),
                                         ("import time; time.sleep(20)", 0.2, "timeout")):
            helper.write_text(body, encoding="utf-8")
            with patch.object(run, "__file__", str(helper)):
                result = run.run_one(case, self.root / expected, timeout)
            self.assertEqual(result["status"], expected)
            self.assertTrue((self.root / expected / "result.json").is_file())

    def test_batch_continues_and_does_not_claim_unverified_passed(self):
        cases = self.root / "cases"
        cases.mkdir()
        for name in ("a", "b"):
            (cases / f"{name}.json").write_text(json.dumps({"id": name}), encoding="utf-8")
        with patch.object(run, "ROOT", self.root), patch.object(run, "CASES", cases), \
             patch.object(run.sys, "argv", ["run.py"]), \
             patch.object(run, "run_one", side_effect=[{"status": "failed"}, {"status": "unverified"}]) as execute, \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run.main(), 1)
            self.assertEqual(execute.call_count, 2)
        with patch.object(run, "ROOT", self.root), patch.object(run, "CASES", cases), \
             patch.object(run.sys, "argv", ["run.py", "--case", "b"]), \
             patch.object(run, "run_one", return_value={"status": "unverified"}), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run.main(), 2)
        with patch.object(run, "ROOT", self.root), patch.object(run, "CASES", cases), \
             patch.object(run.sys, "argv", ["run.py", "--case", "b"]), \
             patch.object(run, "run_one", return_value={"status": "compared"}), \
             contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(run.main(), 2)

    def test_fmu_endpoints_checked_before_loading_nova(self):
        config = json.loads((run.CASES / "case1.json").read_text(encoding="utf-8"))
        # Metadata-only check: requires the two supplied FMUs, but does not load their libraries.
        if not (run.ROOT / config["models"]["fmu1"]).is_file():
            self.skipTest("case1 FMUs not installed")
        config["connections"][0][1] = "fmu2::missing_variable"
        with self.assertRaisesRegex(ValueError, "变量不存在"):
            run.validate_case(config)
        config["step_size"] = 0
        with self.assertRaisesRegex(ValueError, "正数"):
            run.validate_case(config)


if __name__ == "__main__":
    unittest.main()
