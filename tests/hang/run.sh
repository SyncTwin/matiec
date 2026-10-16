#!/bin/sh
# Regression tests for infinite loops in the parser/lexer error recovery.
# Every file in this directory contains a syntax error. The compiler must
# report it and terminate quickly with a non-zero exit code.
#
# Usage (from the top source directory, after building):
#   tests/hang/run.sh [path/to/iec2c] [timeout_in_seconds]

IEC2C=${1:-./iec2c}
TIMEOUT=${2:-5}
DIR=$(dirname "$0")
OUT=$(mktemp -d)
FAIL=0

for f in "$DIR"/*.st; do
  timeout "$TIMEOUT" "$IEC2C" -I lib -T "$OUT" "$f" > "$OUT/log" 2>&1
  rc=$?
  if [ $rc -eq 124 ]; then
    echo "FAIL (hang, killed after ${TIMEOUT}s): $f"; FAIL=1
  elif [ $rc -eq 0 ] || [ $rc -gt 128 ]; then
    echo "FAIL (exit code $rc): $f"; FAIL=1
  elif ! grep -q "error:" "$OUT/log"; then
    echo "FAIL (no error message): $f"; FAIL=1
  else
    echo "ok: $f"
  fi
done

rm -rf "$OUT"
exit $FAIL
