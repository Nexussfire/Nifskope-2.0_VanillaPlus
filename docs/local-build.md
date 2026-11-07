# Local Windows build

This source snapshot matches upstream NifSkope commit
`8e30c9fb758d7bed6d21d2eab57b3e9654707e3a` (the `simplify.cpp` and `build/nif.xml`
Git blob hashes match). The missing submodule sources were checked out at the
exact revisions recorded in that commit:

| Dependency | Revision |
| --- | --- |
| qhull | `d1c2fc0caa5f644f3a0f220290d4a868c68ed4f6` |
| gli | `779b99ac6656e4d30c3b24e96e0136a59649a869` |
| meshoptimizer | `ec2ee8b7eee36cebfd0c2b4bf28322da90492484` |
| kfmxml | `91eff92daf197f0c5376740ed4b000d064138ec2` |
| nifxml | `970a6238218a106daaeb89a61bcda0eeaf9d08c4` |

Qt source: `C:\Utility Apps\qt-everywhere-src-6.12.0` (6.12.0 alpha1).
Visual Studio 2022 Build Tools provide the x64 compiler, CMake, and Ninja.
The source-built MSVC Qt kit and its build cache are in
`C:\Utility Apps\NifSkopeBuildTools\qt6` and `qt6-build`. The relocated old
CMake cache retains its original paths and cannot safely be resumed in place.
The optional `scripts/configure-local-qt.cmd` and `scripts/build-local-qt.cmd`
instead configure a fresh build in `qt6-build-fresh` and install to `qt6-fresh`.
They use Python to prepare header directories for Qt's `syncqt` tool.

The Qt 6.12.0 alpha source build completed and installed successfully, but the
fork's MSVC application build fails on GCC-specific code in the existing tree.
No broad source changes were made to force MSVC compatibility.

## Working NifSkope build

The recommended alternative from the upstream README is a local portable MSYS2
UCRT64 installation in `C:\Utility Apps\NifSkopeBuildTools\msys64`. Its 2026-09-27 base archive was verified
against the official release SHA-256
`ea2f31a0b6ade63914ce441ffb022f0f6aa96982bfefa2326460a26d5fb01322`.
The installed packages include GCC 16.2.0, Qt 6.12.0, qmake, make and Qt image
formats. Qt's MSYS2 binaries are a separate kit from the locally compiled MSVC
Qt build; mixing those C++ ABIs is unsupported.

The GCC application build succeeded and the deploy script copied its required
Qt plugins, transitive UCRT runtime DLLs, XML schemas, style and shaders. The
executable is `C:\Utility Apps\NifSkopeBuildTools\nifskope-ucrt\release\NifSkope.exe`. The default Windows
platform and the offscreen platform both passed a `--version` startup check;
this is not a graphics, NIF load/save, or feature test. No test asset was modified.

To rebuild from PowerShell at the project root:

```powershell
$env:MSYSTEM = 'UCRT64'
& 'C:\UTILIT~1\NifSkopeBuildTools\msys64\usr\bin\bash.exe' -lc 'cd $PROJECT_ROOT && bash scripts/build-local-nifskope-ucrt.sh'
```

The build uses `noavx2=1`, which targets CPUs with AVX and F16C. The qmake
project's existing pre/post-link steps are disabled in this isolated build:
they would rewrite the source README and replace a resource directory. The
deploy script provides the runtime files instead. The optional CoACD shared
library and NifMopp tool are not bundled; operations needing them may be unavailable.
The build uses the existing `C:\UTILIT~1` short name for `C:\Utility Apps`;
MSYS2 recommends paths without spaces, so the toolchain should be retested if
that directory is renamed or the short name changes.

Resource initialization now installs a detected game path before scanning its
Data directory. Settings version 2 is migrated once to version 3: a detected,
enabled game with no resource-list key is scanned and the discovered paths are
saved. Explicitly empty lists, custom lists, and disabled games are not changed.
`tests/run-vanillaplus-ui-smoke.sh` checks this with temporary settings; setting
`NIFSKOPE_TEST_GAME_ROOT` additionally tests a real Skyrim installation read-only.
