#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BASIKA="${1:-$ROOT_DIR/basika}"
FIXTURE="$ROOT_DIR/demo/basica.bas"

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
CACHE_ROOT="${BASIKA_PERF_CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/basika/performance}"
BASELINE="$CACHE_ROOT/basica-demo-v1-$MACHINE_KEY.seconds"

mkdir -p "$CACHE_ROOT"

OUTPUT="$("$BASIKA" "$FIXTURE" 2>&1)"
if ! grep -Fqx 'Final count: -5000000 ' <<<"$OUTPUT"; then
  printf 'BASICA performance demo returned an unexpected result:\n%s\n' "$OUTPUT" >&2
  exit 1
fi

declare -a TIMES=()
for run in 1 2 3 4 5; do
  TIME_OUTPUT="$({ /usr/bin/time -p "$BASIKA" "$FIXTURE" >/dev/null; } 2>&1)"
  ELAPSED="$(printf '%s\n' "$TIME_OUTPUT" | awk '$1 == "real" { print $2; exit }')"
  if [[ ! "$ELAPSED" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
    printf 'Could not read elapsed time for benchmark run %s:\n%s\n' "$run" "$TIME_OUTPUT" >&2
    exit 1
  fi
  TIMES+=("$ELAPSED")
done

MEDIAN="$(printf '%s\n' "${TIMES[@]}" | sort -n | sed -n '3p')"
if [[ ! "$MEDIAN" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  echo "Could not calculate benchmark median" >&2
  exit 1
fi

write_baseline() {
  local value="$1"
  local temporary="$BASELINE.tmp.$$"
  printf '%s\n' "$value" > "$temporary"
  mv "$temporary" "$BASELINE"
}

if [[ ! -f "$BASELINE" ]]; then
  write_baseline "$MEDIAN"
  printf 'Performance check PASS: no baseline existed; stored %.3f seconds for this machine.\n' "$MEDIAN"
  exit 0
fi

BASELINE_SECONDS="$(cat "$BASELINE")"
if [[ ! "$BASELINE_SECONDS" =~ ^[0-9]+([.][0-9]+)?$ ]]; then
  printf 'Invalid performance baseline at %s\n' "$BASELINE" >&2
  exit 1
fi

if awk -v current="$MEDIAN" -v baseline="$BASELINE_SECONDS" \
    'BEGIN { exit !(baseline > 0 && current > baseline * 1.10) }'; then
  printf 'Performance check FAIL: median %.3f seconds is more than 10%% slower than baseline %.3f seconds.\n' \
    "$MEDIAN" "$BASELINE_SECONDS" >&2
  exit 1
fi

if awk -v current="$MEDIAN" -v baseline="$BASELINE_SECONDS" \
    'BEGIN { exit !(current < baseline) }'; then
  write_baseline "$MEDIAN"
fi
printf 'Performance check PASS: median %.3f seconds; machine baseline %.3f seconds (10%% regression limit).\n' \
  "$MEDIAN" "$(cat "$BASELINE")"
