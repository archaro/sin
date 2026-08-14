#!/usr/bin/env python3
"""Audit Sinistra's checked-in contract inventories.

The source files named in the project brief are the authority for language,
IR, opcode, and libcall identifiers.  This script deliberately uses only the
Python standard library so it can run before the replacement test framework.
"""

from __future__ import annotations

import argparse
import csv
import re
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
CATALOG_FILES = (
    "contracts.csv",
    "language.csv",
    "bytecode.csv",
    "api.csv",
    "libcalls.csv",
    "executables.csv",
    "tests.csv",
    "archive_symbols.csv",
    "archive_exclusions.csv",
)
ID_RE = re.compile(r"^[A-Za-z0-9_.-]+$")
# These scanner/parser hooks are implemented in the authored .l/.y sources;
# generated Flex/Bison entry points remain the only excluded yy* symbols.
AUTHORED_YY_SYMBOLS = {"yyalloc", "yyfree", "yyrealloc", "yyerror"}
ARCHIVE_OBJECT_MODULES = {
    "log.o": "common", "memory.o": "common", "cli_io.o": "common", "floatconv.o": "common", "error.o": "common", "util.o": "common",
    "bytecode_abi.o": "bytecode", "bytecode_wire.o": "bytecode", "bytecode_format.o": "bytecode", "bytecode_verify.o": "bytecode", "bytecode_convert.o": "bytecode", "sdiss_core.o": "bytecode",
    "parser.o": "compiler", "lexer.o": "compiler", "absyn.o": "compiler", "semant.o": "compiler", "ir.o": "compiler", "lower.o": "compiler", "compiler_context.o": "compiler", "compiler_pipeline.o": "compiler", "emitbc.o": "compiler", "compdiag.o": "compiler",
    "libcall_sys.o": "libcall", "libcall_task.o": "libcall", "libcall_net.o": "libcall", "libcall_str.o": "libcall", "libcall_list.o": "libcall", "libcall_registry.o": "libcall", "libcall_table.o": "libcall",
    "stack.o": "runtime", "value.o": "runtime", "list.o": "runtime", "itemref.o": "runtime", "vm.o": "runtime", "task.o": "runtime", "runtime_decode.o": "runtime", "runtime_value.o": "runtime", "runtime_item_ops.o": "runtime", "runtime_opcode.o": "runtime", "runtime_frame.o": "runtime", "interpret.o": "runtime",
    "item_hash.o": "itemstore", "item_tree.o": "itemstore", "item_registry.o": "itemstore", "item_persist.o": "itemstore", "item_source_persist.o": "itemstore", "item_error.o": "itemstore", "item_persist_v1.o": "itemstore", "item_persist_v2.o": "itemstore",
    "network.o": "network", "libtelnet.o": "network",
}


class AuditError(Exception):
    pass


def fail(message: str) -> None:
    raise AuditError(message)


def read_csv(path: Path, fields: tuple[str, ...]) -> list[dict[str, str]]:
    try:
        with path.open(newline="", encoding="utf-8") as stream:
            reader = csv.DictReader(stream)
            if tuple(reader.fieldnames or ()) != fields:
                fail(f"{path.name} has wrong columns")
            rows = list(reader)
    except OSError as error:
        fail(f"cannot read {path}: {error}")
    for line, row in enumerate(rows, start=2):
        if set(row) != set(fields) or any(value is None for value in row.values()):
            fail(f"{path.name} row {line} has missing or extra cells")
        if any(not row[field].strip() for field in fields):
            fail(f"{path.name} row {line} has a blank field")
    return rows


def split_refs(value: str) -> list[str]:
    return [item for item in value.split(";") if item]


def source_entries(root: Path) -> tuple[list[str], list[str], list[str], list[str], list[tuple[str, str]], list[tuple[str, str, int, int, int]]]:
    parser = (root / "src/compiler/parser.y").read_text(encoding="utf-8")
    tokens: list[str] = []
    for line in parser.splitlines():
        if line.startswith("%token"):
            tokens.extend(re.findall(r"\bT[A-Z][A-Z0-9_]*\b", line))
    productions = re.findall(r"^([a-z][a-z0-9_]*)\s*:", parser, re.MULTILINE)
    ast_text = (root / "src/compiler/absyn.h").read_text(encoding="utf-8")
    ast_block = "N_" + ast_text.split("typedef enum { N_", 1)[1].split("} ENUM_NODE", 1)[0]
    ast = re.findall(r"\bN_[A-Z0-9_]+\b", ast_block)
    abi = (root / "src/bytecode/bytecode_abi.h").read_text(encoding="utf-8")
    ir_block = abi.split("typedef enum {", 1)[1].split("} IR_Op", 1)[0]
    ir = re.findall(r"\bIR_OP_[A-Z0-9_]+\b", ir_block)
    schema = (root / "src/bytecode/opcode_schema.def").read_text(encoding="utf-8")
    opcodes = re.findall(r"^OP\(([^,]+),\s*'?(.*?)'?,", schema, re.MULTILINE)
    # The second capture is intentionally retained as an ABI check, not just
    # an inventory count.
    opcode_rows = [(name.strip(), symbol.strip()) for name, symbol in opcodes]
    libcall_text = (root / "src/libcall/libcall_list.h").read_text(encoding="utf-8")
    libcalls: list[tuple[str, str, int, int, int]] = []
    pattern = re.compile(
        r'^\s*X\("([^"]+)",\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+),',
        re.MULTILINE,
    )
    for library, call, lib_index, call_index, args in pattern.findall(libcall_text):
        libcalls.append((library, call, int(lib_index), int(call_index), int(args)))
    if len(tokens) != len(set(tokens)) or len(productions) != len(set(productions)):
        fail("canonical grammar contains duplicate identifiers")
    if len(ast) != len(set(ast)) or len(ir) != len(set(ir)):
        fail("canonical AST/IR definitions contain duplicate identifiers")
    if len(opcode_rows) != len({name for name, _ in opcode_rows}):
        fail("canonical opcode schema contains duplicate identifiers")
    if len(libcalls) != len({(row[0], row[1]) for row in libcalls}):
        fail("canonical libcall list contains duplicate identifiers")
    return tokens, productions, ast, ir, opcode_rows, libcalls


def archive_symbol_objects(archive: Path) -> dict[str, str]:
    """Return each defined symbol and the authored archive object owning it."""
    if not archive.is_file():
        fail(f"archive does not exist: {archive}")
    try:
        result = subprocess.run(
            ["nm", "-g", "--defined-only", str(archive)],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as error:
        fail(f"cannot inspect archive {archive}: {error}")
    symbols: dict[str, str] = {}
    object_name = "unknown.o"
    for line in result.stdout.splitlines():
        if line.endswith(":") and not line.startswith(" "):
            object_name = line[:-1]
            continue
        fields = line.split()
        if len(fields) >= 3 and len(fields[-2]) == 1:
            symbols[fields[-1]] = object_name
    if not symbols:
        fail(f"archive has no global symbols: {archive}")
    return symbols


def archive_symbols(archive: Path) -> set[str]:
    return set(archive_symbol_objects(archive))


def validate_archive_object_modules(symbol_objects: dict[str, str]) -> None:
    unknown_objects = set(symbol_objects.values()) - set(ARCHIVE_OBJECT_MODULES)
    if unknown_objects:
        fail(f"archive contains unmapped object(s): {sorted(unknown_objects)}")


def check_ids(rows: list[dict[str, str]], field: str, name: str) -> None:
    seen: set[str] = set()
    for line, row in enumerate(rows, start=2):
        value = row[field]
        if not ID_RE.fullmatch(value):
            fail(f"{name} row {line} has malformed {field} {value!r}")
        if value in seen:
            fail(f"duplicate {field} {value}")
        seen.add(value)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (for fixture audits)")
    parser.add_argument("--catalog-dir", type=Path, help="catalog directory, relative to --root")
    parser.add_argument("--archive", type=Path, help="built shared archive, relative to --root")
    args = parser.parse_args()
    root = args.root.resolve()
    catalog_dir = (args.catalog_dir or root / "tests/inventory")
    if not catalog_dir.is_absolute():
        catalog_dir = root / catalog_dir
    archive = args.archive
    if archive is None:
        candidates = sorted((root / "lib").glob("*/libsinshared.a"))
        if not candidates:
            fail("no shared archive found; run make lib first")
        archive = candidates[-1]
    elif not archive.is_absolute():
        archive = root / archive

    try:
        fields = {
            "contracts.csv": ("contract_id", "area", "facets", "test_ids", "description"),
            "language.csv": ("contract_id", "kind", "canonical_id", "facets", "test_ids", "description"),
            "bytecode.csv": ("contract_id", "kind", "canonical_id", "facets", "test_ids", "description"),
            "api.csv": ("contract_id", "module", "contract_name", "declaration", "normal_behavior", "invalid_input", "boundary_behavior", "ownership_cleanup", "failure_behavior", "test_ids"),
            "libcalls.csv": ("contract_id", "library", "call", "lib_index", "call_index", "args", "metadata", "valid_behavior", "invalid_arguments", "ownership", "side_effects", "failure_behavior", "source_integration", "test_ids"),
            "executables.csv": ("contract_id", "program", "facet", "contract", "test_ids"),
            "tests.csv": ("test_id", "contract_ids"),
            "archive_symbols.csv": ("symbol", "object", "module", "declaration", "api_contract_id"),
            "archive_exclusions.csv": ("symbol", "reason"),
        }
        catalogs = {name: read_csv(catalog_dir / name, fields[name]) for name in CATALOG_FILES}
        check_ids(catalogs["contracts.csv"], "contract_id", "contracts")
        all_contracts = {row["contract_id"] for row in catalogs["contracts.csv"]}
        for name in ("language.csv", "bytecode.csv", "api.csv", "libcalls.csv", "executables.csv"):
            check_ids(catalogs[name], "contract_id", name)
            overlap = all_contracts.intersection(row["contract_id"] for row in catalogs[name])
            if overlap:
                fail(f"duplicate contract IDs across catalogs: {sorted(overlap)[0]}")
            all_contracts.update(row["contract_id"] for row in catalogs[name])
        check_ids(catalogs["tests.csv"], "test_id", "tests")
        ledger_path = root / "tests/baseline/legacy_test_ledger.csv"
        ledger = read_csv(ledger_path, ("legacy_id", "category", "suite", "owner", "source_location", "behavioral_purpose", "planned_replacement_id", "parity_notes"))
        expected_tests = {row["legacy_id"] for row in ledger}
        tests = {row["test_id"] for row in catalogs["tests.csv"]}
        if tests != expected_tests:
            missing, extra = sorted(expected_tests - tests), sorted(tests - expected_tests)
            fail(f"test inventory mismatch (missing={missing[:1]}, unknown={extra[:1]})")
        for row in catalogs["contracts.csv"]:
            refs = split_refs(row["test_ids"])
            if not refs:
                fail(f"contract {row['contract_id']} has no registered tests")
            unknown = set(refs) - expected_tests
            if unknown:
                fail(f"contract {row['contract_id']} references unknown test ID {sorted(unknown)[0]}")
        for row in catalogs["tests.csv"]:
            refs = split_refs(row["contract_ids"])
            if not refs:
                fail(f"test {row['test_id']} has no contract IDs")
            unknown = set(refs) - all_contracts
            if unknown:
                fail(f"test {row['test_id']} references unknown contract ID {sorted(unknown)[0]}")
        for name in ("language.csv", "bytecode.csv", "api.csv", "libcalls.csv", "executables.csv"):
            for row in catalogs[name]:
                refs = split_refs(row["test_ids"])
                unknown = set(refs) - expected_tests
                if not refs:
                    fail(f"{name} contract {row['contract_id']} has no registered tests")
                if unknown:
                    fail(f"{name} contract {row['contract_id']} references unknown test ID {sorted(unknown)[0]}")

        # The test catalog is a reciprocal index, not an independent claim:
        # every edge in either direction must be present on the other side.
        reverse: dict[str, set[str]] = defaultdict(set)
        for name in ("contracts.csv", "language.csv", "bytecode.csv", "api.csv", "libcalls.csv", "executables.csv"):
            for row in catalogs[name]:
                for test_id in split_refs(row["test_ids"]):
                    reverse[test_id].add(row["contract_id"])
        for row in catalogs["tests.csv"]:
            actual = set(split_refs(row["contract_ids"]))
            expected = reverse.get(row["test_id"], set())
            if actual != expected:
                fail(f"test {row['test_id']} has one-sided contract edges (missing={sorted(expected - actual)[:1]}, stale={sorted(actual - expected)[:1]})")

        tokens, productions, ast, ir, opcode_rows, libcalls = source_entries(root)
        language_expected = {
            "token": {f"grammar.token.{value}" for value in tokens},
            "production": {f"grammar.production.{value}" for value in productions},
        }
        language = catalogs["language.csv"]
        for kind, expected in language_expected.items():
            actual = {row["canonical_id"] for row in language if row["kind"] == kind}
            if actual != expected:
                fail(f"language {kind} inventory mismatch (missing={sorted(expected - actual)[:1]}, stale={sorted(actual - expected)[:1]})")
        bytecode_expected = {
            "ast": {f"ast.node.{value}" for value in ast},
            "ir": {f"ir.op.{value}" for value in ir},
            "opcode": {f"opcode.{value}" for value, _ in opcode_rows},
        }
        bytecode = catalogs["bytecode.csv"]
        for kind, expected in bytecode_expected.items():
            actual = {row["canonical_id"] for row in bytecode if row["kind"] == kind}
            if actual != expected:
                fail(f"bytecode {kind} inventory mismatch (missing={sorted(expected - actual)[:1]}, stale={sorted(actual - expected)[:1]})")
        for row in bytecode:
            if row["kind"] == "opcode":
                name = row["canonical_id"].removeprefix("opcode.")
                expected_symbol = dict(opcode_rows)[name]
                if row["description"].split(";", 1)[0] != expected_symbol:
                    fail(f"opcode {name} has stale encoding metadata")
        expected_libcalls = {(library, call, lib_index, call_index, args) for library, call, lib_index, call_index, args in libcalls}
        actual_libcalls = {(row["library"], row["call"], int(row["lib_index"]), int(row["call_index"]), int(row["args"])) for row in catalogs["libcalls.csv"]}
        if actual_libcalls != expected_libcalls:
            fail("libcall inventory does not match src/libcall/libcall_list.h")
        if any(not all(row[field].strip() for field in ("metadata", "valid_behavior", "invalid_arguments", "ownership", "side_effects", "failure_behavior", "source_integration")) for row in catalogs["libcalls.csv"]):
            fail("libcall rows must state metadata, valid, invalid, ownership, side effects, failure, and source facets")
        generic_libcall_values = {
            "valid=returns documented value or nil",
            "invalid=type, arity, range, or missing item as applicable",
            "ownership=returned values follow runtime ownership rules",
            "side_effects=recorded by the library contract, or none",
            "failure=diagnostic or nil according to handler",
            "source=resolved through library.call syntax and LIBCALL opcode",
        }
        for row in catalogs["libcalls.csv"]:
            target = f"{row['library']}.{row['call']}"
            if any(row[field] in generic_libcall_values or target not in row[field] for field in ("valid_behavior", "invalid_arguments", "ownership", "side_effects", "failure_behavior", "source_integration")):
                fail(f"libcall {target} retains generic or non-specific facet text")
        expected_programs = {"scomp", "sin", "sdiss", "sconv"}
        expected_executable_facets = {"command-line", "input-output", "exit-status", "persistence", "errors"}
        executable_groups: dict[str, list[dict[str, str]]] = defaultdict(list)
        for row in catalogs["executables.csv"]:
            executable_groups[row["program"]].append(row)
            if row["contract"] == f"{row['facet']} contract for {row['program']}":
                fail(f"executable {row['program']} retains placeholder contract text")
        if set(executable_groups) != expected_programs:
            fail(f"executable inventory mismatch: {sorted(executable_groups)}")
        for program, rows in executable_groups.items():
            facets = [row["facet"] for row in rows]
            if set(facets) != expected_executable_facets or len(facets) != len(set(facets)):
                fail(f"executable {program} must have exactly one row for each required facet")

        symbol_objects = archive_symbol_objects(archive)
        symbols = set(symbol_objects)
        validate_archive_object_modules(symbol_objects)
        exclusions = {row["symbol"]: row["reason"] for row in catalogs["archive_exclusions.csv"]}
        if len(exclusions) != len(catalogs["archive_exclusions.csv"]):
            fail("duplicate archive exclusion")
        for symbol, reason in exclusions.items():
            if not reason or not symbol.startswith("yy") or symbol in AUTHORED_YY_SYMBOLS:
                fail(f"archive exclusion {symbol} is not a narrowly justified generated symbol")
            if symbol not in symbols:
                fail(f"archive exclusion is stale: {symbol}")
        maintained = symbols - set(exclusions)
        archive_rows = catalogs["archive_symbols.csv"]
        if len({row["symbol"] for row in archive_rows}) != len(archive_rows):
            fail("duplicate archive symbol accountability row")
        archive_by_symbol = {row["symbol"]: row for row in archive_rows}
        if set(archive_by_symbol) != maintained:
            fail(f"archive symbol catalog mismatch (missing={sorted(maintained - set(archive_by_symbol))[:1]}, stale={sorted(set(archive_by_symbol) - maintained)[:1]})")
        api_rows = catalogs["api.csv"]
        api_by_id = {row["contract_id"]: row for row in api_rows}
        for row in archive_rows:
            symbol = row["symbol"]
            if row["object"] != symbol_objects.get(symbol):
                fail(f"archive declaration provenance mismatch for {symbol}")
            if row["module"] != ARCHIVE_OBJECT_MODULES.get(row["object"]):
                fail(f"archive module ownership mismatch for {symbol}")
            if not row["declaration"].startswith("src/") or symbol not in row["declaration"]:
                fail(f"archive declaration provenance is incomplete for {symbol}")
            contract = api_by_id.get(row["api_contract_id"])
            if contract is None:
                fail(f"archive symbol {symbol} maps to unknown API contract {row['api_contract_id']}")
            if contract["module"] != row["module"]:
                fail(f"archive symbol {symbol} maps across API modules")
        archive_contracts = {row["api_contract_id"] for row in archive_rows}
        if any(row["module"] != "application" and row["contract_id"] not in archive_contracts for row in api_rows):
            fail("API contract has no archive symbol mapping")
        modules = {row["module"] for row in api_rows}
        required_modules = {"common", "compiler", "bytecode", "runtime", "itemstore", "libcall", "network", "application"}
        if not required_modules.issubset(modules):
            fail(f"API catalog omits architectural modules {sorted(required_modules - modules)}")
        forbidden = (
            "module=archive", "normal=declared", "invalid=caller-contract",
            "boundary=limits", "ownership=module-defined", "failure=error-result",
            "Maintained global symbol", "returns or mutates its declared object",
            "performs the module operation declared at its authored definition",
            "rejects invalid arguments and state preconditions documented by its declaration",
            "preserves the limits represented by its authored data structures",
            "follows the borrow/own/free rule documented beside its declaration",
            "reports its declared status/error result and preserves state on failure",
            "performs the declared allocation, growth, or overflow-check operation",
            "checks semantic state or records the diagnostic fields named by its compiler declaration",
            "constructs, traverses, names, or releases the AST/value structure named by its declaration",
            "exposes the module operation represented by",
            "malformed arguments or state before publishing a result",
            "preserves the limits and wire/state boundaries exercised by its registered witness tests",
            "retains resources it owns, transfers successful results to callers",
            "returns its documented failure status or diagnostic while preserving prior observable state",
        )
        if any(any(value in " ".join(row[field] for field in ("declaration", "normal_behavior", "invalid_input", "boundary_behavior", "ownership_cleanup", "failure_behavior")) for value in forbidden) for row in api_rows):
            fail("API catalog retains placeholder ownership or facet vocabulary")
        if {row["contract_id"] for row in api_rows if row["module"] == "application"} != {"api.entrypoint.scomp", "api.entrypoint.sin", "api.entrypoint.sdiss", "api.entrypoint.sconv"}:
            fail("API application entry-point set is incomplete or stale")
        if any(not all(row[field].strip() for field in ("contract_name", "declaration", "normal_behavior", "invalid_input", "boundary_behavior", "ownership_cleanup", "failure_behavior")) for row in api_rows):
            fail("API rows must state declaration, normal, invalid, boundary, ownership, and failure facets")
    except (AuditError, OSError, ValueError, KeyError) as error:
        print(f"inventory audit: ERROR: {error}", file=sys.stderr)
        return 1
    print(f"inventory audit: valid ({len(tokens)} tokens, {len(productions)} productions, {len(ast)} AST nodes, {len(ir)} IR rows, {len(opcode_rows)} opcode rows, {len(libcalls)} libcalls, {len(maintained)} archive symbols, {len(api_rows)} API contracts, {len(expected_tests)} tests)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
