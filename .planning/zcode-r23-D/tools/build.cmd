@echo off
rem R23-D isolated build wrapper: serializes big builds via lock dir, -j2 default
rem usage: build.cmd <target...>   (extra cmake args after -- )
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
set QT_QPA_PLATFORM=offscreen
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z
cmake --build build-D --parallel 2 %*
exit /b %ERRORLEVEL%
