@echo off
rem Test runner for the cartography-platform-7 worktree (CRLF).
rem DLL resolution: build dir (applocal), Qt bin (debug Qt6*d.dll),
rem qca/kc bins, and the shared vcpkg installed dir (debug + release).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set SRC=C:\Users\wangj.KEVIN\projects\exp-rs-cartography-platform-7
set BUILD=%SRC%\build-dev
set VCPKG_BIN=C:\Users\wangj.KEVIN\projects\exp-rs-cartography-knowledge-platform-6\build-dev\vcpkg_installed\x64-windows\bin
set VCPKG_DBG=C:\Users\wangj.KEVIN\projects\exp-rs-cartography-knowledge-platform-6\build-dev\vcpkg_installed\x64-windows\debug\bin
set PATH=%BUILD%;C:\deps\Qt\6.8.0\msvc2022_64\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin;%VCPKG_DBG%;%VCPKG_BIN%;%PATH%
set CTEST_PARALLEL_LEVEL=1
cd /d "%BUILD%"
if "%~1"=="direct" (
  test_mapspec.exe %2 %3 %4
  exit /b %errorlevel%
)
if "%~1"=="" (
  ctest --output-on-failure
) else (
  ctest -R "%~1" --output-on-failure
)
exit /b %errorlevel%
