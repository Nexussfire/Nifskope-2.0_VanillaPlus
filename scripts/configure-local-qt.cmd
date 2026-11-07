@echo off
setlocal
set "QT_SOURCE=C:\Utility Apps\qt-everywhere-src-6.12.0\qtbase"
set "VS_ROOT=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools"
set "CMAKE_BIN=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
set "NINJA_BIN=%VS_ROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
set "QT_BUILD=C:\Utility Apps\NifSkopeBuildTools\qt6-build-fresh"
set "QT_INSTALL=C:\Utility Apps\NifSkopeBuildTools\qt6-fresh"
if not exist "%QT_SOURCE%\CMakeLists.txt" exit /b 1
call "%VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "PATH=%CMAKE_BIN%;%NINJA_BIN%;%PATH%"
pushd "%~dp0.."
cmake -S "%QT_SOURCE%" -B "%QT_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%QT_INSTALL%" -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DBUILD_TESTING=OFF -DQT_BUILD_BENCHMARKS=OFF
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
