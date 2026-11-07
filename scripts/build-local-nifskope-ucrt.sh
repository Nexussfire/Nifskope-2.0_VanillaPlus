#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
tools_root="/c/UTILIT~1/NifSkopeBuildTools"
build_dir="${NIFSKOPE_BUILD_DIR:-$tools_root/nifskope-ucrt}"
mkdir -p "$build_dir"
cd "$build_dir"
source_win="$(cygpath -m "$root")"

# The original pre-link step rewrites the source README, and its post-link
# step removes and replaces a resource directory. Deploy resources separately.
qmake6 "$root/NifSkope.pro" noavx2=1 -after \
    'QMAKE_PRE_LINK=' 'QMAKE_POST_LINK=' \
    'QMAKE_RC=windres --preprocessor=C:/UTILIT~1/NifSkopeBuildTools/msys64/ucrt64/bin/gcc.exe --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED' \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/libfo76utils/src" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/qhull/src" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/gli/gli" \
    "QMAKE_CXXFLAGS+=-I$source_win/lib/gli/external" \
    "QMAKE_CFLAGS+=-I$source_win/lib/qhull/src"
make -j8
NIFSKOPE_BUILD_DIR_WIN="$(cygpath -m "$build_dir")" python "$root/scripts/deploy-local-nifskope.py"
