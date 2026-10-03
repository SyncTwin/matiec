#!/usr/bin/env bash
# Compare the WebAssembly iec2json (wasm/, built by build_wasm.sh) against a
# native iec2json byte for byte: stdout and exit code.
#   tests/wasm_compare.sh <native-iec2json> file.st [...]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
NATIVE=$1; shift
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
pass=0; fail=0
for f in "$@"; do
  (cd "$(dirname "$f")" && "$NATIVE" -I "$ROOT/lib" "$(basename "$f")" > "$TMP/n" 2>/dev/null); n=$?
  node "$ROOT/wasm_demo.mjs" "$f" > "$TMP/w" 2>/dev/null; w=$?
  if [ $n = $w ] && cmp -s "$TMP/n" "$TMP/w"; then pass=$((pass+1)); else fail=$((fail+1)); echo "DIFF $f (exit native=$n wasm=$w)"; fi
done
echo "same: $pass, different: $fail"
[ $fail = 0 ]
