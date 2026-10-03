#!/usr/bin/env bash
# Build iec2json and iec2c as WebAssembly ES6 modules (emscripten).
#
# Output (in $OUT_DIR, default ./wasm):
#   iec2json.mjs + iec2json.wasm
#   iec2c.mjs    + iec2c.wasm
#
# The standard library (lib/*.txt) is embedded into the .wasm under /lib,
# so callers can run e.g. callMain(['-I', '/lib', 'file.st']).
#
# --embed-file vs --preload-file: both carry the same 172 KB of lib/*.txt.
# --preload-file emits a separate .data file plus loader code in the .mjs
# that fetches it (an extra file and HTTP request, and locateFile tweaks when
# bundling). --embed-file stores the bytes in a wasm data segment. Measured
# for iec2json at -O2 (bytes, raw / gzip -9):
#   embed:   .wasm 1156301/281276 + .mjs 64735/18314           = 1221036/299590
#   preload: .wasm  983741/266716 + .mjs 68702/19507 + .data 172075/13853
#                                                               = 1224518/300076
# Embedding is slightly smaller both raw and gzipped and ships as two files,
# so we embed.
#
# Requirements: emsdk activated (or EMSDK pointing at it), host bison+flex,
# autotools. bison/flex run on the host and generate C++ that em++ compiles.
#
# Usage: ./build_wasm.sh [build_dir] ; env OUT_DIR, OPT (default -O2), JOBS
set -e

SRC_DIR=$(cd "$(dirname "$0")" && pwd)
BUILD_DIR=${1:-$SRC_DIR/build-wasm}
OUT_DIR=${OUT_DIR:-$SRC_DIR/wasm}
OPT=${OPT:--O2}
JOBS=${JOBS:-$(nproc 2>/dev/null || echo 4)}

if ! command -v emcc >/dev/null 2>&1; then
  for d in "$EMSDK" "$SRC_DIR/../emsdk" "$HOME/emsdk"; do
    if [ -n "$d" ] && [ -f "$d/emsdk_env.sh" ]; then
      . "$d/emsdk_env.sh" >/dev/null 2>&1
      break
    fi
  done
fi
command -v emcc >/dev/null 2>&1 || { echo "emcc not found: activate emsdk first" >&2; exit 1; }

[ -x "$SRC_DIR/configure" ] || (cd "$SRC_DIR" && autoreconf -i)

mkdir -p "$BUILD_DIR" "$OUT_DIR"
cd "$BUILD_DIR"
if [ ! -f Makefile ]; then
  # -g0 overrides the -g from common.mk (DWARF blocks binaryen optimizations).
  emconfigure "$SRC_DIR/configure" CXXFLAGS="$OPT -g0"
fi
# Only the static libraries and main.o are needed; the final link is ours.
emmake make -j"$JOBS"

# Stage only the IEC library sources (lib/C holds C runtime headers that the
# compiler itself never reads).
STAGE="$BUILD_DIR/wasm-lib"
rm -rf "$STAGE" && mkdir -p "$STAGE"
cp "$SRC_DIR"/lib/*.txt "$STAGE"/

LIBS_COMMON="stage1_2/libstage1_2.a stage3/libstage3.a absyntax/libabsyntax.a absyntax_utils/libabsyntax_utils.a"

LDFLAGS="$OPT -g0 \
  -sMODULARIZE=1 -sEXPORT_ES6=1 \
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=1 \
  -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=4MB \
  -sFORCE_FILESYSTEM=1 \
  -sENVIRONMENT=web,worker,node \
  -sEXPORTED_RUNTIME_METHODS=FS,callMain \
  --embed-file $STAGE@/lib"

link() { # name stage4-lib export-name
  em++ -o "$OUT_DIR/$1.mjs" main.o $2 $LIBS_COMMON $LDFLAGS -sEXPORT_NAME=$3
}

link iec2json stage4/generate_json/libstage4_json.a createIec2json
link iec2c    stage4/generate_c/libstage4_c.a       createIec2c

for f in "$OUT_DIR"/*.wasm; do
  printf '%s raw=%s gzip=%s\n' "$(basename "$f")" \
    "$(wc -c < "$f")" "$(gzip -9 -c "$f" | wc -c)"
done
