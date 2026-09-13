@echo off
rem build_offline_bundle.cmd - assemble the offline classroom bundle (goal D7).
rem
rem Usage:
rem   scripts\build_offline_bundle.cmd --build-dir <dir> [--out <dir>] [--version <v>]
rem                                    [--max-mb <n>] [--skip-samples] [--verify <bundle>]
rem
rem Windows twin of build_offline_bundle.sh (same steps, same manifest contract,
rem see packaging/OFFLINE_BUNDLE.md). Fully local; never touches the network.
setlocal enabledelayedexpansion
set "SCRIPT_DIR=%~dp0"
set "REPO_ROOT=%SCRIPT_DIR%..\"
for %%I in ("%REPO_ROOT%") do set "REPO_ROOT=%%~fI"

set "BUILD_DIR="
set "OUT_DIR="
set "VERSION="
set "MAX_MB=250"
set "SKIP_SAMPLES=0"
set "VERIFY_PATH="

:parse
if "%~1"=="" goto :parsed
if /I "%~1"=="--build-dir"    ( set "BUILD_DIR=%~2"   & shift & shift & goto :parse )
if /I "%~1"=="--out"          ( set "OUT_DIR=%~2"     & shift & shift & goto :parse )
if /I "%~1"=="--version"      ( set "VERSION=%~2"     & shift & shift & goto :parse )
if /I "%~1"=="--max-mb"       ( set "MAX_MB=%~2"      & shift & shift & goto :parse )
if /I "%~1"=="--skip-samples" ( set "SKIP_SAMPLES=1"  & shift & goto :parse )
if /I "%~1"=="--verify"       ( set "VERIFY_PATH=%~2" & shift & shift & goto :parse )
if /I "%~1"=="-h"             ( goto :usage )
if /I "%~1"=="--help"         ( goto :usage )
echo build_offline_bundle: unknown option "%~1" & exit /b 2

:usage
findstr /B /R "^rem" "%~f0"
exit /b 2

:parsed
if not "%VERIFY_PATH%"=="" goto :verify

if "%BUILD_DIR%"=="" ( echo build_offline_bundle: --build-dir is required & exit /b 2 )
if not exist "%BUILD_DIR%\" ( echo build_offline_bundle: build dir not found: %BUILD_DIR% & exit /b 1 )

set "CLI_BIN="
set "GEN_BIN="
for %%C in ("%BUILD_DIR%\sicnu_geo_rs_cli.exe" "%BUILD_DIR%\bin\sicnu_geo_rs_cli.exe" "%BUILD_DIR%\src\cli\sicnu_geo_rs_cli.exe") do (
  if exist %%C if "!CLI_BIN!"=="" set "CLI_BIN=%%~fC"
)
for %%G in ("%BUILD_DIR%\tools\sicnu_generate_samples.exe" "%BUILD_DIR%\sicnu_generate_samples.exe" "%BUILD_DIR%\bin\sicnu_generate_samples.exe") do (
  if exist %%G if "!GEN_BIN!"=="" set "GEN_BIN=%%~fG"
)
if "!CLI_BIN!"=="" ( echo build_offline_bundle: sicnu_geo_rs_cli.exe not found under %BUILD_DIR% & exit /b 1 )
if "!GEN_BIN!"=="" ( echo build_offline_bundle: sicnu_generate_samples.exe not found under %BUILD_DIR% & exit /b 1 )

if "%VERSION%"=="" for /F "delims=" %%V in ('git -C "%REPO_ROOT%" describe --tags --always 2^>nul') do set "VERSION=%%V"
if "%VERSION%"=="" set "VERSION=dev"
if "%OUT_DIR%"=="" set "OUT_DIR=%REPO_ROOT%\dist"
set "BUNDLE=%OUT_DIR%\sicnu-lab-%VERSION%"
if exist "%BUNDLE%\" rmdir /S /Q "%BUNDLE%"
mkdir "%BUNDLE%\bin" "%BUNDLE%\data" "%BUNDLE%\labs" || exit /b 1

echo == copying binaries ==
copy /Y "%CLI_BIN%" "%BUNDLE%\bin\" >nul || exit /b 1
copy /Y "%GEN_BIN%" "%BUNDLE%\bin\" >nul || exit /b 1

rem Deploy the Qt/QGIS/GDAL runtime next to the CLI so bin/ is self-contained.
rem QGIS_BIN (the QGIS/vcpkg install's bin directory from the configure step)
rem is a HARD requirement: the CLI links qgis_core, and windeployqt cannot
rem supply QGIS/GDAL DLLs. A teacher shipping a non-self-contained bundle
rem would only discover the missing DLLs on the offline classroom machine.
if "%QGIS_BIN%"=="" if not "%SICNU_QGIS_BIN%"=="" set "QGIS_BIN=%SICNU_QGIS_BIN%"
if "%QGIS_BIN%"=="" (
  echo build_offline_bundle: QGIS_BIN is required ^(or set SICNU_QGIS_BIN^) -
  echo   point it at the QGIS build's bin directory, e.g. the vcpkg_installed
  echo   x64-windows\bin or qgis install used at configure time.
  exit /b 1
)
if not exist "%QGIS_BIN%\qgis_core.dll" (
  echo build_offline_bundle: QGIS_BIN does not contain qgis_core.dll: %QGIS_BIN%
  exit /b 1
)
rem windeployqt: prefer the Qt kit used at configure time, fall back to PATH.
set "WINDEPLOYQT="
if not "%SICNU_QT_DIR%"=="" if exist "%SICNU_QT_DIR%\bin\windeployqt.exe" set "WINDEPLOYQT=%SICNU_QT_DIR%\bin\windeployqt.exe"
if "%WINDEPLOYQT%"=="" for %%W in (windeployqt.exe) do set "WINDEPLOYQT=%%~$PATH:W"
if "%WINDEPLOYQT%"=="" (
  echo build_offline_bundle: windeployqt.exe not found ^(set SICNU_QT_DIR or add Qt bin to PATH^)
  exit /b 1
)
echo == deploying runtime (windeployqt) ==
"%WINDEPLOYQT%" --no-translations --no-system-d3d-compiler --no-opengl-sw --compiler-runtime "%BUNDLE%\bin\sicnu_geo_rs_cli.exe" || exit /b 1
echo == copying QGIS/GDAL runtime from %QGIS_BIN% ==
for %%F in ("%QGIS_BIN%\*.dll") do copy /Y "%%~fF" "%BUNDLE%\bin\" >nul
if not exist "%BUNDLE%\bin\qgis_core.dll" (
  echo build_offline_bundle: qgis_core.dll missing after copy - QGIS_BIN DLL set incomplete
  exit /b 1
)
rem proj.db is what lets offline grading identify EPSG authorities - without
rem it the CRS assertion fails every submission. Copied next to the DLLs' data.
set "QGIS_SHARE="
if not "%SICNU_QGIS_SHARE%"=="" set "QGIS_SHARE=%SICNU_QGIS_SHARE%"
if "%QGIS_SHARE%"=="" for %%I in ("%QGIS_BIN%\..") do set "QGIS_SHARE=%%~fI\share"
if not exist "%QGIS_SHARE%\proj\proj.db" (
  echo build_offline_bundle: proj.db not found under %QGIS_SHARE%\proj ^(set SICNU_QGIS_SHARE^)
  exit /b 1
)
echo == copying PROJ/GDAL data from %QGIS_SHARE% ==
mkdir "%BUNDLE%\data\runtime"
robocopy "%QGIS_SHARE%\proj" "%BUNDLE%\data\runtime\proj" /E /NFL /NDL /NJH /NJS >nul
if errorlevel 8 exit /b 1
if exist "%QGIS_SHARE%\gdal" (
  robocopy "%QGIS_SHARE%\gdal" "%BUNDLE%\data\runtime\gdal" /E /NFL /NDL /NJH /NJS >nul
  if errorlevel 8 exit /b 1
)
if not exist "%BUNDLE%\data\runtime\proj\proj.db" (
  echo build_offline_bundle: proj.db missing after copy
  exit /b 1
)

echo == generating sample data (deterministic, offline) ==
mkdir "%BUNDLE%\data\samples"
if not "%SKIP_SAMPLES%"=="1" (
  pushd "%BUNDLE%"
  "%GEN_BIN%" "%BUNDLE%\data\samples" || ( popd & exit /b 1 )
  popd
)

echo == copying data tree ==
rem robocopy exit codes: 0-7 are success flavors, >=8 is a real failure.
for %%E in (labs pipelines schemas help plugins processing cartography agent tools) do (
  if exist "%REPO_ROOT%\data\%%E\" robocopy "%REPO_ROOT%\data\%%E" "%BUNDLE%\data\%%E" /E /NFL /NDL /NJH /NJS >nul
  if errorlevel 8 (
    echo build_offline_bundle: robocopy failed for data\%%E
    exit /b 1
  )
)
mkdir "%BUNDLE%\data\fonts"
copy /Y "%REPO_ROOT%\resources\fonts\*.ttf" "%BUNDLE%\data\fonts\" >nul || exit /b 1

echo == applying D7 grading overlay ^(calibrated to the shipped scene^) ==
rem The repo rules track D4/D1's fixture; the bundle ships the calibrated copy.
copy /Y "%REPO_ROOT%\packaging\bundle\labs\grading-overlay\*.rules.json" "%BUNDLE%\data\labs\grading\" >nul || exit /b 1

echo == copying lab 1 ==
mkdir "%BUNDLE%\labs\lab1"
copy /Y "%REPO_ROOT%\packaging\bundle\labs\lab1\lab1_ndvi.pipeline.json" "%BUNDLE%\labs\lab1\" >nul || exit /b 1
copy /Y "%REPO_ROOT%\packaging\bundle\labs\lab1\INSTRUCTIONS-zh.md" "%BUNDLE%\labs\lab1\" >nul || exit /b 1

echo == copying one-click scripts and docs ==
for %%F in (RUN.cmd GENERATE_SAMPLES.cmd GRADE_ALL.cmd VERIFY.cmd VERIFY.ps1 README-zh.md) do (
  copy /Y "%REPO_ROOT%\packaging\bundle\%%F" "%BUNDLE%\%%F" >nul || exit /b 1
)

echo == writing manifest ==
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%bundle_manifest.ps1" -Bundle "%BUNDLE%" -Version "%VERSION%" -MaxMb %MAX_MB% || exit /b 1

call "%SCRIPT_DIR%build_offline_bundle.cmd" --verify "%BUNDLE%"
exit /b %errorlevel%

:verify
if not exist "%VERIFY_PATH%\manifest.json" ( echo build_offline_bundle: no manifest.json under %VERIFY_PATH% & exit /b 1 )
powershell -NoProfile -ExecutionPolicy Bypass -Command "& '%SCRIPT_DIR%bundle_manifest.ps1' -Verify '%VERIFY_PATH%'"
exit /b %errorlevel%
