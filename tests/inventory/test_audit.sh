#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
audit=$repo_root/tests/inventory/audit.py
archive=${1:-}
if [ -z "$archive" ]; then
  archive=$(find "$repo_root/lib" -name libsinshared.a -print -quit)
fi
[ -n "$archive" ] && [ -f "$archive" ]
case $archive in
  /*) ;;
  *) archive=$repo_root/$archive ;;
esac

PYTHONDONTWRITEBYTECODE=1 python3 "$audit" --archive "$archive" >/dev/null
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$repo_root/tests/inventory" python3 - <<'PY'
import audit

assert audit.grammar_tokens("%token TPLUS\n%left TPLUS\n") == ["TPLUS"]
descriptor_source = r'''
/* {"conformance.comment-spoof", ignored, "conformance", 1, "x"}, */
static const char *unrelated = "{\\\"conformance.string-spoof\\\",";
static const TF_TestDescriptor tests[] = {
  {"conformance.real", real_test, "conformance", 5000, "contract.real"},
};
'''
assert audit.conformance_descriptor_ids(descriptor_source) == {"conformance.real"}
try:
    audit.grammar_tokens("%left TPLUS\n%right TPLUS\n")
except audit.AuditError as error:
    assert "conflicting precedence" in str(error)
else:
    raise AssertionError("conflicting precedence declarations were accepted")
tokens, _, _, ir, opcodes, libcalls = audit.source_entries(audit.ROOT)
assert "TAND" in tokens
assert "TNOT" in tokens
assert "IR_OP_PUSH_INT" in ir
assert "IR_OP_IR_OP_PUSH_INT" not in ir
libcall_opcode = next(row for row in opcodes if row[0] == "LIBCALL")
assert len(libcall_opcode) == 10
assert libcall_opcode[6] == "VALIDATE_LIBCALL_PAIR"
assert libcall_opcode[7] == "op_libcall"
assert libcall_opcode[8] == "STACK_DYNAMIC(0,1,IR_STACK_LIBCALL)"
assert libcall_opcode[9] == "IR_CONTROL_STRAIGHT"
assert next(row for row in libcalls if row[:2] == ("sys", "backup"))[5] == "lc_sys_backup"
print("[inventory-audit] exact grammar, IR, opcode, and libcall extraction passed")
PY

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -a "$repo_root/tests/inventory/." "$work/catalog"

expect_failure() {
  local label=$1
  local expected=$2
  local output=$work/$label.out
  if PYTHONDONTWRITEBYTECODE=1 python3 "$audit" --catalog-dir "$work/catalog" --archive "$archive" >"$output" 2>&1; then
    printf 'audit negative self-test unexpectedly passed: %s\n' "$label" >&2
    exit 1
  fi
  grep -q 'inventory audit: ERROR:' "$output"
  grep -q "$expected" "$output"
}

expect_root_failure() {
  local label=$1
  local expected=$2
  local output=$work/$label.out
  if PYTHONDONTWRITEBYTECODE=1 python3 "$audit" --root "$work/root" \
      --catalog-dir "$work/catalog" --archive "$archive" >"$output" 2>&1; then
    printf 'audit negative self-test unexpectedly passed: %s\n' "$label" >&2
    exit 1
  fi
  grep -q 'inventory audit: ERROR:' "$output"
  grep -q "$expected" "$output"
}

# Precedence directives declare lexer tokens too; canonical drift must fail.
mkdir -p "$work/root/src/compiler" "$work/root/src/bytecode" \
  "$work/root/src/libcall" "$work/root/tests/baseline" \
  "$work/root/tests/conformance"
cp "$repo_root/src/compiler/parser.y" "$work/root/src/compiler/parser.y"
cp "$repo_root/src/compiler/absyn.h" "$work/root/src/compiler/absyn.h"
cp "$repo_root/src/bytecode/bytecode_abi.h" "$work/root/src/bytecode/bytecode_abi.h"
cp "$repo_root/src/bytecode/opcode_schema.def" "$work/root/src/bytecode/opcode_schema.def"
cp "$repo_root/src/libcall/libcall_list.h" "$work/root/src/libcall/libcall_list.h"
cp "$repo_root/tests/baseline/legacy_test_ledger.csv" \
  "$work/root/tests/baseline/legacy_test_ledger.csv"
cp "$repo_root/tests/conformance/test_conformance.c" \
  "$work/root/tests/conformance/test_conformance.c"
sed -i 's/^%left TAND$/%left/' "$work/root/src/compiler/parser.y"
expect_root_failure precedence_token 'language token inventory mismatch'

# Duplicate contract IDs are rejected before any canonical comparison.
sed -n '2p' "$work/catalog/contracts.csv" >>"$work/catalog/contracts.csv"
expect_failure duplicate_id 'duplicate contract_id'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Removing an actual opcode row detects stale/missing schema coverage.
python3 - "$work/catalog/bytecode.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
removed = next(row["contract_id"] for row in rows if row["canonical_id"] == "opcode.HALT")
rows = [row for row in rows if row["canonical_id"] != "opcode.HALT"]
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)

tests_path = path.with_name("tests.csv")
tests = list(csv.DictReader(tests_path.open(newline="", encoding="utf-8")))
for row in tests:
    row["contract_ids"] = ";".join(item for item in row["contract_ids"].split(";") if item != removed)
with tests_path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=tests[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(tests)
PY
expect_failure missing_opcode 'bytecode opcode inventory mismatch'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Exact opcode metadata includes validator, runtime handler, nested stack
# metadata, and control class, not only the encoded symbol.
python3 - "$work/catalog/bytecode.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
row = next(row for row in rows if row["canonical_id"] == "opcode.LIBCALL")
row["canonical_metadata"] = row["canonical_metadata"].replace(
    "validator=VALIDATE_LIBCALL_PAIR", "validator=VALIDATE_NONE").replace(
    "control_class=IR_CONTROL_STRAIGHT", "control_class=IR_CONTROL_CONDITIONAL")
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
PY
expect_failure opcode_metadata 'opcode LIBCALL has stale canonical metadata'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Libcall handler symbols are part of the permanent dispatch ABI metadata.
python3 - "$work/catalog/libcalls.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
row = next(row for row in rows if row["library"] == "sys" and row["call"] == "backup")
row["handler"] = "lc_sys_save"
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
PY
expect_failure libcall_handler 'libcall sys.backup has stale handler metadata'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Unknown test references are rejected even when the row shape is valid.
python3 - "$work/catalog/contracts.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
rows[0]["test_ids"] = "legacy.unknown.inventory-test"
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
PY
expect_failure unknown_test 'unknown test ID'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Removing one reverse edge proves tests.csv is a reciprocal index.
python3 - "$work/catalog/tests.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
row = next(row for row in rows if len(row["contract_ids"].split(";")) > 1)
row["contract_ids"] = ";".join(row["contract_ids"].split(";")[:-1])
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
PY
expect_failure reciprocal_edge 'one-sided contract edges'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Removing a maintained symbol proves archive accountability reconciliation.
python3 - "$work/catalog/archive_symbols.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
removed_index = next(index for index, row in enumerate(rows) if row["module"] != "application")
rows.pop(removed_index)
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)

PY
expect_failure missing_archive_symbol 'archive symbol catalog mismatch'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# An archive symbol may not resolve to an unknown grouped API contract.
python3 - "$work/catalog/archive_symbols.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
rows[0]["api_contract_id"] = "api.unknown.contract"
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
PY
expect_failure unknown_api_contract 'maps to unknown API contract'
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Attempt-2 API template text is rejected even when archive symbols remain complete.
python3 - "$work/catalog/api.csv" <<'PY'
import csv
import sys
from pathlib import Path
path = Path(sys.argv[1])
rows = list(csv.DictReader(path.open(newline="", encoding="utf-8")))
rows[0]["normal_behavior"] = "normal=runtime symbol returns or mutates its declared object"
with path.open("w", newline="", encoding="utf-8") as stream:
    writer = csv.DictWriter(stream, fieldnames=rows[0].keys(), lineterminator="\n")
    writer.writeheader()
    writer.writerows(rows)
PY
expect_failure api_template 'API catalog retains placeholder'

# Unknown archive object/module mappings fail directly without mutating files.
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$repo_root/tests/inventory" python3 - <<'PY'
import audit

try:
    audit.validate_archive_object_modules({"invented.o": "new-module"})
except audit.AuditError as error:
    assert "unmapped object" in str(error)
else:
    raise SystemExit("unknown archive object unexpectedly accepted")
print("[inventory-audit] unknown object mapping rejected")
PY

printf '[inventory-audit] positive and negative checks passed\n'
