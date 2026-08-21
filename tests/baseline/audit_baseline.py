#!/usr/bin/env python3
"""Validate the checked-in coverage baseline."""

import csv
from decimal import Decimal, InvalidOperation
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
COVERAGE = ROOT / "tests/baseline/coverage_snapshot.csv"

COVERAGE_FIELDS = (
    "module",
    "owner",
    "status",
    "lines_covered",
    "lines_total",
    "lines_percent",
    "branches_covered",
    "branches_total",
    "branches_percent",
    "functions_covered",
    "functions_total",
    "functions_percent",
    "notes",
)
OWNERS = {
    "common",
    "bytecode",
    "compiler",
    "runtime",
    "itemstore",
    "libcall",
    "net",
    "executable",
}
REQUIRED_COVERAGE_OWNERS = OWNERS
INTEGER_FIELDS = (
    "lines_covered",
    "lines_total",
    "branches_covered",
    "branches_total",
    "functions_covered",
    "functions_total",
)
PERCENT_FIELDS = ("lines_percent", "branches_percent", "functions_percent")


def fail(message):
    print(f"baseline audit: {message}", file=sys.stderr)
    raise SystemExit(1)


def read_csv(path, required_fields):
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if reader.fieldnames is None or tuple(reader.fieldnames) != required_fields:
                fail(f"{path.name} has the wrong columns")
            rows = list(reader)
    except OSError as error:
        fail(f"cannot read {path}: {error}")
    return rows


def check_row_shape(rows, fields, name):
    for index, row in enumerate(rows, start=2):
        if set(row) != set(fields) or None in row:
            fail(f"{name} row {index} has extra or missing cells")
        if any(value is None for value in row.values()):
            fail(f"{name} row {index} has a missing cell")


def check_percent(value, covered, total, field, row_number):
    if total == 0:
        if value != "n/a":
            fail(f"coverage row {row_number} needs n/a for zero {field} total")
        return
    try:
        percent = Decimal(value)
    except InvalidOperation:
        fail(f"coverage row {row_number} has malformed {field} percentage")
    if percent < 0 or percent > 100:
        fail(f"coverage row {row_number} has out-of-range {field} percentage")
    expected = (Decimal(100) * covered / total).quantize(Decimal("0.01"))
    if percent != expected:
        fail(f"coverage row {row_number} has inconsistent {field} percentage")


def check_coverage(rows):
    check_row_shape(rows, COVERAGE_FIELDS, "coverage")
    expected_modules = {
        str(path.relative_to(ROOT))
        for path in (ROOT / "src").rglob("*.c")
        if path not in {ROOT / "src/parser.c", ROOT / "src/lexer.c"}
    }
    actual_modules = {row["module"] for row in rows}
    if actual_modules != expected_modules:
        fail("coverage module set does not match authored src/*.c files")
    seen = set()
    owners = set()
    for index, row in enumerate(rows, start=2):
        for field in COVERAGE_FIELDS:
            if not row[field].strip():
                fail(f"coverage row {index} has a blank {field}")
        if row["module"] in seen:
            fail(f"duplicate coverage module {row['module']}")
        seen.add(row["module"])
        if row["owner"] not in OWNERS:
            fail(f"unknown coverage owner {row['owner']}")
        owners.add(row["owner"])
        status = row["status"]
        if status not in {"measured", "no_instrumentable_code", "unavailable"}:
            fail(f"unknown coverage status {status}")
        values = {}
        for field in INTEGER_FIELDS:
            try:
                values[field] = int(row[field])
            except ValueError:
                fail(f"coverage row {index} has malformed {field}")
            if values[field] < 0:
                fail(f"coverage row {index} has a negative {field}")
        if values["lines_covered"] > values["lines_total"] or values["branches_covered"] > values["branches_total"] or values["functions_covered"] > values["functions_total"]:
            fail(f"coverage row {index} has covered counts above totals")
        if status == "no_instrumentable_code" and any(values.values()):
            fail(f"coverage row {index} has counts for no-instrumentable code")
        for field, covered, total in (
            ("lines_percent", values["lines_covered"], values["lines_total"]),
            ("branches_percent", values["branches_covered"], values["branches_total"]),
            ("functions_percent", values["functions_covered"], values["functions_total"]),
        ):
            check_percent(row[field], covered, total, field, index)
    missing = REQUIRED_COVERAGE_OWNERS - owners
    if missing:
        fail(f"coverage omits owners {sorted(missing)}")


def main():
    coverage = read_csv(COVERAGE, COVERAGE_FIELDS)
    check_coverage(coverage)
    print("baseline audit: coverage snapshot is valid")


if __name__ == "__main__":
    main()
