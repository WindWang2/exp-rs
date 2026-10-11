@echo off
rem R8-science test runner: DLL resolution via build dir (applocal), Qt bin,
rem qca/kc bins and this worktree's vcpkg installed dir (debug + release).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set SRC=C:\Users\wangj.KEVIN\projects\exp-rs-r8-science
set BUILD=%SRC%\build-r8sci
set VCPKG_BIN=%BUILD%\vcpkg_installed\x64-windows\bin
set VCPKG_DBG=%BUILD%\vcpkg_installed\x64-windows\debug\bin
set PATH=%BUILD%;C:\deps\Qt\6.8.0\msvc2022_64\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin;%VCPKG_DBG%;%VCPKG_BIN%;%PATH%
set CTEST_PARALLEL_LEVEL=2
cd /d "%BUILD%"
if "%~1"=="" ( ctest --output-on-failure %2 %3 %4 %5 ) else ( ctest -R "%~1" --output-on-failure %2 %3 %4 %5 )
exit /b %errorlevel%
