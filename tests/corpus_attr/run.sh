#!/bin/sh
# Usage: tests/corpus_attr/run.sh <matiec-build-dir> [files...]
# Prints "<file> <result>" where result is ok / syntax_error (iec2c rc=1 with "error:" output)
# / CRASH(rc) (killed by a signal) / HANG (timeout 30s) / error(rc).
B=${1:?build dir}; shift
D=$(cd "$(dirname "$0")" && pwd)
[ $# -eq 0 ] && set -- "$D"/*.st
OUT=$(mktemp -d)
for f in "$@"; do
  timeout 30 "$B/iec2c" -I "$B/lib" -T "$OUT" "$f" >"$OUT/log" 2>&1; rc=$?
  if [ $rc -eq 124 ]; then r=HANG
  elif [ $rc -eq 0 ]; then r=ok
  elif [ $rc -gt 128 ]; then r="CRASH($rc)"
  elif [ $rc -eq 1 ] && grep -q "error:" "$OUT/log"; then r=syntax_error
  else r="error($rc)"; fi
  echo "$(basename "$f") $r"
done
rm -rf "$OUT"
