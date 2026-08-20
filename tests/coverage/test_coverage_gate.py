#!/usr/bin/env python3
"""Focused success and failure checks for coverage_gate.py."""

from __future__ import annotations

import csv
import importlib.util
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("coverage_gate", Path(__file__).with_name("coverage_gate.py"))
assert SPEC is not None and SPEC.loader is not None
coverage = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(coverage)


class CoverageGateTests(unittest.TestCase):
    def setUp(self) -> None:
        with coverage.FLOORS.open(newline="", encoding="utf-8") as stream:
            self.rows = list(csv.DictReader(stream))

    def floor_copy(self, rows: list[dict[str, str]]) -> Path:
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        path = Path(directory.name) / "floors.csv"
        with path.open("w", newline="", encoding="utf-8") as stream:
            writer = csv.DictWriter(stream, fieldnames=coverage.FLOOR_FIELDS, lineterminator="\n")
            writer.writeheader()
            writer.writerows(rows)
        return path

    def test_valid_floors_and_zero_metric_are_accepted(self) -> None:
        floors = coverage.validate_floors(ROOT, coverage.FLOORS)
        results: dict[str, dict[str, set[tuple]]] = {}
        for module, row in floors.items():
            if row["status"] != "measured":
                continue
            lines = {(module, 1, "f", True)}
            branches = set() if row["branches_floor"] == "n/a" else {(module, "f", 0, True)}
            functions = {(module, "f", True)}
            results[module] = {"lines": lines, "branches": branches, "functions": functions}
        # The synthetic 100% observations satisfy every checked-in floor.
        rows = coverage.check_results(floors, results)
        self.assertEqual(len(rows), len(floors))

    def test_each_supported_compiler_has_a_complete_keyed_floor_set(self) -> None:
        gcc = coverage.validate_floors(ROOT, coverage.FLOORS, "gcc-13")
        clang = coverage.validate_floors(ROOT, coverage.FLOORS_CLANG, "clang-18")
        self.assertEqual(set(gcc), set(clang))
        self.assertTrue(all(row["compiler"] == "gcc-13" for row in gcc.values()))
        self.assertTrue(all(row["compiler"] == "clang-18" for row in clang.values()))

    def test_missing_stale_and_duplicate_records_fail(self) -> None:
        missing = self.rows[1:]
        with self.assertRaisesRegex(coverage.CoverageError, "missing floor record"):
            coverage.validate_floors(ROOT, self.floor_copy(missing))
        stale = self.rows + [dict(self.rows[0], module="src/stale.c")]
        with self.assertRaisesRegex(coverage.CoverageError, "stale floor record"):
            coverage.validate_floors(ROOT, self.floor_copy(stale))
        duplicate = self.rows + [dict(self.rows[0])]
        with self.assertRaisesRegex(coverage.CoverageError, "duplicate floor record"):
            coverage.validate_floors(ROOT, self.floor_copy(duplicate))

    def test_metric_regression_fails(self) -> None:
        floors = coverage.validate_floors(ROOT, coverage.FLOORS)
        results: dict[str, dict[str, set[tuple]]] = {}
        first_measured = next(module for module, row in floors.items() if row["status"] == "measured")
        for module, row in floors.items():
            if row["status"] != "measured":
                continue
            results[module] = {
                "lines": {(module, 1, "f", module != first_measured)},
                "branches": (set() if row["branches_floor"] == "n/a"
                             else {(module, "f", 0, True)}),
                "functions": {(module, "f", True)},
            }
        with self.assertRaisesRegex(coverage.CoverageError, "coverage regression"):
            coverage.check_results(floors, results)

    def test_invalid_rationale_status_and_exclusion_policy_fail(self) -> None:
        blank = [dict(row) for row in self.rows]
        blank[0]["rationale"] = " "
        with self.assertRaisesRegex(coverage.CoverageError, "blank field|nonblank rationale"):
            coverage.validate_floors(ROOT, self.floor_copy(blank))
        invalid_status = [dict(row) for row in self.rows]
        invalid_status[0]["status"] = "unavailable"
        with self.assertRaisesRegex(coverage.CoverageError, "invalid status"):
            coverage.validate_floors(ROOT, self.floor_copy(invalid_status))
        invalid_exclusion = [dict(row) for row in self.rows]
        libtelnet = next(row for row in invalid_exclusion if row["module"] == coverage.THIRD_PARTY)
        libtelnet["status"] = "measured"
        libtelnet["lines_floor"] = "20.33"
        libtelnet["branches_floor"] = "18.05"
        libtelnet["functions_floor"] = "35.14"
        with self.assertRaisesRegex(coverage.CoverageError, "must have excluded_third_party"):
            coverage.validate_floors(ROOT, self.floor_copy(invalid_exclusion))
        invalid_compiler = [dict(row) for row in self.rows]
        invalid_compiler[0]["compiler"] = "clang"
        with self.assertRaisesRegex(coverage.CoverageError, "compiler key"):
            coverage.validate_floors(ROOT, self.floor_copy(invalid_compiler), "gcc-13")

    def test_inventory_failure_is_gate_failure(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            audit = root / "tests/inventory/audit.py"
            audit.parent.mkdir(parents=True)
            audit.write_text("raise SystemExit(1)\n", encoding="utf-8")
            with self.assertRaisesRegex(coverage.CoverageError, "coverage command failed"):
                coverage.run_inventory(root, root / "libsinshared.a")

    def test_compiler_vendor_uses_version_output_not_alias(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            compiler = Path(directory) / "cc"
            compiler.write_text("#!/bin/sh\nprintf '%s\\n' 'Ubuntu clang version 18.1.3'\n", encoding="utf-8")
            compiler.chmod(0o755)
            self.assertEqual(coverage.compiler_identity(str(compiler)), ("clang", 18))
            self.assertEqual(coverage.compiler_vendor(str(compiler)), "clang")
            unknown = Path(directory) / "mystery-cc"
            unknown.write_text("#!/bin/sh\nprintf '%s\\n' 'mystery compiler 1.0'\n", encoding="utf-8")
            unknown.chmod(0o755)
            with self.assertRaisesRegex(coverage.CoverageError, "did not identify GCC or Clang"):
                coverage.compiler_vendor(str(unknown))

    def test_unreviewed_compiler_major_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            compiler = Path(directory) / "gcc"
            compiler.write_text("#!/bin/sh\nprintf '%s\\n' 'gcc (Fake) 14.2.0'\n", encoding="utf-8")
            compiler.chmod(0o755)
            vendor, major = coverage.compiler_identity(str(compiler))
            self.assertEqual((vendor, major), ("gcc", 14))
            with self.assertRaisesRegex(coverage.CoverageError, "no reviewed coverage baseline.*gcc-14"):
                coverage.reviewed_floors(f"{vendor}-{major}")

    def test_llvm_tool_override_must_match_compiler_major(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tool = Path(directory) / "llvm-cov-override"
            tool.write_text("#!/bin/sh\nprintf '%s\\n' 'LLVM version 17.0.6'\n", encoding="utf-8")
            tool.chmod(0o755)
            with self.assertRaisesRegex(coverage.CoverageError, "reports major 17.*clang-18"):
                coverage.find_llvm_tool(str(tool), "llvm-cov", 18)

    def test_gcov_override_must_match_compiler_major(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            tool = Path(directory) / "gcov-override"
            tool.write_text("#!/bin/sh\nprintf '%s\\n' 'gcov (Fake) 12.4.0'\n", encoding="utf-8")
            tool.chmod(0o755)
            with self.assertRaisesRegex(coverage.CoverageError, "reports major 12.*gcc-13"):
                coverage.find_gcov(str(tool), 13)

    def test_gcov_discovery_prefers_matching_suffix_over_wrong_generic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            generic = directory_path / "gcov"
            matching = directory_path / "gcov-13"
            generic.write_text("#!/bin/sh\nprintf '%s\\n' 'gcov (Fake) 12.4.0'\n", encoding="utf-8")
            matching.write_text("#!/bin/sh\nprintf '%s\\n' 'gcov (Fake) 13.3.0'\n", encoding="utf-8")
            generic.chmod(0o755)
            matching.chmod(0o755)
            with mock.patch.dict(os.environ, {"PATH": directory}, clear=False):
                self.assertEqual(coverage.find_gcov(None, 13), str(matching))

    def test_llvm_discovery_prefers_matching_suffix_over_wrong_generic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            directory_path = Path(directory)
            generic = directory_path / "llvm-cov"
            matching = directory_path / "llvm-cov-18"
            generic.write_text("#!/bin/sh\nprintf '%s\\n' 'LLVM version 17.0.6'\n", encoding="utf-8")
            matching.write_text("#!/bin/sh\nprintf '%s\\n' 'LLVM version 18.1.3'\n", encoding="utf-8")
            generic.chmod(0o755)
            matching.chmod(0o755)
            with mock.patch.dict(os.environ, {"PATH": directory}, clear=False):
                self.assertEqual(coverage.find_llvm_tool(None, "llvm-cov", 18), str(matching))

    def test_llvm_18_export_schema_uses_native_summaries(self) -> None:
        module = "src/common/error.c"
        payload = {
            "data": [{
                "files": [{"filename": str(ROOT / module),
                           "segments": [[1, 1, 0, True, True, False]],
                           "summary": {"lines": {"count": 41, "covered": 40},
                                       "branches": {"count": 0, "covered": 0},
                                       "functions": {"count": 1, "covered": 1}}}],
            }],
        }
        summaries = coverage.parse_llvm_summaries(payload, ROOT, [module])
        self.assertEqual(summaries[module]["lines"], {"covered": 40, "total": 41})
        self.assertEqual(summaries[module]["branches"], {"covered": 0, "total": 0})
        self.assertEqual(summaries[module]["functions"], {"covered": 1, "total": 1})
        duplicate = {"data": [{"files": payload["data"][0]["files"] * 2}]}
        with self.assertRaisesRegex(coverage.CoverageError, "duplicate LLVM coverage summary"):
            coverage.parse_llvm_summaries(duplicate, ROOT, [module])
        with self.assertRaisesRegex(coverage.CoverageError, "omit authored module"):
            coverage.parse_llvm_summaries({"data": [{"files": []}]}, ROOT, [module])

    def test_no_instrumentable_module_requires_zero_native_totals(self) -> None:
        static = coverage.NO_INSTRUMENTABLE
        empty_gcov = coverage.parse_gcov_payload(
            {"files": []}, ROOT, static, allow_empty=True)
        self.assertEqual(empty_gcov, {"lines": set(), "branches": set(), "functions": set()})
        with self.assertRaisesRegex(coverage.CoverageError, "unexpectedly has gcov observations"):
            coverage.parse_gcov_payload(
                {"files": [{"file": str(ROOT / static),
                            "lines": [{"line_number": 1, "count": 1}]}]},
                ROOT, static, allow_empty=True)
        zero = {"data": [{"files": [{"filename": str(ROOT / static),
            "summary": {"lines": {"count": 0, "covered": 0},
                        "branches": {"count": 0, "covered": 0},
                        "functions": {"count": 0, "covered": 0}}}]}]}
        summaries = coverage.parse_llvm_summaries(zero, ROOT, [], [static])
        self.assertEqual(summaries[static]["lines"], {"covered": 0, "total": 0})
        violating = {"data": [{"files": [{"filename": str(ROOT / static),
            "summary": {"lines": {"count": 1, "covered": 0},
                        "branches": {"count": 0, "covered": 0},
                        "functions": {"count": 0, "covered": 0}}}]}]}
        with self.assertRaisesRegex(coverage.CoverageError, "unexpectedly has LLVM lines"):
            coverage.parse_llvm_summaries(violating, ROOT, [], [static])

    def test_coordinate_merge_uses_or_semantics_without_duplicates(self) -> None:
        first = {("src/net/network.c", 10, "handle", False),
                 ("src/net/network.c", 11, "handle", True)}
        second = {("src/net/network.c", 10, "handle", True),
                  ("src/net/network.c", 12, "handle", False)}
        self.assertEqual(
            coverage.merge_observations(first, second),
            {("src/net/network.c", 10, "handle", True),
             ("src/net/network.c", 11, "handle", True),
             ("src/net/network.c", 12, "handle", False)},
        )

    def test_regression_writes_detailed_current_results(self) -> None:
        floors = coverage.validate_floors(ROOT, coverage.FLOORS)
        first = next(module for module, row in floors.items() if row["status"] == "measured")
        results = {}
        for module, row in floors.items():
            if row["status"] != "measured":
                continue
            results[module] = {
                "lines": {(module, 1, "f", module != first)},
                "branches": set() if row["branches_floor"] == "n/a" else {(module, "f", 0, True)},
                "functions": {(module, "f", True)},
            }
        rows, failures = coverage.evaluate_results(floors, results)
        self.assertTrue(failures)
        with tempfile.TemporaryDirectory() as directory:
            coverage.write_reports(Path(directory), rows)
            report = (Path(directory) / "coverage.csv").read_text(encoding="utf-8")
            self.assertIn("FAIL", report)
            self.assertGreaterEqual(report.count("\n"), len(floors) + 1)


if __name__ == "__main__":
    unittest.main()
