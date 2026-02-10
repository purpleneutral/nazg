#!/usr/bin/env sh
set -eu
cmake -S . -B build -G "Ninja" 2>/dev/null || cmake -S . -B build
cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
