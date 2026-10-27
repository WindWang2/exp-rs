@echo off
rem R23-D isolated build wrapper, -j4 lane (protocol: only while free RAM is
rem ample and no sibling worktree is compiling; back to -j2 if pressure).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CMAKE_BUILD_PARALLEL_LEVEL=4
set CTEST_PARALLEL_LEVEL=1
set QT_QPA_PLATFORM=offscreen
set QT_QPA_PLATFORM_PLUGIN_PATH=C:\deps\Qt\6.8.0\msvc2022_64\plugins\platforms
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z
cmake --build build-D --parallel 4 %*
exit /b %ERRORLEVEL%
