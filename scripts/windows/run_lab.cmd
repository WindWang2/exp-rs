@echo off
rem run_lab.cmd - repo-side offline lab 1: generate samples -> run the NDVI
rem pipeline -> grade the artifact (D7). Network-free: SICNU_OFFLINE=1 and
rem --offline are always set. Windows twin of the bundle's RUN.cmd.
setlocal enabledelayedexpansion
call "%~dp0_env.cmd"

set "BUILD=%SICNU_REPO_ROOT%\build-dev"
set "CLI=%BUILD%\sicnu_geo_rs_cli.exe"
set "GEN=%BUILD%\tools\sicnu_generate_samples.exe"
if not exist "%CLI%" ( echo ERROR: %CLI% not found - run setup.cmd first & exit /b 1 )

set "SICNU_OFFLINE=1"
set "SICNU_LAB_RULES_DIR=%SICNU_REPO_ROOT%\data\labs\grading"
set "PATH=%BUILD%;%SICNU_QT_DIR%\bin;%SICNU_QCA_DIR%\bin;%SICNU_KEYCHAIN_DIR%\bin;%SICNU_VCPKG_INSTALLED%\x64-windows\bin;%PATH%"
cd /d "%SICNU_REPO_ROOT%"

echo === [1/3] sample data ===
if exist "data\samples\landsat_sample.tif" (
  echo     data\samples present - skipping.
) else (
  if not exist "%GEN%" ( echo ERROR: %GEN% missing - run setup.cmd first & exit /b 1 )
  "%GEN%" data\samples || exit /b 1
)

echo === [2/3] lab 1 pipeline (NDVI, offline) ===
if not exist "output" mkdir "output"
"%CLI%" --offline --pipeline "packaging\bundle\labs\lab1\lab1_ndvi.pipeline.json" || exit /b 1

echo === [3/3] grading ===
"%CLI%" --offline lab --lab ndvi_basics --grade "output\lab1_ndvi.tif" --out "output\lab1_report.json"
set "RC=%errorlevel%"
if "%RC%"=="0" ( echo RESULT: lab 1 PASSED - output\lab1_report.json
) else if "%RC%"=="1" ( echo RESULT: lab 1 graded BELOW the pass line - output\lab1_report.json
) else ( echo RESULT: lab 1 could not be graded (exit %RC%^) )
exit /b %RC%
