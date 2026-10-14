#!/bin/sh
# The browser build's automated checks: what runs without the game's data, without a GPU and without controllers.
# Used by .github/workflows/browser.yml, and runnable by hand on any machine with the pinned toolchain on PATH
# (docs/BROWSER_BUILD.md): emcc (Emscripten 6.0.12) and its LLVM tools (emsdk's upstream/bin: llvm-objdump, llvm-nm),
# node, python with libclang 18.1.1, naga 30.0.1 (BT3_NAGA).
#   sh port/tests/ci/browser_checks.sh
# Builds from the checkout as it is (the build's own folders under port/build), then runs the data-free tests. Stops at
# the first failure. Nothing here reads, needs or produces game data: the data tables are built as shapes only
# (port/tools/wasm_data.py); the values come from the user's disc at run time.
set -eu
cd "$(dirname "$0")/../../.."

step() { printf '\n==== %s\n' "$1"; }

step "versions"
emcc --version | head -n 1
for tool in llvm-objdump llvm-nm; do
    command -v "$tool" >/dev/null || { echo "$tool is not on PATH (emsdk's upstream/bin): the build needs it"; exit 1; }
done
llvm-objdump --version | head -n 2 | tail -n 1
node --version
python --version
python -c "import clang.cindex as c; print('libclang (Python):', c.__file__)"
"${BT3_NAGA:?BT3_NAGA must name the naga 30.0.1 executable}" --version

step "build: game sources"
python port/tools/wasm_build.py game
step "build: the port's code"
python port/tools/wasm_build.py port
step "build: data-table shapes"
python port/tools/wasm_build.py data
step "build: link"
python port/tools/wasm_build.py link

step "test: software float (port/tests/wasm_sf)"
python port/tests/wasm_sf/run.py
step "test: pipeline keys (port/tests/pipekey)"
python port/tests/pipekey/run.py
step "test: data loader (port/tests/web_data)"
node port/tests/web_data/test_data.js
step "test: memory layout (port/tests/wasm_mem)"
python port/tests/wasm_mem/build.py

printf '\nall automated browser checks passed\n'
