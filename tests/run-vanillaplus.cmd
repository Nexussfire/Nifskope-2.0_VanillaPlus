@echo off
setlocal
rem Run from a Visual Studio x64 Native Tools command prompt.
where cl >nul 2>nul
if errorlevel 1 (
  echo MSVC cl.exe is required. Use a Visual Studio x64 Native Tools prompt.
  exit /b 1
)
pushd "%~dp0.."
set "TEST_DIR=C:\UTILIT~1\NifSkopeBuildTools\vanillaplus-tests"
if not exist "%TEST_DIR%" mkdir "%TEST_DIR%"
cl /nologo /std:c++20 /EHsc /W4 /WX /Od /Zi /Isrc /Ilib src\geometry\vanillaplus.cpp src\geometry\vanillaplusreduce.cpp lib\meshoptimizer\src\simplifier.cpp lib\meshoptimizer\src\allocator.cpp tests\vanillaplus_test.cpp /Fo:%TEST_DIR%\ /Fd:%TEST_DIR%\compiler.pdb /Fe:%TEST_DIR%\vanillaplus_test.exe /link /INCREMENTAL:NO
if errorlevel 1 (
  popd
  exit /b 1
)
"%TEST_DIR%\vanillaplus_test.exe"
if errorlevel 1 (
  popd
  exit /b 1
)
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Isrc src\geometry\vanillaplus.cpp tests\vanillaplus_demo.cpp /Fo:%TEST_DIR%\ /Fe:%TEST_DIR%\vanillaplus_demo.exe /link /INCREMENTAL:NO
if errorlevel 1 (
  popd
  exit /b 1
)
"%TEST_DIR%\vanillaplus_demo.exe" "%TEST_DIR%\preview.svg"
set "test_result=%errorlevel%"
popd
exit /b %test_result%
