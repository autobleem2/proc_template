#!/usr/bin/env bash
# the Windows build (MSYS2 UCRT64: gcc, cmake, ninja) into build_win/, the self-test, and the processor folder
# as it goes onto a stick - dist/proc_template/ with bin/windows-x86_64/proc_template.exe
set -e
cd "$(dirname "$0")"
mkdir -p build_win
cmake -S . -B build_win -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build_win
(cd build_win && ctest --output-on-failure)
rm -rf dist/proc_template
mkdir -p dist/proc_template/bin/windows-x86_64
cp package/processor.ini LICENSE dist/proc_template/
cp build_win/proc_template.exe dist/proc_template/bin/windows-x86_64/
echo "dist/proc_template is ready: copy it to <stick>/System/Processors/proc_template"
