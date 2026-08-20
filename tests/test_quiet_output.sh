#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/.." && pwd -P)
runner=$repo_root/tests/shared/quiet_runner.sh
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

cat >"$work/success.sh" <<'EOF'
#!/usr/bin/env bash
printf 'successful chatter that must be hidden\n'
printf '[fixture] totals: ran=2 passed=2 failed=0 skipped=0 status=SUCCESS\n'
EOF
cat >"$work/failure.sh" <<'EOF'
#!/usr/bin/env bash
printf 'failure stdout detail\n'
printf 'failure stderr diagnostic\n' >&2
printf '[fixture] totals: ran=1 passed=0 failed=1 skipped=2 status=FAILURE\n' >&2
exit 7
EOF
cat >"$work/abnormal.sh" <<'EOF'
#!/usr/bin/env bash
printf 'abnormal exit diagnostic\n' >&2
exit 9
EOF
cat >"$work/build-tool.sh" <<'EOF'
#!/usr/bin/env bash
printf 'compiler command stdout\n'
printf 'compiler warning stderr\n' >&2
EOF
cat >"$work/Makefile" <<EOF
.PHONY: all build fail
all: build
	@'$runner' aggregate fixture-make "'$runner' run fixture-test -- '$work/success.sh'"
build:
	'$work/build-tool.sh'
fail:
	@'$runner' aggregate fixture-make-failure "'$runner' run fixture-failure -- '$work/failure.sh'"
EOF
chmod +x "$work/success.sh" "$work/failure.sh" "$work/abnormal.sh" "$work/build-tool.sh"

success_capture() {
  "$runner" run fixture-success -- "$work/success.sh" >"$work/success.out" 2>&1
  grep -q 'ran=2 passed=2 failed=0.*status=SUCCESS' "$work/success.out"
  ! grep -q 'successful chatter' "$work/success.out"
}

one_wrapper() {
  "$runner" one fixture-one -- "$work/success.sh" >"$work/one.out" 2>&1
  grep -q 'ran=1 passed=1 failed=0 skipped=0 status=SUCCESS' "$work/one.out"
  ! grep -q 'successful chatter' "$work/one.out"
}

controlled_failure() {
  if "$runner" run fixture-failure -- "$work/failure.sh" >"$work/failure.out" 2>&1; then
    printf 'quiet runner unexpectedly accepted controlled failure\n' >&2
    return 1
  fi
  grep -q 'failure stdout detail' "$work/failure.out"
  grep -q 'failure stderr diagnostic' "$work/failure.out"
  grep -q 'ran=1 passed=0 failed=1 skipped=2 status=FAILURE' "$work/failure.out"
  test "$(tail -n 1 "$work/failure.out")" = "[fixture] totals: ran=1 passed=0 failed=1 skipped=2 status=FAILURE"
}

abnormal_exit() {
  if "$runner" run fixture-abnormal -- "$work/abnormal.sh" >"$work/abnormal.out" 2>&1; then
    printf 'quiet runner unexpectedly accepted abnormal exit\n' >&2
    return 1
  fi
  grep -q 'abnormal exit diagnostic' "$work/abnormal.out"
  grep -q 'status=FAILURE exit=9' "$work/abnormal.out"
  test "$(tail -n 1 "$work/abnormal.out")" = "[fixture-abnormal] totals: ran=1 passed=0 failed=1 skipped=0 status=FAILURE exit=9"
}

make_success() {
  make -f "$work/Makefile" -C "$work" all >"$work/make-output.out" 2>&1
  grep -q "'$work/build-tool.sh'" "$work/make-output.out"
  grep -q 'compiler command stdout' "$work/make-output.out"
  grep -q 'compiler warning stderr' "$work/make-output.out"
  ! grep -q 'successful chatter' "$work/make-output.out"
  ! grep -q 'fixture-test.*totals' "$work/make-output.out"
  test "$(grep 'fixture-make.*totals:' "$work/make-output.out" | tail -n 1)" = "[fixture-make] totals: ran=2 passed=2 failed=0 skipped=0 status=SUCCESS [PASS]"
}

make_failure() {
  if make -f "$work/Makefile" -C "$work" fail >"$work/make-failure.out" 2>&1; then
    printf 'Make failure fixture unexpectedly passed\n' >&2
    return 1
  fi
  grep -q 'failure stdout detail' "$work/make-failure.out"
  grep -q 'failure stderr diagnostic' "$work/make-failure.out"
  test "$(grep 'fixture-make-failure.*totals:' "$work/make-failure.out" | tail -n 1)" = "[fixture-make-failure] totals: ran=1 passed=0 failed=1 skipped=2 status=FAILURE [FAIL]"
}

aggregate_success() {
  "$runner" aggregate fixture-aggregate \
    "printf '[a] totals: ran=2 passed=2 failed=0 skipped=0 status=SUCCESS\\n'" \
    "printf '[b] totals: ran=3 passed=3 failed=0 skipped=0 status=SUCCESS\\n'" \
    >"$work/aggregate.out" 2>&1
  grep -q 'ran=5 passed=5 failed=0 skipped=0 status=SUCCESS' "$work/aggregate.out"
  grep -q '\[fixture-aggregate\].*status=SUCCESS \[PASS\]' "$work/aggregate.out"
  test "$(tail -n 1 "$work/aggregate.out")" = "[fixture-aggregate] totals: ran=5 passed=5 failed=0 skipped=0 status=SUCCESS [PASS]"
}

aggregate_failure() {
  if "$runner" aggregate fixture-aggregate-failure \
      "'$runner' run nested-failure -- '$work/failure.sh'" \
      "printf '[after] totals: ran=3 passed=3 failed=0 skipped=0 status=SUCCESS\\n'" \
      >"$work/aggregate-failure.out" 2>&1; then
    printf 'quiet aggregate unexpectedly accepted controlled failure\n' >&2
    return 1
  fi
  grep -q 'failure stderr diagnostic' "$work/aggregate-failure.out"
  grep -q 'ran=4 passed=3 failed=1 skipped=2 status=FAILURE' "$work/aggregate-failure.out"
  grep -q '\[fixture-aggregate-failure\].*status=FAILURE \[FAIL\]' "$work/aggregate-failure.out"
  test "$(grep 'fixture-aggregate-failure.*totals:' "$work/aggregate-failure.out" | tail -n 1)" = "[fixture-aggregate-failure] totals: ran=4 passed=3 failed=1 skipped=2 status=FAILURE [FAIL]"
}

run_named_case() {
  case "$1" in
    success_capture) success_capture ;;
    one_wrapper) one_wrapper ;;
    controlled_failure) controlled_failure ;;
    abnormal_exit) abnormal_exit ;;
    make_success) make_success ;;
    make_failure) make_failure ;;
    aggregate_success) aggregate_success ;;
    aggregate_failure) aggregate_failure ;;
    *) printf 'unknown quiet-output case: %s\n' "$1" >&2; return 2 ;;
  esac
}

if [ "$#" -eq 1 ]; then
  run_named_case "$1"
  exit $?
fi
[ "$#" -eq 0 ] || { printf 'usage: %s [CASE]\n' "$0" >&2; exit 2; }
success_capture
one_wrapper
controlled_failure
abnormal_exit
make_success
make_failure
aggregate_success
aggregate_failure
printf '[quiet-output] totals: ran=8 passed=8 failed=0 skipped=0 status=SUCCESS [PASS]\n'
