@echo off
rem R24-A test runner: DLL resolution = build dir (applocal deps),
rem Qt debug bin, qca/kc bins, vcpkg debug+release bins.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set SRC=C:\Users\wangj.KEVIN\projects\exp-rs-r24_a-20261027T022732Z
set BUILD=%SRC%\build-r24_a
set VCPKG_BIN=%BUILD%\vcpkg_installed\x64-windows\bin
set VCPKG_DBG=%BUILD%\vcpkg_installed\x64-windows\debug\bin
set PATH=%BUILD%;C:\deps\Qt\6.8.0\msvc2022_64\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin;%VCPKG_DBG%;%VCPKG_BIN%;%PATH%
set QT_QPA_PLATFORM=offscreen
set CTEST_PARALLEL_LEVEL=1
set PROJ_LIB=%BUILD%\vcpkg_installed\x64-windows\share\proj
cd /d "%BUILD%"
if "%~1"=="direct" (
  %2 %3 %4 %5
  exit /b %errorlevel%
)
if "%~1"=="" (
  ctest --output-on-failure %2 %3 %4 %5
) else (
  ctest -R "%~1" --output-on-failure %2 %3 %4 %5
)
exit /b %errorlevel%
