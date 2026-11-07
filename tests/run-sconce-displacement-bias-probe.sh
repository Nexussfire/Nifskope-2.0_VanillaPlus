#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${NIFSKOPE_BUILD_DIR:-/c/UTILIT~1/NifSkopeBuildTools/nifskope-ucrt}"
cd "$build"
qt="../msys64/ucrt64"
g++ -std=c++20 -O2 -march=sandybridge -mf16c \
  -I"$root/src" -I"$root/lib" -I"$root/lib/libfo76utils/src" \
  -I"$qt/include/qt6" -I"$qt/include/qt6/QtCore" -I"$qt/include/qt6/QtGui" \
  -I"$qt/include/qt6/QtWidgets" -I"$qt/include/qt6/QtNetwork" \
  -I"$qt/include/qt6/QtXml" -I"$qt/include/qt6/QtOpenGL" -I"$qt/include/qt6/QtOpenGLWidgets" \
  -c "$root/tests/sconce_displacement_bias_probe.cpp" -o GeneratedFiles/.obj/sconce_displacement_bias_probe.o
g++ -mthreads -o release/sconce-displacement-bias-probe.exe \
  GeneratedFiles/.obj/sconce_displacement_bias_probe.o @GeneratedFiles/.obj/object_script.NifSkope.Release \
  -lopengl32 "$qt/lib/libQt6OpenGLWidgets.dll.a" "$qt/lib/libQt6OpenGL.dll.a" \
  "$qt/lib/libQt6Widgets.dll.a" "$qt/lib/libQt6Gui.dll.a" \
  "$qt/lib/libQt6Xml.dll.a" "$qt/lib/libQt6Network.dll.a" "$qt/lib/libQt6Core.dll.a" -lshell32
QT_QPA_PLATFORM=offscreen timeout 90s ./release/sconce-displacement-bias-probe.exe \
  "$root/Meshes Vanilla Test/meshes/dwewallsconce03.nif" "$root/Textures Vanilla Test"
