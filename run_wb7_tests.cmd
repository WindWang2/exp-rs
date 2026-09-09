@echo off
REM Workbench 7.0 test runner: vcvars + sequential ctest (resource-bounded).
REM Usage: run_wb7_tests.cmd <regex>   e.g. run_wb7_tests.cmd test_workbench_shutdown_policy
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-professional-workbench-7
set CTEST_PARALLEL_LEVEL=1
set QT_QPA_PLATFORM=offscreen
set CMAKE_BUILD_PARALLEL_LEVEL=2
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe" -C Release --output-on-failure -R "%*"
exit /b %errorlevel%
