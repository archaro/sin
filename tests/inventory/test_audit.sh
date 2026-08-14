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

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
cp -a "$repo_root/tests/inventory/." "$work/catalog"

expect_failure() {
  local label=$1
  shift
  local output=$work/$label.out
  if PYTHONDONTWRITEBYTECODE=1 python3 "$audit" --catalog-dir "$work/catalog" --archive "$archive" >"$output" 2>&1; then
    printf 'audit negative self-test unexpectedly passed: %s\n' "$label" >&2
    exit 1
  fi
  grep -q 'inventory audit: ERROR:' "$output"
}

# Duplicate contract IDs are rejected before any canonical comparison.
sed -n '2p' "$work/catalog/contracts.csv" >>"$work/catalog/contracts.csv"
expect_failure duplicate_id
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Removing one canonical opcode row detects stale/missing schema coverage.
sed -i '2d' "$work/catalog/bytecode.csv"
expect_failure missing_opcode
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
expect_failure unknown_test
cp -a "$repo_root/tests/inventory/." "$work/catalog"

# Removing a maintained symbol proves archive/API reconciliation.
sed -i '2d' "$work/catalog/api.csv"
expect_failure missing_api_symbol

printf '[inventory-audit] positive and negative checks passed\n'
