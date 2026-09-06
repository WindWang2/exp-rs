@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-pi-harness-4
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
cmake --build build %*
