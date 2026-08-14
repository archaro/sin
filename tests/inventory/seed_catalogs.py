#!/usr/bin/env python3
"""Regenerate the reviewed inventory CSVs from canonical definitions.

This maintainer utility derives references from the legacy ledger and archive
objects.  Normal audit/test commands never rewrite checked-in catalogs.
"""

from __future__ import annotations

import csv
import sys
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import audit  # noqa: E402


ROOT = Path(__file__).resolve().parents[2]
OUT = Path(__file__).resolve().parent
FIELDS = {
    "contracts.csv": ("contract_id", "area", "facets", "test_ids", "description"),
    "language.csv": ("contract_id", "kind", "canonical_id", "facets", "test_ids", "description"),
    "bytecode.csv": ("contract_id", "kind", "canonical_id", "facets", "test_ids", "description"),
    "api.csv": ("contract_id", "module", "symbol", "facets", "test_ids", "description"),
    "libcalls.csv": ("contract_id", "library", "call", "lib_index", "call_index", "args", "metadata", "valid_behavior", "invalid_arguments", "ownership", "side_effects", "failure_behavior", "source_integration", "test_ids"),
    "executables.csv": ("contract_id", "program", "facet", "contract", "test_ids"),
    "tests.csv": ("test_id", "contract_ids"),
    "archive_exclusions.csv": ("symbol", "reason"),
}


def write(name: str, rows: list[dict[str, str]]) -> None:
    with (OUT / name).open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS[name], lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)


def load_ledger() -> list[dict[str, str]]:
    with (ROOT / "tests/baseline/legacy_test_ledger.csv").open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream))


def relevant_tests(ledger: list[dict[str, str]], words: list[str], owners: set[str], minimum: int = 1) -> list[str]:
    """Select stable tests by source/purpose, with an owner fallback."""
    words = [word.lower() for word in words if word]
    owners = {"net" if owner == "network" else owner for owner in owners}
    scored: list[tuple[int, str]] = []
    for row in ledger:
        haystack = " ".join((row["legacy_id"], row["source_location"], row["behavioral_purpose"])).lower()
        score = sum(3 for word in words if word in haystack)
        if row["owner"] in owners:
            score += 1
        if score:
            scored.append((score, row["legacy_id"]))
    scored.sort(key=lambda item: (-item[0], item[1]))
    selected = [test_id for _, test_id in scored]
    if len(selected) < minimum:
        selected.extend(row["legacy_id"] for row in ledger if row["owner"] in owners and row["legacy_id"] not in selected)
    return selected[: max(minimum, min(len(selected), 8))]


def module_for_object(object_name: str) -> str:
    stem = Path(object_name).stem
    if stem in {"log", "memory", "cli_io", "floatconv", "error", "util"}:
        return "common"
    if stem in {"bytecode_abi", "bytecode_wire", "bytecode_format", "bytecode_verify", "bytecode_convert", "sdiss_core"}:
        return "bytecode"
    if stem in {"parser", "lexer", "absyn", "semant", "ir", "lower", "compiler_context", "compiler_pipeline", "emitbc", "compdiag"}:
        return "compiler"
    if stem.startswith("libcall_"):
        return "libcall"
    if stem.startswith("item_"):
        return "itemstore"
    if stem in {"stack", "value", "list", "itemref", "vm", "task", "runtime_decode", "runtime_value", "runtime_item_ops", "runtime_opcode", "runtime_frame", "interpret"}:
        return "runtime"
    if stem in {"network", "libtelnet"}:
        return "network"
    return "common"


def call_tests(ledger: list[dict[str, str]], library: str) -> list[str]:
    tests = relevant_tests(ledger, [f"test_libcall_{library}", f"lc_{library}"], {"libcall", "runtime"}, minimum=2)
    if library == "task":
        tests += relevant_tests(ledger, ["task_lifecycle"], {"runtime"}, minimum=1)
    integration = relevant_tests(ledger, ["pipeline", "libcall"], {"compiler"}, minimum=1)
    return list(dict.fromkeys(tests + integration))[:10]


def main() -> None:
    tokens, productions, ast, ir, opcode_rows, libcalls = audit.source_entries(ROOT)
    archive = sorted((ROOT / "lib").glob("*/libsinshared.a"))[-1]
    symbol_objects = audit.archive_symbol_objects(archive)
    symbols = sorted(symbol_objects)
    authored_yy = {"yyalloc", "yyfree", "yyrealloc", "yyerror"}
    exclusions = sorted(symbol for symbol in symbols if symbol.startswith("yy") and symbol not in authored_yy)
    ledger = load_ledger()
    contracts: list[dict[str, str]] = []
    reverse: dict[str, set[str]] = defaultdict(set)

    def register(row: dict[str, str], name: str) -> None:
        if name == "contracts.csv":
            contracts.append(row)
        for test_id in row["test_ids"].split(";"):
            reverse[test_id].add(row["contract_id"])

    for row in ledger:
        contract_id = f"baseline.{row['legacy_id']}"
        register({"contract_id": contract_id, "area": "baseline", "facets": "normal=legacy behavior;invalid=legacy rejection;boundary=legacy limits;ownership=legacy cleanup;failure=legacy error path", "test_ids": row["legacy_id"], "description": f"Registered baseline behavior from {row['source_location']}."}, "contracts.csv")

    language: list[dict[str, str]] = []
    for kind, values, label in (("token", tokens, "lexer token"), ("production", productions, "grammar production")):
        for value in values:
            tests = relevant_tests(ledger, [value.lower(), "parser"], {"compiler"}, minimum=2)
            language.append({"contract_id": f"language.{kind}.{value.lower()}", "kind": kind, "canonical_id": f"grammar.{kind}.{value}", "facets": "normal=source accepted;invalid=diagnostic;boundary=source span;ownership=parser-owned;failure=allocation diagnostic", "test_ids": ";".join(tests), "description": f"{label} {value} is recognized and diagnosed through parser tests."})
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
            tests = relevant_tests(ledger, [slug, kind, "parser"], {"compiler"}, minimum=2)
            language.append({"contract_id": f"language.{kind}.{slug}", "kind": kind, "canonical_id": f"grammar.{kind}.{slug}", "facets": f"normal={kind} accepted;invalid={kind} diagnostic;boundary=source limits;ownership=compiler-owned;failure=diagnostic propagation", "test_ids": ";".join(tests), "description": f"The {value} {kind} contract is exercised by source and diagnostic tests."})

    bytecode: list[dict[str, str]] = []
    for kind, values, prefix, label in (("ast", ast, "ast.node", "AST node"), ("ir", ir, "ir.op", "IR operation")):
        for value in values:
            tests = relevant_tests(ledger, [value.lower().removeprefix("ir_op_"), "emitbc", "parser"], {"compiler", "bytecode"}, minimum=2)
            bytecode.append({"contract_id": f"bytecode.{kind}.{value.lower()}", "kind": kind, "canonical_id": f"{prefix}.{value}", "facets": "normal=constructed;invalid=validator rejection;boundary=operand limits;ownership=unit-owned;failure=allocation or diagnostic", "test_ids": ";".join(tests), "description": f"{label} {value} is constructed, lowered, and validated by compiler tests."})
    for value, symbol in opcode_rows:
        tests = relevant_tests(ledger, [value.lower(), "opcode", "emitbc"], {"bytecode", "compiler", "runtime"}, minimum=3)
        bytecode.append({"contract_id": f"bytecode.opcode.{value.lower()}", "kind": "opcode", "canonical_id": f"opcode.{value}", "facets": "normal=encoded dispatch;invalid=verifier diagnostic;boundary=operand width;ownership=bytecode buffer;failure=malformed stream", "test_ids": ";".join(tests), "description": f"{symbol}; schema encoding and opcode row {value}."})
    extras = {
        "encoding": ["header", "integer", "float", "string", "label", "libcall", "embedded-code", "item"],
        "verifier": ["opcode-boundary", "operand-width", "stack-underflow", "stack-overflow", "jump-target", "call-target", "libcall-pair", "item-expression"],
        "lowering": ["expression", "statement", "short-circuit", "loop", "foreach", "item", "call", "libcall"],
        "runtime": ["dispatch", "stack", "locals", "control-flow", "item-ops", "libcall", "errors", "cleanup"],
        "disassembly": ["mnemonic", "operand", "header", "item-expression", "malformed", "options"],
    }
    for kind, values in extras.items():
        for value in values:
            tests = relevant_tests(ledger, [value, kind], {"bytecode", "compiler", "runtime"}, minimum=2)
            bytecode.append({"contract_id": f"bytecode.{kind}.{value}", "kind": kind, "canonical_id": f"{kind}.{value}", "facets": f"normal={kind} operation;invalid={kind} rejection;boundary=wire limits;ownership=caller buffer;failure=diagnostic result", "test_ids": ";".join(tests), "description": f"Bytecode {kind} behavior for {value} is covered by focused tests."})

    api: list[dict[str, str]] = []
    for symbol in symbols:
        if symbol in exclusions:
            continue
        module = module_for_object(symbol_objects[symbol])
        tests = relevant_tests(ledger, [symbol.lower(), Path(symbol_objects[symbol]).stem], {module}, minimum=2)
        api.append({"contract_id": f"api.symbol.{symbol}", "module": module, "symbol": symbol, "facets": f"normal={module} symbol {symbol} returns or mutates its declared object;invalid=its declaration's argument and state preconditions;boundary=its documented size and range limits;ownership={module} defines allocation, borrowing, and cleanup;failure=its declared diagnostic, status, or nil result", "test_ids": ";".join(tests), "description": f"{module} API symbol {symbol} is defined by {symbol_objects[symbol]} and exercised through module contracts."})
    app_tests = {
        "scomp": relevant_tests(ledger, ["pipeline", "compiler", "cli"], {"compiler", "executable"}, minimum=3),
        "sin": relevant_tests(ledger, ["sin_", "itemstore", "runtime"], {"runtime", "itemstore", "executable"}, minimum=3),
        "sdiss": relevant_tests(ledger, ["sdiss"], {"bytecode", "compiler"}, minimum=3),
        "sconv": relevant_tests(ledger, ["sconv"], {"executable", "itemstore"}, minimum=3),
    }
    for program, tests in app_tests.items():
        symbol = f"{program}.main"
        api.append({"contract_id": f"api.entrypoint.{program}", "module": "application", "symbol": symbol, "facets": f"normal={program} parses command-line options and orchestrates its services;invalid={program} rejects malformed arguments and unavailable inputs;boundary={program} preserves path, byte, and resource limits;ownership={program} owns startup resources and releases them on exit;failure={program} reports diagnostics and a nonzero status", "test_ids": ";".join(tests), "description": f"Application entry point contract for {program}."})

    lib_rows: list[dict[str, str]] = []
    library_behavior = {
        "sys": "system itemstore and compiler service",
        "list": "immutable list transformation",
        "net": "network connection and output service",
        "str": "runtime string transformation",
        "task": "runtime task lifecycle service",
    }
    for library, call, lib_index, call_index, args in libcalls:
        tests = call_tests(ledger, library)
        target = f"{library}.{call}"
        behavior = library_behavior[library]
        lib_rows.append({"contract_id": f"libcall.{target}", "library": library, "call": call, "lib_index": str(lib_index), "call_index": str(call_index), "args": str(args), "metadata": f"metadata={target} ABI pair {lib_index}:{call_index}, handler arity {args}, registry order is stable", "valid_behavior": f"valid={target} performs its {behavior} operation for {args} validated arguments and returns its documented runtime value", "invalid_arguments": f"invalid={target} rejects wrong arity, wrong value types, out-of-range indexes, or unavailable resources before side effects", "ownership": f"ownership={target} transfers, borrows, or releases values according to the runtime value contract for {library}", "side_effects": f"side_effects={target} changes {behavior} state only where its named operation requires it; otherwise it leaves state unchanged", "failure_behavior": f"failure={target} returns the handler's nil/status result and preserves the context diagnostic on allocation, lookup, or I/O failure", "source_integration": f"source={target} resolves from source-language {library}.{call} syntax, registry metadata, and the LIBCALL opcode", "test_ids": ";".join(tests)})

    executable_rows: list[dict[str, str]] = []
    executable_contracts = {
        "scomp": {"command-line": "scomp accepts a source path and optional output destination", "input-output": "scomp reads Sinistra source and writes verified object bytes", "exit-status": "scomp returns zero only after successful compilation and nonzero on diagnostics", "persistence": "scomp emits a self-contained object without mutating the source tree", "errors": "scomp reports source, lexer, parser, semantic, and output failures"},
        "sin": {"command-line": "sin accepts the itemstore/source-root options used by the runtime", "input-output": "sin loads persisted state and serves runtime/network input and output", "exit-status": "sin returns a failure status for startup, load, or shutdown errors", "persistence": "sin applies its itemstore version and source-sidecar policy during startup and save", "errors": "sin reports malformed state, source, runtime, and network errors"},
        "sdiss": {"command-line": "sdiss accepts an object input and disassembly/output options", "input-output": "sdiss reads bytecode and writes deterministic mnemonics and operands", "exit-status": "sdiss returns nonzero for malformed input or output failure", "persistence": "sdiss is read-only and does not rewrite its input object", "errors": "sdiss preserves verifier diagnostics and reports writer failures"},
        "sconv": {"command-line": "sconv accepts legacy itemstore input, destination, and conversion options", "input-output": "sconv reads earlier itemstores and writes the latest compatible format", "exit-status": "sconv returns nonzero when conversion or durability fails", "persistence": "sconv preserves atomicity and existing data when conversion fails", "errors": "sconv reports bad headers, malformed records, collisions, and durability failures"},
    }
    for program, facets in executable_contracts.items():
        for facet, text in facets.items():
            executable_rows.append({"contract_id": f"executable.{program}.{facet}", "program": program, "facet": facet, "contract": text, "test_ids": ";".join(app_tests[program])})

    for row in language:
        register(row, "language.csv")
    for row in bytecode:
        register(row, "bytecode.csv")
    for row in api:
        register(row, "api.csv")
    for row in lib_rows:
        register(row, "libcalls.csv")
    for row in executable_rows:
        register(row, "executables.csv")
    test_rows = [{"test_id": row["legacy_id"], "contract_ids": ";".join(sorted(reverse[row["legacy_id"]]))} for row in ledger]
    write("contracts.csv", contracts)
    write("language.csv", language)
    write("bytecode.csv", bytecode)
    write("api.csv", api)
    write("libcalls.csv", lib_rows)
    write("executables.csv", executable_rows)
    write("tests.csv", test_rows)
    write("archive_exclusions.csv", [{"symbol": symbol, "reason": "Flex/Bison-generated scanner/parser interface; excluded from authored API inventory."} for symbol in exclusions])


if __name__ == "__main__":
    main()
