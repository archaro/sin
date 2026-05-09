#!/usr/bin/env bash
set -euo pipefail

# Gate for core compiler subsystems. This script must pass before merge when
# changes touch src/absyn.*, src/ir.*, or src/emitbc.*.

make test
./tests/test-compiler
