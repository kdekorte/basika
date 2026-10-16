#!/usr/bin/env bash
set -euo pipefail

# A check that basika has not become much slower: tests/performance/
# benchmark.bas runs the interpreter's hot paths for about two seconds, and
# the median of three runs must be within 10% of a baseline kept for this
# machine outside the repository. The benchmark also writes a checksum,
# which must match, so a wrong answer fails too.
#
# The baseline is the median of the first run on the machine and stays put;
# set BASIKA_PERF_UPDATE_BASELINE=1 to record the current time instead (after
# a real speedup, or when the machine has changed).
#
# Usage: performance_guard.sh [path/to/basika]

ROOT_DIR="$(cd "$(dirname "$0")/../.." && pwd)"
BASIKA="${1:-$ROOT_DIR/basika}"
PROGRAM=tests/performance/benchmark.bas
RESULT=tests/benchmark.result
EXPECTED="Count: 3000000 Checksum: 46857 Pixels: 960767 "
NAME=benchmark-v1

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
BASELINE="$CACHE_ROOT/$NAME-$MACHINE_KEY.seconds"
mkdir -p "$CACHE_ROOT"

fail() {
  printf 'Performance check FAIL: %s\n' "$1" >&2
  rm -f "$RESULT"
  exit 1
}

# One untimed run first, so the timed ones do not include a cold start.
"$BASIKA" --headless -x "$PROGRAM" </dev/null >/dev/null 2>&1 || fail "$PROGRAM exited with an error"

times=()
for run in 1 2 3; do
  rm -f "$RESULT"
  # Wall-clock seconds to the microsecond (/usr/bin/time -p only has
  # hundredths); basika's own output is discarded.
  elapsed="$(perl -MTime::HiRes=time -e '
    my $start = time;
    my $pid = fork() // exit 1;
    if ($pid == 0) {
      open(STDOUT, ">", "/dev/null"); open(STDERR, ">", "/dev/null"); open(STDIN, "<", "/dev/null");
      exec(@ARGV) or exit 127;
    }
    waitpid($pid, 0);
    exit 1 if $?;
    printf "%.4f\n", time - $start;' "$BASIKA" --headless -x "$PROGRAM")" ||
    fail "$PROGRAM exited with an error on run $run"
  result="$(cat "$RESULT" 2>/dev/null || true)"
  [[ "$result" == "$EXPECTED" ]] ||
    fail "$PROGRAM gave the wrong result on run $run: '$result' (expected '$EXPECTED')"
  times+=("$elapsed")
done
rm -f "$RESULT"
median="$(printf '%s\n' "${times[@]}" | sort -n | sed -n '2p')"

if [[ ! -f "$BASELINE" || "${BASIKA_PERF_UPDATE_BASELINE:-0}" == "1" ]]; then
  printf '%s\n' "$median" > "$BASELINE.tmp.$$"
  mv "$BASELINE.tmp.$$" "$BASELINE"
  printf 'Performance check PASS: stored a baseline of %.3f seconds for this machine.\n' "$median"
  exit 0
fi

base="$(cat "$BASELINE")"
[[ "$base" =~ ^[0-9]+([.][0-9]+)?$ ]] || fail "invalid baseline in $BASELINE"
if awk -v current="$median" -v base="$base" 'BEGIN { exit !(current > base * 1.10) }'; then
  fail "$(printf 'median %.3f seconds is more than 10%% slower than the baseline of %.3f seconds.' "$median" "$base")"
fi
printf 'Performance check PASS: median %.3f seconds; baseline %.3f seconds (10%% limit).\n' "$median" "$base"
