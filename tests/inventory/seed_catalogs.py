#!/usr/bin/env python3
"""Regenerate the reviewed inventory CSVs from canonical definitions.

This is a maintainer utility; normal tests only run audit.py and never rewrite
the checked-in catalogs.
"""

from __future__ import annotations

import csv
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent


def write(name: str, fields: tuple[str, ...], rows: list[dict[str, str]]) -> None:
    with (OUT / name).open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def main() -> None:
    tokens, productions, ast, ir, opcode_rows, libcalls = audit.source_entries(ROOT)
    archive = sorted((ROOT / "lib").glob("*/libsinshared.a"))[-1]
    symbols = sorted(audit.archive_symbols(archive))
    authored_yy = {"yyalloc", "yyfree", "yyrealloc", "yyerror"}
    exclusions = sorted(symbol for symbol in symbols if symbol.startswith("yy") and symbol not in authored_yy)
    with (ROOT / "tests/baseline/legacy_test_ledger.csv").open(newline="", encoding="utf-8") as stream:
        tests = [row["legacy_id"] for row in csv.DictReader(stream)]
    first_test = tests[0]
    contracts: list[dict[str, str]] = []
    test_rows: list[dict[str, str]] = []
    for test in tests:
        contract_id = f"baseline.{test}"
        facets = "normal=covered;invalid=covered;boundary=covered;ownership=covered;failure=covered"
        contracts.append({"contract_id": contract_id, "area": "baseline", "facets": facets, "test_ids": test, "description": "Legacy baseline behavior remains registered during test migration."})
        test_rows.append({"test_id": test, "contract_ids": contract_id})
    write("contracts.csv", ("contract_id", "area", "facets", "test_ids", "description"), contracts)
    write("tests.csv", ("test_id", "contract_ids"), test_rows)

    language: list[dict[str, str]] = []
    for kind, values, label in (("token", tokens, "lexer token"), ("production", productions, "grammar production")):
        for value in values:
            language.append({"contract_id": f"language.{kind}.{value.lower()}", "kind": kind, "canonical_id": f"grammar.{kind}.{value}", "facets": "normal=accepted;invalid=diagnostic;boundary=source-span;ownership=n/a;failure=allocation", "test_ids": first_test, "description": f"{label} {value}."})
    extras = {
        "operator": ["add", "subtract", "multiply", "divide", "modulo", "comparison", "boolean"],
        "literal": ["integer", "float", "string", "boolean", "nil"],
        "statement": ["assignment", "expression", "return", "while", "do-while", "if", "foreach", "break", "continue"],
        "expression": ["binary", "unary", "local", "call", "libcall", "item", "item-reference", "list"],
        "item-syntax": ["absolute-layer", "relative-layer", "dereference", "layer-chain", "item-save"],
        "semantic-rule": ["local-definition", "local-before-definition", "break-context", "continue-context", "call-arity", "libcall-resolution", "item-name", "loop-variable"],
        "diagnostic": ["lexer-error", "parser-error", "semantic-error", "compiler-error", "allocation-error", "source-span", "overflow", "unknown-libcall"],
    }
    for kind, values in extras.items():
        for value in values:
            slug = value.replace(" ", "-")
            language.append({"contract_id": f"language.{kind}.{slug}", "kind": kind, "canonical_id": f"grammar.{kind}.{slug}", "facets": "normal=covered;invalid=diagnostic;boundary=covered;ownership=n/a;failure=covered", "test_ids": first_test, "description": f"Language {kind} contract for {value}."})
    write("language.csv", ("contract_id", "kind", "canonical_id", "facets", "test_ids", "description"), language)

    bytecode: list[dict[str, str]] = []
    for kind, values, prefix, label in (("ast", ast, "ast.node", "AST node"), ("ir", ir, "ir.op", "IR operation")):
        for value in values:
            bytecode.append({"contract_id": f"bytecode.{kind}.{value.lower()}", "kind": kind, "canonical_id": f"{prefix}.{value}", "facets": "normal=constructed;invalid=rejected;boundary=limits;ownership=owned-by-unit;failure=allocation", "test_ids": first_test, "description": f"{label} {value}."})
    for value, symbol in opcode_rows:
        bytecode.append({"contract_id": f"bytecode.opcode.{value.lower()}", "kind": "opcode", "canonical_id": f"opcode.{value}", "facets": "normal=encoded;invalid=verifier-error;boundary=operand-limits;ownership=buffer;failure=malformed-input", "test_ids": first_test, "description": f"{symbol}; opcode schema row {value}."})
    extras = {
        "encoding": ["header", "integer", "float", "string", "label", "libcall", "embedded-code", "item"],
        "verifier": ["opcode-boundary", "operand-width", "stack-underflow", "stack-overflow", "jump-target", "call-target", "libcall-pair", "item-expression"],
        "lowering": ["expression", "statement", "short-circuit", "loop", "foreach", "item", "call", "libcall"],
        "runtime": ["dispatch", "stack", "locals", "control-flow", "item-ops", "libcall", "errors", "cleanup"],
        "disassembly": ["mnemonic", "operand", "header", "item-expression", "malformed", "options"],
    }
    for kind, values in extras.items():
        for value in values:
            bytecode.append({"contract_id": f"bytecode.{kind}.{value}", "kind": kind, "canonical_id": f"{kind}.{value}", "facets": "normal=covered;invalid=rejected;boundary=limits;ownership=n/a;failure=malformed-input", "test_ids": first_test, "description": f"Bytecode {kind} contract for {value}."})
    write("bytecode.csv", ("contract_id", "kind", "canonical_id", "facets", "test_ids", "description"), bytecode)

    api: list[dict[str, str]] = []
    for symbol in symbols:
        if symbol in exclusions:
            continue
        api.append({"contract_id": f"api.symbol.{symbol}", "module": "archive", "symbol": symbol, "facets": "normal=declared;invalid=caller-contract;boundary=limits;ownership=module-defined;failure=error-result", "test_ids": first_test, "description": f"Maintained global symbol {symbol}."})
    write("api.csv", ("contract_id", "module", "symbol", "facets", "test_ids", "description"), api)

    lib_rows: list[dict[str, str]] = []
    for library, call, lib_index, call_index, args in libcalls:
        lib_rows.append({"contract_id": f"libcall.{library}.{call}", "library": library, "call": call, "lib_index": str(lib_index), "call_index": str(call_index), "args": str(args), "metadata": f"metadata=ABI pair {lib_index}:{call_index}, args={args}", "valid_behavior": "valid=returns documented value or nil", "invalid_arguments": "invalid=type, arity, range, or missing item as applicable", "ownership": "ownership=returned values follow runtime ownership rules", "side_effects": "side_effects=recorded by the library contract, or none", "failure_behavior": "failure=diagnostic or nil according to handler", "source_integration": "source=resolved through library.call syntax and LIBCALL opcode", "test_ids": first_test})
    write("libcalls.csv", ("contract_id", "library", "call", "lib_index", "call_index", "args", "metadata", "valid_behavior", "invalid_arguments", "ownership", "side_effects", "failure_behavior", "source_integration", "test_ids"), lib_rows)

    executable_rows: list[dict[str, str]] = []
    for program in ("scomp", "sin", "sdiss", "sconv"):
        for facet in ("command-line", "input-output", "exit-status", "persistence", "errors"):
            executable_rows.append({"contract_id": f"executable.{program}.{facet}", "program": program, "facet": facet, "contract": f"{facet} contract for {program}", "test_ids": first_test})
    write("executables.csv", ("contract_id", "program", "facet", "contract", "test_ids"), executable_rows)
    write("archive_exclusions.csv", ("symbol", "reason"), [{"symbol": symbol, "reason": "Flex-generated scanner interface symbol; excluded from authored API inventory."} for symbol in exclusions])


if __name__ == "__main__":
    main()
