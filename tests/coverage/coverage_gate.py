#!/usr/bin/env python3
"""Collect and audit non-regressing coverage for authored Sinistra modules.

The checked-in floor file is intentionally separate from the historical
snapshot.  This tool never writes it: changing a floor is a manual review
operation in the checked-in CSV, including its rationale.
"""

from __future__ import annotations

import argparse
import csv
import gzip
import json
import os
from pathlib import Path
import re
import shutil
import shlex
import subprocess
import sys
import tempfile
from decimal import Decimal, InvalidOperation, ROUND_HALF_UP
from typing import Iterable


ROOT = Path(__file__).resolve().parents[2]
FLOORS = ROOT / "tests/baseline/coverage_floors.csv"
FLOORS_CLANG = ROOT / "tests/baseline/coverage_floors_clang.csv"
FLOOR_FIELDS = (
    "module", "owner", "status", "lines_floor", "branches_floor",
    "functions_floor", "rationale", "compiler",
)
REVIEWED_FLOORS = {
    "gcc-13": FLOORS,
    "clang-18": FLOORS_CLANG,
}
STATUSES = {"measured", "no_instrumentable_code", "excluded_third_party"}
OWNERS = {
    "common", "bytecode", "compiler", "runtime", "itemstore", "libcall",
    "net", "executable",
}
NO_INSTRUMENTABLE = "src/libcall/libcall_table.c"
NO_INSTRUMENTABLE_MODULES = {
    NO_INSTRUMENTABLE,
}
THIRD_PARTY = "src/net/libtelnet.c"
PERCENT_QUANTUM = Decimal("0.01")


class CoverageError(Exception):
    """A user-facing coverage gate error."""


def fail(message: str) -> None:
    raise CoverageError(message)


def compiler_identity(command: str) -> tuple[str, int]:
    """Identify compiler vendor and major from version output, not its alias."""
    argv = shlex.split(command)
    if not argv:
        fail("coverage compiler command is empty")
    try:
        result = subprocess.run(argv + ["--version"], check=True,
                                capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot identify coverage compiler {command!r}: {error}")
    version = (result.stdout + result.stderr).lower()
    if "clang" in version:
        match = re.search(r"clang(?: version)?\s+(\d+)", version)
        if not match:
            match = re.search(r"\b(\d+)(?:\.\d+){1,2}\b", version)
        if match:
            return "clang", int(match.group(1))
    if ("gcc" in version or "gnu compiler collection" in version
            or "free software foundation" in version):
        match = re.search(r"gcc[^\n]*?\b(\d+)(?:\.\d+)+", version)
        if not match:
            match = re.search(r"gnu compiler collection[^\n]*?\b(\d+)(?:\.\d+)+", version)
        if not match:
            match = re.search(r"\b(\d+)(?:\.\d+){1,2}\b", version)
        if match:
            return "gcc", int(match.group(1))
    if ("clang" in version or "gcc" in version or "gnu compiler collection" in version
            or "free software foundation" in version):
        fail(f"unsupported coverage compiler {command!r}; version output has no major version")
    fail(f"unsupported coverage compiler {command!r}; --version did not identify GCC or Clang")


def compiler_vendor(command: str) -> str:
    """Compatibility helper returning only the selected compiler vendor."""
    return compiler_identity(command)[0]


def reviewed_floors(toolchain: str) -> Path:
    floors = REVIEWED_FLOORS.get(toolchain)
    if floors is None:
        fail(f"no reviewed coverage baseline for compiler toolchain {toolchain}")
    return floors


def llvm_tool_major(path: str) -> int:
    try:
        result = subprocess.run([path, "--version"], check=True,
                                capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot identify LLVM tool {path!r}: {error}")
    match = re.search(r"\b(\d+)(?:\.\d+)+\b", result.stdout + result.stderr)
    if not match:
        fail(f"LLVM tool {path!r} reported no major version")
    return int(match.group(1))


def find_llvm_tool(requested: str | None, prefix: str, major: int) -> str:
    """Find and verify an LLVM tool matching the selected compiler major."""
    if requested:
        path = requested if "/" in requested else shutil.which(requested)
        if not path:
            fail(f"missing requested LLVM tool {requested!r} for clang-{major}")
        actual = llvm_tool_major(path)
        if actual != major:
            fail(f"LLVM tool {path!r} reports major {actual}, but compiler toolchain is clang-{major}")
        return path
    candidates = (f"{prefix}-{major}", prefix)
    for candidate in candidates:
        path = shutil.which(candidate)
        if not path:
            continue
        actual = llvm_tool_major(path)
        if actual == major:
            return path
        if candidate == prefix:
            fail(f"LLVM tool {path!r} reports major {actual}, but compiler toolchain is clang-{major}")
    fail(f"missing LLVM tool matching clang-{major}; tried {', '.join(candidates)}")


def gcov_tool_major(path: str) -> int:
    try:
        result = subprocess.run([path, "--version"], check=True,
                                capture_output=True, text=True)
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot identify gcov tool {path!r}: {error}")
    match = re.search(r"\b(\d+)(?:\.\d+)+\b", result.stdout + result.stderr)
    if not match:
        fail(f"gcov tool {path!r} reported no major version")
    return int(match.group(1))


def find_gcov(requested: str | None, major: int) -> str:
    """Find and verify a gcov reporter matching the selected GCC major."""
    if requested:
        path = requested if "/" in requested else shutil.which(requested)
        if not path:
            fail(f"missing requested gcov tool {requested!r} for gcc-{major}")
        actual = gcov_tool_major(path)
        if actual != major:
            fail(f"gcov tool {path!r} reports major {actual}, but compiler toolchain is gcc-{major}")
        return path
    candidates = (f"gcov-{major}", "gcov")
    for candidate in candidates:
        path = shutil.which(candidate)
        if not path:
            continue
        actual = gcov_tool_major(path)
        if actual == major:
            return path
        if candidate == "gcov":
            fail(f"gcov tool {path!r} reports major {actual}, but compiler toolchain is gcc-{major}")
    fail(f"missing gcov tool matching gcc-{major}; tried {', '.join(candidates)}")


def owner_for(module: str) -> str:
    parts = Path(module).parts
    if len(parts) < 2 or parts[0] != "src":
        fail(f"module is not an authored src path: {module}")
    if len(parts) == 2:
        return "executable"
    owner = parts[1]
    if owner not in OWNERS:
        fail(f"module has unknown owner {module}")
    return owner


def authored_modules(root: Path) -> list[str]:
    source_root = root / "src"
    if not source_root.is_dir():
        fail(f"missing source directory: {source_root}")
    return sorted(
        str(path.relative_to(root))
        for path in source_root.rglob("*.c")
        if path.is_file() and str(path.relative_to(root)) not in {"src/parser.c", "src/lexer.c"}
        and path.relative_to(root).parts[:2] != ("src", "generated")
    )


def read_csv(path: Path) -> list[dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if tuple(reader.fieldnames or ()) != FLOOR_FIELDS:
                fail(f"{path}: wrong columns (expected {','.join(FLOOR_FIELDS)})")
            rows = list(reader)
    except OSError as error:
        fail(f"cannot read {path}: {error}")
    for line, row in enumerate(rows, start=2):
        if set(row) != set(FLOOR_FIELDS) or any(value is None for value in row.values()):
            fail(f"{path}: row {line} has missing or extra cells")
        if any(not row[field].strip() for field in FLOOR_FIELDS):
            fail(f"{path}: row {line} has a blank field")
    return rows


def parse_floor(value: str, field: str, row_number: int) -> Decimal | None:
    if value == "n/a":
        return None
    try:
        result = Decimal(value)
    except InvalidOperation:
        fail(f"floor row {row_number} has malformed {field}: {value!r}")
    if result < 0 or result > 100:
        fail(f"floor row {row_number} has out-of-range {field}: {value!r}")
    if result.quantize(PERCENT_QUANTUM) != result:
        fail(f"floor row {row_number} has more than two decimal places in {field}")
    return result


def validate_floors(root: Path, path: Path = FLOORS,
                    expected_compiler: str | None = None) -> dict[str, dict[str, str]]:
    rows = read_csv(path)
    compiler = expected_compiler or (rows[0]["compiler"] if rows else "")
    if compiler not in REVIEWED_FLOORS:
        fail(f"floor file {path} has unsupported toolchain key {compiler!r}")
    expected = set(authored_modules(root))
    seen: set[str] = set()
    floors: dict[str, dict[str, str]] = {}
    for number, row in enumerate(rows, start=2):
        module = row["module"]
        if row["compiler"] != compiler:
            fail(f"floor row {number} has compiler key {row['compiler']!r}; expected {compiler!r}")
        if module in seen:
            fail(f"duplicate floor record for {module}")
        seen.add(module)
        if module not in expected:
            fail(f"stale floor record for {module}")
        if row["owner"] != owner_for(module):
            fail(f"floor row {number} has wrong owner for {module}")
        status = row["status"]
        if status not in STATUSES:
            fail(f"floor row {number} has invalid status {status!r}")
        if status == "no_instrumentable_code" and module not in NO_INSTRUMENTABLE_MODULES:
            fail(f"no-instrumentable status is only valid for {sorted(NO_INSTRUMENTABLE_MODULES)}")
        if status == "excluded_third_party" and module != THIRD_PARTY:
            fail(f"third-party exclusion is only valid for {THIRD_PARTY}")
        if module in NO_INSTRUMENTABLE_MODULES and status != "no_instrumentable_code":
            fail(f"{module} must have no_instrumentable_code status")
        if module == THIRD_PARTY and status != "excluded_third_party":
            fail(f"{THIRD_PARTY} must have excluded_third_party status")
        if len(row["rationale"].strip()) < 8:
            fail(f"floor row {number} needs a nonblank rationale")
        values = [parse_floor(row[field], field, number)
                  for field in ("lines_floor", "branches_floor", "functions_floor")]
        if status != "measured" and any(value is not None for value in values):
            fail(f"{status} floor row {number} must use n/a metric floors")
        floors[module] = row
    missing = expected - seen
    if missing:
        fail(f"missing floor record for {sorted(missing)[0]}")
    if seen != expected:
        fail("floor module set does not match authored src/**/*.c")
    return floors


def percent(covered: int, total: int) -> Decimal | None:
    if total == 0:
        return None
    return (Decimal(100) * Decimal(covered) / Decimal(total)).quantize(
        PERCENT_QUANTUM, rounding=ROUND_HALF_UP)


def metric_summary(lines: set[tuple], branches: set[tuple], functions: set[tuple]) -> dict[str, int | str]:
    values = {
        "lines_covered": sum(1 for item in lines if item[-1]),
        "lines_total": len(lines),
        "branches_covered": sum(1 for item in branches if item[-1]),
        "branches_total": len(branches),
        "functions_covered": sum(1 for item in functions if item[-1]),
        "functions_total": len(functions),
    }
    for metric in ("lines", "branches", "functions"):
        coverage = percent(values[f"{metric}_covered"], values[f"{metric}_total"])
        values[f"{metric}_percent"] = "n/a" if coverage is None else f"{coverage:.2f}"
    return values


def merge_observations(first: set[tuple], second: set[tuple]) -> set[tuple]:
    """Union coordinates while treating a hit in either report as covered."""
    merged: dict[tuple, bool] = {}
    for item in (*first, *second):
        coordinate, covered = item[:-1], bool(item[-1])
        merged[coordinate] = merged.get(coordinate, False) or covered
    return {(*coordinate, covered) for coordinate, covered in merged.items()}


def source_key(path: str, root: Path) -> str | None:
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = root / candidate
    try:
        return str(candidate.resolve().relative_to(root.resolve()))
    except ValueError:
        return None


def parse_gcov_payload(payload: dict, root: Path, module: str,
                       allow_empty: bool = False) -> dict[str, set[tuple]]:
    lines: set[tuple] = set()
    branches: set[tuple] = set()
    functions: set[tuple] = set()
    branch_ordinals: dict[str, int] = {}
    for source in payload.get("files", []):
        if source_key(str(source.get("file", "")), root) != module:
            continue
        for item in source.get("lines", []):
            key = (module, int(item["line_number"]), str(item.get("function_name", "")))
            lines.add((*key, int(item.get("count", 0)) > 0))
            function_name = str(item.get("function_name", ""))
            for branch in item.get("branches", []):
                ordinal = branch_ordinals.get(function_name, 0)
                branch_ordinals[function_name] = ordinal + 1
                branch_key = (module, function_name, ordinal)
                branches.add((*branch_key, int(branch.get("count", 0)) > 0))
        for item in source.get("functions", []):
            name = str(item.get("name", item.get("demangled_name", "")))
            functions.add((module, name, int(item.get("execution_count", 0)) > 0))
    if not lines and not functions and not branches and not allow_empty:
        fail(f"gcov JSON has no observations for {module}")
    if module in NO_INSTRUMENTABLE_MODULES and (lines or branches or functions):
        fail(f"{module} unexpectedly has gcov observations")
    return {"lines": lines, "branches": branches, "functions": functions}


def _read_gcov_payload(path: Path) -> dict:
    try:
        with gzip.open(path, "rt", encoding="utf-8") as stream:
            return json.load(stream)
    except (OSError, json.JSONDecodeError) as error:
        fail(f"cannot read gcov JSON {path}: {error}")


def _select_gcov_report(candidates: Iterable[Path], root: Path, module: str,
                        allow_empty: bool = False) -> dict[str, set[tuple]]:
    matches: list[tuple[Path, dict]] = []
    for candidate in candidates:
        payload = _read_gcov_payload(candidate)
        if any(source_key(str(source.get("file", "")), root) == module
               for source in payload.get("files", [])):
            matches.append((candidate, payload))
    if not matches:
        if allow_empty:
            return {"lines": set(), "branches": set(), "functions": set()}
        fail(f"gcov produced no JSON report containing {module}")
    if len(matches) != 1:
        names = ", ".join(str(path) for path, _ in matches)
        fail(f"gcov produced ambiguous JSON reports containing {module}: {names}")
    return parse_gcov_payload(matches[0][1], root, module, allow_empty)


def parse_gcov_json(path: Path, root: Path, module: str,
                    allow_empty: bool = False) -> dict[str, set[tuple]]:
    return _select_gcov_report([path], root, module, allow_empty)


def run_command(command: list[str], cwd: Path, env: dict[str, str] | None = None) -> None:
    try:
        result = subprocess.run(command, cwd=cwd, env=env, text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    except OSError as error:
        fail(f"cannot run {' '.join(command)}: {error}")
    if result.returncode != 0:
        output = result.stdout.strip()
        fail(f"coverage command failed ({result.returncode}): {' '.join(command)}\n{output}")


def _collect_gcov_module(gcov: str, root: Path, coverage_input: Path,
                         source: Path, module: str, report_dir: Path,
                         no_instrumentable: set[str]) -> dict[str, set[tuple]]:
    if not coverage_input.exists():
        fail(f"missing instrumented coverage input for {module}: {coverage_input}")
    raw_root = report_dir / "raw"
    raw_root.mkdir(parents=True, exist_ok=True)
    invocation_dir = Path(tempfile.mkdtemp(prefix="gcov-", dir=raw_root))
    run_command([gcov, "-b", "-f", "--json-format", "-o", str(coverage_input), str(source)],
                invocation_dir)
    created = sorted(invocation_dir.glob("*.gcov.json.gz"))
    if not created:
        if module in no_instrumentable:
            return {"lines": set(), "branches": set(), "functions": set()}
        fail(f"gcov produced no new JSON report for {module}")
    return _select_gcov_report(created, root, module, module in no_instrumentable)


def _merge_gcov_modules(first: dict[str, set[tuple]],
                        second: dict[str, set[tuple]]) -> dict[str, set[tuple]]:
    return {metric: merge_observations(first[metric], second[metric])
            for metric in ("lines", "branches", "functions")}


def merge_gcc_profiles(profile_root: Path, build_dir: Path, report_dir: Path,
                       gcov_tool: str) -> None:
    """Merge per-process gcda trees and publish them beside their gcno files."""
    profiles = sorted(path for path in profile_root.glob("gcov-*")
                      if path.is_dir())
    if not profiles:
        fail(f"no isolated GCC profiles under {profile_root}")
    work = report_dir / "gcov-merge"
    shutil.rmtree(work, ignore_errors=True)
    work.mkdir(parents=True)
    current = profiles
    level = 0
    while len(current) > 1:
        next_level: list[Path] = []
        level_dir = work / f"level-{level}"
        level_dir.mkdir()
        for index in range(0, len(current), 2):
            if index + 1 == len(current):
                next_level.append(current[index])
                continue
            output = level_dir / f"profile-{index // 2}"
            run_command([gcov_tool, "merge", str(current[index]),
                         str(current[index + 1]), "-o", str(output)],
                        report_dir)
            next_level.append(output)
        current = next_level
        level += 1
    relative_build = Path(*build_dir.resolve().parts[1:])
    merged_build = current[0] / relative_build
    if not merged_build.is_dir():
        fail(f"merged GCC profile omits build tree {build_dir.resolve()}")
    copied = 0
    for source in merged_build.rglob("*.gcda"):
        target = build_dir / source.relative_to(merged_build)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        copied += 1
    if copied == 0:
        fail(f"merged GCC profile contains no gcda files for {build_dir.resolve()}")


def collect_gcc(root: Path, build_dir: Path, modules: Iterable[str], report_dir: Path,
                major: int, gcov: str | None = None,
                no_instrumentable: Iterable[str] = (),
                network_adapter: Path | None = None) -> dict[str, dict[str, set[tuple]]]:
    gcov = find_gcov(gcov, major)
    no_instrumentable_set = set(no_instrumentable)
    results: dict[str, dict[str, set[tuple]]] = {}
    module_list = list(modules)
    if network_adapter is not None and "src/net/network.c" in module_list:
        if not network_adapter.is_file():
            fail(f"missing network adapter coverage object: {network_adapter}")
    for module in module_list:
        source = root / module
        object_dir = build_dir / source.relative_to(root).parent.relative_to("src")
        if source.relative_to(root).parent == Path("src"):
            object_dir = build_dir
        results[module] = _collect_gcov_module(
            gcov, root, object_dir, source, module, report_dir, no_instrumentable_set)
        if module == "src/net/network.c" and network_adapter is not None:
            adapter_source = root / "tests/rewrite/group7_adapter_network.c"
            adapter_gcno = network_adapter.with_suffix(".gcno")
            if not adapter_source.is_file():
                fail(f"missing network adapter source: {adapter_source}")
            if not adapter_gcno.is_file():
                fail(f"missing network adapter coverage note: {adapter_gcno}")
            adapter_result = _collect_gcov_module(
                gcov, root, adapter_gcno, adapter_source, module,
                report_dir, no_instrumentable_set)
            results[module] = _merge_gcov_modules(results[module], adapter_result)
    return results


def collect_clang(root: Path, build_dir: Path, modules: Iterable[str], report_dir: Path,
                  major: int, llvm_cov: str | None = None,
                  llvm_profdata: str | None = None,
                  no_instrumentable: Iterable[str] = ()) -> dict[str, dict[str, dict[str, int]]]:
    """Collect Clang's exported source coordinates from the gate binaries."""
    llvm_cov = find_llvm_tool(llvm_cov, "llvm-cov", major)
    llvm_profdata = find_llvm_tool(llvm_profdata, "llvm-profdata", major)
    profiles = sorted((build_dir / "coverage-data").glob("*.profraw"))
    if not profiles:
        fail(f"no Clang profile data under {build_dir / 'coverage-data'}")
    profile = report_dir / "coverage.profdata"
    run_command([llvm_profdata, "merge", "-sparse", "-o", str(profile),
                 *[str(path) for path in profiles]], report_dir)
    binaries = [build_dir / "bin" / name
                for name in ("scomp", "sdiss", "sin", "sconv")]
    binaries.extend(sorted((build_dir / "tests/framework").glob("*")))
    binaries.extend(sorted((build_dir / "tests/conformance").glob("*")))
    binaries.extend(sorted((build_dir / "tests/rewrite").glob("**/test_*")))
    binaries = [binary for binary in binaries if binary.is_file()]
    if not binaries:
        fail("Clang coverage export has no gate binaries")
    try:
        result = subprocess.run(
            [llvm_cov, "export", "-format=text", f"-instr-profile={profile}",
             str(binaries[0]), *[f"-object={binary}" for binary in binaries[1:]]],
            cwd=report_dir, check=True, capture_output=True, text=True)
        payload = json.loads(result.stdout)
    except (OSError, subprocess.CalledProcessError, json.JSONDecodeError) as error:
        fail(f"cannot export combined Clang coverage: {error}")
    return parse_llvm_summaries(payload, root, modules, no_instrumentable)


def parse_llvm_summaries(payload: dict, root: Path, modules: Iterable[str],
                         no_instrumentable: Iterable[str] = ()) -> dict[str, dict[str, dict[str, int]]]:
    """Read one LLVM export's native per-file summary counts.

    LLVM 18 puts the authoritative line/branch/function totals under each
    `data[].files[].summary`; segment coordinates are not line observations.
    """
    no_instrumentable_set = set(no_instrumentable)
    expected = set(modules) | no_instrumentable_set
    summaries: dict[str, dict[str, dict[str, int]]] = {}
    for entry in payload.get("data", []):
        for source in entry.get("files", []):
            module = source_key(str(source.get("filename", "")), root)
            if module not in expected:
                continue
            if module in summaries:
                fail(f"duplicate LLVM coverage summary for {module}")
            summary = source.get("summary")
            if module in no_instrumentable_set:
                if not isinstance(summary, dict):
                    fail(f"LLVM coverage summary is missing for {module}")
                for metric in ("lines", "branches", "functions"):
                    values = summary.get(metric)
                    if (not isinstance(values, dict)
                            or int(values.get("covered", 0)) > 0
                            or int(values.get("count", 0)) > 0):
                        fail(f"{module} unexpectedly has LLVM {metric} observations")
                summaries[module] = {metric: {"covered": 0, "total": 0}
                                     for metric in ("lines", "branches", "functions")}
                continue
            if not isinstance(summary, dict):
                fail(f"LLVM coverage summary is missing for {module}")
            current: dict[str, dict[str, int]] = {}
            for metric in ("lines", "branches", "functions"):
                values = summary.get(metric)
                if not isinstance(values, dict) or "covered" not in values or "count" not in values:
                    fail(f"LLVM coverage summary is missing {metric} for {module}")
                current[metric] = {"covered": int(values["covered"]),
                                   "total": int(values["count"])}
            summaries[module] = current
    missing = set(modules) - set(summaries)
    if missing:
        fail(f"LLVM coverage summaries omit authored module {sorted(missing)[0]}")
    return summaries


def evaluate_results(floors: dict[str, dict[str, str]], results: dict[str, dict]) -> tuple[list[dict[str, str]], list[str]]:
    rows: list[dict[str, str]] = []
    failures: list[str] = []
    for module in sorted(floors):
        floor = floors[module]
        status = floor["status"]
        if status != "measured":
            rows.append({"module": module, "owner": floor["owner"], "status": status,
                         "lines_covered": "0", "lines_total": "0", "lines_percent": "n/a",
                         "branches_covered": "0", "branches_total": "0", "branches_percent": "n/a",
                         "functions_covered": "0", "functions_total": "0", "functions_percent": "n/a",
                         "result": "EXCLUDED"})
            continue
        if module not in results:
            failures.append(f"missing measured result for {module}")
            continue
        if isinstance(results[module]["lines"], dict):
            summary = {}
            for metric in ("lines", "branches", "functions"):
                values = results[module][metric]
                summary[f"{metric}_covered"] = values["covered"]
                summary[f"{metric}_total"] = values["total"]
                coverage = percent(values["covered"], values["total"])
                summary[f"{metric}_percent"] = "n/a" if coverage is None else f"{coverage:.2f}"
        else:
            summary = metric_summary(results[module]["lines"], results[module]["branches"], results[module]["functions"])
        row = {"module": module, "owner": floor["owner"], "status": status,
               **{field: str(value) for field, value in summary.items()}, "result": "PASS"}
        for metric in ("lines", "branches", "functions"):
            current = summary[f"{metric}_percent"]
            expected = floor[f"{metric}_floor"]
            if current == "n/a" and expected != "n/a":
                failures.append(f"{module} has zero {metric} observations but its floor is {expected}%")
            elif current != "n/a" and expected == "n/a":
                failures.append(f"{module} has {metric} observations but its floor is n/a")
            elif current != "n/a" and Decimal(current) < Decimal(expected):
                failures.append(f"{module} {metric} coverage {current}% is below floor {expected}%")
        if any(message.startswith(f"{module} ") for message in failures):
            row["result"] = "FAIL"
        rows.append(row)
    return rows, failures


def check_results(floors: dict[str, dict[str, str]], results: dict[str, dict]) -> list[dict[str, str]]:
    """Validate results and raise while retaining a testable public helper."""
    rows, failures = evaluate_results(floors, results)
    if failures:
        fail("coverage regression:\n" + "\n".join(f"- {message}" for message in failures))
    return rows


def write_reports(report_dir: Path, rows: list[dict[str, str]]) -> None:
    report_dir.mkdir(parents=True, exist_ok=True)
    fields = ("module", "owner", "status", "lines_covered", "lines_total", "lines_percent",
              "branches_covered", "branches_total", "branches_percent", "functions_covered",
              "functions_total", "functions_percent", "result")
    csv_path = report_dir / "coverage.csv"
    with csv_path.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    with (report_dir / "coverage.txt").open("w", encoding="utf-8") as stream:
        stream.write("Sinistra coverage gate (percentage floors)\n\n")
        for row in rows:
            stream.write(f"{row['result']:8} {row['module']:45} "
                         f"lines {row['lines_percent']:>6} ({row['lines_covered']}/{row['lines_total']}) "
                         f"branches {row['branches_percent']:>6} ({row['branches_covered']}/{row['branches_total']}) "
                         f"functions {row['functions_percent']:>6} ({row['functions_covered']}/{row['functions_total']})\n")


def write_failure_report(report_dir: Path, error: CoverageError) -> None:
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / "coverage-error.txt").write_text(
        f"Sinistra coverage gate failed\n\n{error}\n", encoding="utf-8")


def run_inventory(root: Path, archive: Path) -> None:
    audit = root / "tests/inventory/audit.py"
    run_command([sys.executable, str(audit), "--archive", str(archive)], root,
                {**os.environ, "PYTHONDONTWRITEBYTECODE": "1"})


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT)
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="active obj/<build>-<compiler> directory")
    parser.add_argument("--floors", type=Path)
    parser.add_argument("--compiler", default="gcc", help="selected compiler (gcc or clang)")
    parser.add_argument("--gcov")
    parser.add_argument("--gcov-tool")
    parser.add_argument("--gcov-profile-root", type=Path)
    parser.add_argument("--llvm-cov")
    parser.add_argument("--llvm-profdata")
    parser.add_argument("--archive", type=Path)
    parser.add_argument("--skip-inventory", action="store_true")
    args = parser.parse_args()
    root = args.root.resolve()
    build_dir = args.build_dir if args.build_dir.is_absolute() else root / args.build_dir
    report_dir = build_dir / "coverage"
    try:
        vendor, major = compiler_identity(args.compiler)
        toolchain = f"{vendor}-{major}"
        default_floors = reviewed_floors(toolchain)
        selected_floors = args.floors or default_floors
        floors_path = selected_floors if selected_floors.is_absolute() else root / selected_floors
        floors = validate_floors(root, floors_path, toolchain)
        report_dir.mkdir(parents=True, exist_ok=True)
        measured = [module for module, row in floors.items() if row["status"] == "measured"]
        no_instrumentable = [module for module, row in floors.items()
                             if row["status"] == "no_instrumentable_code"]
        if vendor == "clang":
            results = collect_clang(root, build_dir, measured, report_dir,
                                    major, args.llvm_cov, args.llvm_profdata,
                                    no_instrumentable)
        else:
            if args.gcov_profile_root:
                profile_root = (args.gcov_profile_root if
                                args.gcov_profile_root.is_absolute() else
                                root / args.gcov_profile_root)
                gcov_tool = args.gcov_tool or f"gcov-tool-{major}"
                merge_gcc_profiles(profile_root, build_dir, report_dir,
                                   gcov_tool)
            network_adapter = build_dir / "tests/objects/tests/rewrite/group7_adapter_network.o"
            results = collect_gcc(root, build_dir, measured + no_instrumentable,
                                  report_dir, major, args.gcov, no_instrumentable,
                                  network_adapter)
        rows, failures = evaluate_results(floors, results)
        write_reports(report_dir, rows)
        if failures:
            fail("coverage regression:\n" + "\n".join(f"- {message}" for message in failures))
        if not args.skip_inventory:
            archive = args.archive
            if archive is None:
                archive = root / "lib" / f"coverage-{Path(args.compiler).name}" / "libsinshared.a"
            if not archive.is_absolute():
                archive = root / archive
            run_inventory(root, archive)
        print(f"coverage gate: PASS ({len(rows)} module records); reports={report_dir}")
        return 0
    except CoverageError as error:
        write_failure_report(report_dir, error)
        print(f"coverage gate: ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
