@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;%PATH%
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-r8-science
set CMAKE_BUILD_PARALLEL_LEVEL=4
set CTEST_PARALLEL_LEVEL=2
cmake --build build-r8sci %*
