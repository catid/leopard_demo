#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target coded_file_demo --parallel

mkdir -p build/file-demo

./build/coded_file_demo encode \
    examples/demo_video.mp4 \
    build/file-demo/demo_video.leo \
    2 \
    1048576

./build/coded_file_demo decode \
    build/file-demo/demo_video.leo \
    build/file-demo/demo_video.recovered.mp4 \
    --erase-data 0

cmp examples/demo_video.mp4 build/file-demo/demo_video.recovered.mp4

./build/coded_file_demo fuzz 10000 8 4 4096 12345

echo "Demo OK: build/file-demo/demo_video.recovered.mp4 matches examples/demo_video.mp4"
