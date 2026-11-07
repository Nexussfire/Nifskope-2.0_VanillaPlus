@echo off
setlocal
pushd "%~dp0.."
set "TEST_DIR=C:\UTILIT~1\NifSkopeBuildTools\vanillaplus-tests"
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /Isrc src\geometry\vanillaplus.cpp tests\vanillaplus_repeat_probe.cpp /Fo:%TEST_DIR%\ /Fe:%TEST_DIR%\vanillaplus_repeat_probe.exe /link /INCREMENTAL:NO
if errorlevel 1 (
  popd
  exit /b 1
)
"%TEST_DIR%\vanillaplus_repeat_probe.exe"
set "test_result=%errorlevel%"
popd
exit /b %test_result%
