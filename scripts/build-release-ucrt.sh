#!/usr/bin/env bash
# Portable out-of-tree build. Run in an MSYS2 UCRT64 shell.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-${NIFSKOPE_BUILD_DIR:-$root/../nifskope-rc-build}}"
mkdir -p "$build"
build="$(cd "$build" && pwd)"
if [[ "$build" == "$root" ]]; then echo "Choose an out-of-tree build directory." >&2; exit 1; fi
source_win="$(cygpath -m "$root")"
gcc_path="$(command -v gcc)"
[[ "$gcc_path" == *.exe ]] || gcc_path="$gcc_path.exe"
gcc_win="$(cygpath -sm "$gcc_path")"
if [[ "$gcc_win" == *' '* || "$source_win" == *' '* ]]; then
    echo "Use source/toolchain paths without spaces (or enable Windows short paths)." >&2; exit 1
fi
cd "$build"
qmake6 "$root/NifSkope.pro" noavx2=1 -after \
    'QMAKE_PRE_LINK=' 'QMAKE_POST_LINK=' \
    "QMAKE_RC=windres --preprocessor=$gcc_win --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/libfo76utils/src" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/qhull/src" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/gli/gli" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/gli/external" \
    "QMAKE_CFLAGS+=-I$source_win/lib/qhull/src"
make -j"${NIFSKOPE_JOBS:-8}"
python "$root/scripts/deploy-release.py" "$(cygpath -m "$build")" "$(qmake6 -query QT_INSTALL_PREFIX)"
