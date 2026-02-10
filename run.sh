#!/usr/bin/env sh
set -eu
BIN=$(awk '/add_executable\(/ {print $2; exit}' CMakeLists.txt)
[ -n "$BIN" ] || BIN=my_cpp_app
exec ./build/$BIN
