@echo off
REM Build targets in the verification-12 worktree. Usage: build_v12.cmd [target...]
REM Compile concurrency is clamped to the track ceiling (-j1, at most -j2).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;C:\deps\winflexbison;%PATH%
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\glm53-scientific-verification-12
set CMAKE_BUILD_PARALLEL_LEVEL=1
set CTEST_PARALLEL_LEVEL=1
if "%~1"=="" (
  C:\Qt\Tools\CMake_64\bin\cmake.exe --build build-v12 --parallel 1
) else (
  C:\Qt\Tools\CMake_64\bin\cmake.exe --build build-v12 --parallel 1 --target %*
)
