@echo off
REM Workbench 7.0 build helper: vcvars + ninja at resource-bounded parallelism.
REM Usage: build_wb7.cmd <ninja args...>   e.g. build_wb7.cmd test_workbench_shutdown_policy
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-professional-workbench-7
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
C:/Qt/Tools/Ninja/ninja.exe -C build-dev %*
exit /b %errorlevel%
