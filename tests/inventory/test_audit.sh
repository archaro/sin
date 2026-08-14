#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/../.." && pwd -P)
audit=$repo_root/tests/inventory/audit.py
archive=${1:-}
if [ -z "$archive" ]; then
  archive=$(find "$repo_root/lib" -name libsinshared.a -print -quit)
fi
[ -n "$archive" ] && [ -f "$archive" ]

PYTHONDONTWRITEBYTECODE=1 python3 "$audit" --archive "$archive" >/dev/null
PYTHONDONTWRITEBYTECODE=1 PYTHONPATH="$repo_root/tests/inventory" python3 - <<'PY'
import audit

_, _, _, ir, _, _ = audit.source_entries(audit.ROOT)
assert "IR_OP_PUSH_INT" in ir
assert "IR_OP_IR_OP_PUSH_INT" not in ir
print("[inventory-audit] exact IR extraction passed")
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
rows[0]["contract_ids"] = ";".join(rows[0]["contract_ids"].split(";")[:-1])
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
