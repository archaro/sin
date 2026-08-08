#!/usr/bin/env bash
set -u

usage() {
  printf 'usage: %s run LABEL -- COMMAND...\n' "$0" >&2
  printf '       %s one LABEL -- COMMAND...\n' "$0" >&2
  printf '       %s report LABEL -- COMMAND...\n' "$0" >&2
  printf '       %s aggregate LABEL COMMAND_STRING...\n' "$0" >&2
  exit 2
}

summary_line() {
  awk '/totals:.*ran=[0-9]+.*passed=[0-9]+.*failed=[0-9]+.*status=(SUCCESS|FAILURE)/ { line=$0 } END { print line }' "$1"
}

summary_field() {
  printf '%s\n' "$1" | sed -n "s/.*$2=\([0-9][0-9]*\).*/\1/p"
}

run_captured() {
  local mode=$1 label=$2
  shift 2
  [ "${1:-}" = -- ] || usage
  shift
  [ "$#" -gt 0 ] || usage

  local work stdout_file stderr_file combined_file rc summary
  work=$(mktemp -d) || exit 2
  stdout_file=$work/stdout
  stderr_file=$work/stderr
  combined_file=$work/combined
  trap 'rm -rf "$work"' RETURN

  "$@" >"$stdout_file" 2>"$stderr_file"
  rc=$?
  cat "$stdout_file" "$stderr_file" >"$combined_file"
  summary=$(summary_line "$combined_file")

  if [ "$rc" -eq 0 ]; then
    if [ "$mode" = one ]; then
      printf '[%s] totals: ran=1 passed=1 failed=0 skipped=0 status=SUCCESS\n' "$label"
      return 0
    fi
    if [ -n "$summary" ] && [[ "$summary" == *status=SUCCESS* ]]; then
      if [ "$mode" = report ] || [ "${SIN_BENCH_REPORT:-0}" = 1 ]; then
        awk '!/totals:.*ran=[0-9]+.*passed=[0-9]+.*failed=[0-9]+.*status=(SUCCESS|FAILURE)/' \
          "$stdout_file"
        awk '!/totals:.*ran=[0-9]+.*passed=[0-9]+.*failed=[0-9]+.*status=(SUCCESS|FAILURE)/' \
          "$stderr_file" >&2
      fi
      printf '%s\n' "$summary"
      return 0
    fi
    printf '[%s][FAIL] command exited successfully without a SUCCESS totals line\n' "$label" >&2
  else
    printf '[%s][FAIL] command exited with status %d\n' "$label" "$rc" >&2
  fi

  if [ -s "$stdout_file" ]; then
    printf '[%s] captured stdout:\n' "$label" >&2
    awk '!/totals:.*ran=[0-9]+.*passed=[0-9]+.*failed=[0-9]+.*status=(SUCCESS|FAILURE)/' "$stdout_file" >&2
  fi
  if [ -s "$stderr_file" ]; then
    printf '[%s] captured stderr:\n' "$label" >&2
    awk '!/totals:.*ran=[0-9]+.*passed=[0-9]+.*failed=[0-9]+.*status=(SUCCESS|FAILURE)/' "$stderr_file" >&2
  fi
  if [ -n "$summary" ] && [[ "$summary" == *status=FAILURE* ]]; then
    printf '%s\n' "$summary" >&2
  else
    printf '[%s] totals: ran=1 passed=0 failed=1 skipped=0 status=FAILURE exit=%d\n' "$label" "$rc" >&2
  fi
  if [ "$rc" -eq 0 ]; then
    return 1
  fi
  return "$rc"
}

aggregate() {
  local label=$1
  shift
  [ "$#" -gt 0 ] || usage

  local work index=0 total_ran=0 total_passed=0 total_failed=0 total_skipped=0
  local command log rc summary ran passed failed skipped status
  work=$(mktemp -d) || exit 2
  trap 'rm -rf "$work"' RETURN

  for command in "$@"; do
    index=$((index + 1))
    log=$work/child-$index
    bash -c "$command" >"$log" 2>&1
    rc=$?
    summary=$(summary_line "$log")
    if [ -n "$summary" ]; then
      ran=$(summary_field "$summary" ran)
      passed=$(summary_field "$summary" passed)
      failed=$(summary_field "$summary" failed)
      skipped=$(summary_field "$summary" skipped)
      skipped=${skipped:-0}
      status=${summary##*status=}
      status=${status%% *}
    else
      ran=1
      passed=0
      failed=1
      skipped=0
      status=FAILURE
    fi
    total_ran=$((total_ran + ran))
    total_passed=$((total_passed + passed))
    total_failed=$((total_failed + failed))
    total_skipped=$((total_skipped + skipped))
    if [ "$rc" -ne 0 ] || [ "$status" != SUCCESS ] || [ "$failed" -ne 0 ]; then
      cat "$log" >&2
      if [ -z "$summary" ]; then
        printf '[%s-child-%d] totals: ran=1 passed=0 failed=1 skipped=0 status=FAILURE exit=%d\n' \
          "$label" "$index" "$rc" >&2
      fi
      if [ "$failed" -eq 0 ]; then
        total_failed=$((total_failed + 1))
        total_passed=$((total_passed > 0 ? total_passed - 1 : 0))
      fi
    else
      awk '!/totals:.*ran=[0-9]+.*passed=[0-9]+.*failed=[0-9]+.*status=(SUCCESS|FAILURE)/' \
        "$log"
    fi
  done

  if [ "$total_failed" -ne 0 ]; then
    printf '[%s] totals: ran=%d passed=%d failed=%d skipped=%d status=FAILURE [FAIL]\n' \
      "$label" "$total_ran" "$total_passed" "$total_failed" "$total_skipped" >&2
    return 1
  fi
  printf '[%s] totals: ran=%d passed=%d failed=0 skipped=%d status=SUCCESS [PASS]\n' \
    "$label" "$total_ran" "$total_passed" "$total_skipped"
}

case ${1:-} in
  run|one|report)
    mode=$1
    shift
    [ "$#" -ge 2 ] || usage
    label=$1
    shift
    run_captured "$mode" "$label" "$@"
    ;;
  aggregate)
    shift
    [ "$#" -ge 2 ] || usage
    label=$1
    shift
    aggregate "$label" "$@"
    ;;
  *) usage ;;
esac
