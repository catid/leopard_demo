#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target coded_file_demo --parallel

mkdir -p build/file-demo

./build/coded_file_demo encode \
    README.md \
    build/file-demo/readme.leo \
    1 \
    1048576

./build/coded_file_demo decode \
    build/file-demo/readme.leo \
    build/file-demo/README.recovered.md \
    --erase-data 0

cmp README.md build/file-demo/README.recovered.md

echo "Demo OK: build/file-demo/README.recovered.md matches README.md"
