@echo off
rem _env.cmd — shared toolchain probe for the D7 one-click scripts.
rem Hardened sibling of the repo-root *_wb7.cmd scripts: everything is probed
rem or %SICNU_*%-overridable, nothing is hardwired to one user profile.
rem Sets (if not already set): SICNU_REPO_ROOT, SICNU_VCVARS, SICNU_CMAKE,
rem SICNU_NINJA, SICNU_CTEST, SICNU_QT_DIR, SICNU_VCPKG, SICNU_VCPKG_INSTALLED,
rem SICNU_WINFLEXBISON. Resource bounds are non-negotiable (goal D7):
rem   CMAKE_BUILD_PARALLEL_LEVEL=2, CTEST_PARALLEL_LEVEL=1.
if defined SICNU_ENV_LOADED exit /b 0
setlocal enabledelayedexpansion

set "SICNU_REPO_ROOT=%~dp0..\.."
for %%I in ("!SICNU_REPO_ROOT!") do set "SICNU_REPO_ROOT=%%~fI"

rem --- vcvars64.bat: vswhere first, then the two well-known installs ---------
if not defined SICNU_VCVARS (
  set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
  if exist "!VSWHERE!" (
    for /F "usebackq tokens=*" %%V in (`"^ "!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath^"`) do (
      if exist "%%V\VC\Auxiliary\Build\vcvars64.bat" set "SICNU_VCVARS=%%V\VC\Auxiliary\Build\vcvars64.bat"
    )
  )
  if not defined SICNU_VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" (
    set "SICNU_VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
  )
  if not defined SICNU_VCVARS if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" (
    set "SICNU_VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  )
)

rem --- cmake: VS-bundled first, then PATH ------------------------------------
if not defined SICNU_CMAKE (
  if defined SICNU_VCVARS for /F "delims=" %%V in ("!SICNU_VCVARS!\..\..\..\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe") do (
    if exist "%%~fV" set "SICNU_CMAKE=%%~fV"
  )
)
if not defined SICNU_CMAKE for %%C in (cmake.exe) do set "SICNU_CMAKE=%%~$PATH:C"
set "SICNU_CMAKE_DIR=!SICNU_CMAKE!\.."
for %%I in ("!SICNU_CMAKE_DIR!") do set "SICNU_CTEST=%%~fI\ctest.exe"

rem --- ninja: Qt kit first (wb7 convention), then PATH -----------------------
if not defined SICNU_NINJA (
  for %%N in ("C:\Qt\Tools\Ninja\ninja.exe") do if exist "%%~fN" set "SICNU_NINJA=%%~fN"
)
if not defined SICNU_NINJA for %%N in (ninja.exe) do set "SICNU_NINJA=%%~$PATH:N"

rem --- dependency locations: wb7 defaults, all overridable -------------------
if not defined SICNU_QT_DIR            set "SICNU_QT_DIR=C:\deps\Qt\6.8.0\msvc2022_64"
if not defined SICNU_VCPKG             set "SICNU_VCPKG=C:\deps\vcpkg"
if not defined SICNU_VCPKG_INSTALLED   set "SICNU_VCPKG_INSTALLED=C:\Users\wangj.KEVIN\projects\exp-rs-win\build-win\vcpkg_installed"
if not defined SICNU_WINFLEXBISON      set "SICNU_WINFLEXBISON=C:\deps\winflexbison"
if not defined SICNU_QCA_DIR           set "SICNU_QCA_DIR=C:\deps\qca-install"
if not defined SICNU_KEYCHAIN_DIR      set "SICNU_KEYCHAIN_DIR=C:\deps\kc-install"

rem --- resource bounds (hard) -------------------------------------------------
set "CMAKE_BUILD_PARALLEL_LEVEL=2"
set "CTEST_PARALLEL_LEVEL=1"
set "QT_QPA_PLATFORM=offscreen"

set "SICNU_ENV_LOADED=1"
endlocal & (
  set "SICNU_REPO_ROOT=%SICNU_REPO_ROOT%"
  set "SICNU_VCVARS=%SICNU_VCVARS%"
  set "SICNU_CMAKE=%SICNU_CMAKE%"
  set "SICNU_CTEST=%SICNU_CTEST%"
  set "SICNU_NINJA=%SICNU_NINJA%"
  set "SICNU_QT_DIR=%SICNU_QT_DIR%"
  set "SICNU_VCPKG=%SICNU_VCPKG%"
  set "SICNU_VCPKG_INSTALLED=%SICNU_VCPKG_INSTALLED%"
  set "SICNU_WINFLEXBISON=%SICNU_WINFLEXBISON%"
  set "SICNU_QCA_DIR=%SICNU_QCA_DIR%"
  set "SICNU_KEYCHAIN_DIR=%SICNU_KEYCHAIN_DIR%"
  set "CMAKE_BUILD_PARALLEL_LEVEL=%CMAKE_BUILD_PARALLEL_LEVEL%"
  set "CTEST_PARALLEL_LEVEL=%CTEST_PARALLEL_LEVEL%"
  set "QT_QPA_PLATFORM=%QT_QPA_PLATFORM%"
)
exit /b 0
