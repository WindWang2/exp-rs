@echo off
rem R24-A build driver: vcvars64 + Ninja, parallelism capped at 2.
rem Usage: build_r24a.cmd [ninja args...]  e.g.  build_r24a.cmd --target test_r24_a_nodata_semantics
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-r24_a-20261027T022732Z
cmake --build build-r24_a %*
