@echo off
setlocal
cd /d "%~dp0"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not exist "..\build\host-tests" mkdir "..\build\host-tests"
cl /nologo /std:c++17 /EHsc /W4 /WX /Fe:"..\build\host-tests\production_policy_test.exe" /Fo:"..\build\host-tests\production_policy_test.obj" production_policy_test.cpp
if errorlevel 1 exit /b 1
"..\build\host-tests\production_policy_test.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++17 /EHsc /W4 /WX /I"stubs" /Fe:"..\build\host-tests\capture_core_test.exe" /Fo:"..\build\host-tests\capture_core_test.obj" capture_core_test.cpp
if errorlevel 1 exit /b 1
"..\build\host-tests\capture_core_test.exe"
