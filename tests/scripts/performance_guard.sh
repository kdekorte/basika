#!/usr/bin/env bash
set -euo pipefail

# Times each benchmark program (the median of five runs) against a baseline
# kept for this machine outside the repository; more than 10% slower fails.
#
# Usage: performance_guard.sh [--window] [path/to/basika]
#   --window runs the graphics benchmarks in a window instead of headless.
#   GPU readbacks and window presents only cost time there, but the times
#   follow the display's refresh rate, so make test runs the headless set.

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
WINDOW=0
if [[ "${1:-}" == "--window" ]]; then
  WINDOW=1
  shift
fi
BASIKA="${1:-$ROOT_DIR/basika}"

cd "$ROOT_DIR"

case "$(uname -s)" in
  Darwin)
    MACHINE="$(uname -s)-$(uname -r)-$(uname -m)-$(sysctl -n machdep.cpu.brand_string 2>/dev/null || uname -m)"
    ;;
  Linux)
    CPU_MODEL="$(awk -F: '/^model name[[:space:]]*:/ { sub(/^[^:]*:[[:space:]]*/, ""); print; exit }' /proc/cpuinfo 2>/dev/null || uname -m)"
    MACHINE="$(uname -s)-$(uname -r)-$(uname -m)-$CPU_MODEL"
    ;;
  *)
    MACHINE="$(uname -s)-$(uname -r)-$(uname -m)"
    ;;
esac
MACHINE_KEY="$(printf '%s' "$MACHINE" | tr -cs '[:alnum:]._-' '_')"
MACHINE_KEY="$(printf '%s' "$MACHINE" | tr -cs '[:alnum:]._-' '_')"
CACHE_ROOT="${BASIKA_PERF_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/basika/performance}"

mkdir -p "$CACHE_ROOT"

# name|program|basika options|a line the output must contain (empty: none)
if (( WINDOW )); then
  FIXTURES=(
    "point-window-v1|tests/performance/point.bas|-x|"
    "paint-window-v1|tests/performance/paint.bas|-x|"
    "display-window-v1|tests/performance/display.bas|-x|"
  )
else
  FIXTURES=(
    "basica-demo-v1|demo/basica.bas||Final count: -5000000 "
    "expressions-v1|tests/performance/expressions.bas|-x|Checksum: 853476 "
    "point-v1|tests/performance/point.bas|--headless -x|"
    "paint-v1|tests/performance/paint.bas|--headless -x|"
  )
fi

write_baseline() {
  local baseline="$1" value="$2"
  local temporary="$baseline.tmp.$$"
  printf '%s\n' "$value" > "$temporary"
  mv "$temporary" "$baseline"
}

# check_fixture name program options expected
check_fixture() {
  local name="$1" program="$2" options="$3" expected="$4"
  local baseline="$CACHE_ROOT/$name-$MACHINE_KEY.seconds"
  local output
  # $options is split into separate arguments on purpose.
  # shellcheck disable=SC2086
  if ! output="$("$BASIKA" $options "$program" 2>&1)"; then
    printf 'Performance check FAIL (%s): %s exited with an error:\n%s\n' "$name" "$program" "$output" >&2
    return 1
  fi
  if [[ -n "$expected" ]] && ! grep -Fqx "$expected" <<<"$output"; then
    printf 'Performance check FAIL (%s): %s returned an unexpected result:\n%s\n' "$name" "$program" "$output" >&2
    return 1
  fi

  local -a times=()
  local run time_output elapsed
  for run in 1 2 3 4 5; do
    # shellcheck disable=SC2086
    time_output="$({ /usr/bin/time -p "$BASIKA" $options "$program" >/dev/null; } 2>&1)"
    elapsed="$(printf '%s\n' "$time_output" | awk '$1 == "real" { print $2; exit }')"
    if [[ ! "$elapsed" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
      printf 'Could not read elapsed time for %s run %s:\n%s\n' "$name" "$run" "$time_output" >&2
      return 1
    fi
    times+=("$elapsed")
  done

  local median
  median="$(printf '%s\n' "${times[@]}" | sort -n | sed -n '3p')"
  if [[ ! "$median" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    echo "Could not calculate the $name median" >&2
    return 1
  fi

  if [[ ! -f "$baseline" ]]; then
    write_baseline "$baseline" "$median"
    printf 'Performance check PASS (%s): no baseline existed; stored %.3f seconds for this machine.\n' "$name" "$median"
    return 0
  fi

  local baseline_seconds
  baseline_seconds="$(cat "$baseline")"
  if [[ ! "$baseline_seconds" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    printf 'Invalid performance baseline at %s\n' "$baseline" >&2
    return 1
  fi

  if awk -v current="$median" -v base="$baseline_seconds" \
      'BEGIN { exit !(base > 0 && current > base * 1.10) }'; then
    printf 'Performance check FAIL (%s): median %.3f seconds is more than 10%% slower than baseline %.3f seconds.\n' \
      "$name" "$median" "$baseline_seconds" >&2
    return 1
  fi

  if awk -v current="$median" -v base="$baseline_seconds" 'BEGIN { exit !(current < base) }'; then
    write_baseline "$baseline" "$median"
  fi
  printf 'Performance check PASS (%s): median %.3f seconds; machine baseline %.3f seconds (10%% regression limit).\n' \
    "$name" "$median" "$(cat "$baseline")"
}

# Every benchmark runs, so one report shows all that regressed.
STATUS=0
for fixture in "${FIXTURES[@]}"; do
  IFS='|' read -r name program options expected <<<"$fixture"
  check_fixture "$name" "$program" "$options" "$expected" || STATUS=1
done
exit "$STATUS"
