#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${NIFSKOPE_BUILD_DIR:-/c/UTILIT~1/NifSkopeBuildTools/nifskope-resource-fix}"
g++ -std=c++20 -O2 -Wall -Wextra -Werror -I"$root/src" "$root/src/geometry/vanillaplus.cpp" "$root/src/geometry/sculpt.cpp" "$root/tests/sculpt_test.cpp" -o "$build/release/sculpt-core-test.exe"
"$build/release/sculpt-core-test.exe"
