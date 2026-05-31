@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo Failed to set up VS environment
    exit /b 1
)
cd /d %~dp0
echo Building openCache with MSVC...

cl /EHsc /std:c++17 /Fe:test_openCache.exe ^
   src/cache_config.cc ^
   src/tag_array.cc ^
   src/open_cache.cc ^
   scenario/scenarios.cc ^
   test/test_main.cc ^
   /I src /I scenario ^
   /W3

if %ERRORLEVEL% EQU 0 (
    echo Build successful! Running tests...
    test_openCache.exe
) else (
    echo Build failed!
    exit /b 1
)
