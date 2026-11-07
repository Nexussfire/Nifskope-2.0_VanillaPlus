@echo off
setlocal
set "VS_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "CMAKE_BIN=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJA_BIN=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "NIFSKOPE_QT_BUILD=C:\Utility Apps\NifSkopeBuildTools\qt6-build-fresh"
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%PATH%"
pushd "%~dp0.."
python scripts\prepare-qt-headers.py
if errorlevel 1 (
  popd
  exit /b 1
)
cmake --build "%NIFSKOPE_QT_BUILD%" --parallel 8
if errorlevel 1 (
  popd
  exit /b 1
)
cmake --install "%NIFSKOPE_QT_BUILD%"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
