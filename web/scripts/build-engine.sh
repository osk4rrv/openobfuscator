#!/usr/bin/env bash
set -euo pipefail

if ! command -v em++ >/dev/null 2>&1; then
  echo "Emscripten (em++) is required to build the browser engine." >&2
  exit 1
fi

mkdir -p public/engine
em++ ../src/luaobf.cpp ../src/wasm.cpp \
  -I../src \
  -std=c++17 \
  -O3 \
  -fexceptions \
  -sWASM=1 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=createOpenObfuscator \
  -sENVIRONMENT=web,worker,node \
  -sALLOW_MEMORY_GROWTH=1 \
  -sFILESYSTEM=0 \
  -sDISABLE_EXCEPTION_CATCHING=0 \
  -sEXPORTED_FUNCTIONS='["_oo_obfuscate","_oo_last_error"]' \
  -sEXPORTED_RUNTIME_METHODS='["ccall"]' \
  -o public/engine/openobfuscator.js
