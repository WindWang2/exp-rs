@echo off
rem RUN.cmd - offline lab 1, end to end (goal D7). Must stay network-free.
rem Run from the bundle root (or double-click in Explorer; it relocates itself).
rem ASCII-only text: classroom consoles are not guaranteed to be UTF-8.
setlocal
set "BUNDLE=%~dp0"
for %%I in ("%BUNDLE:~0,-1%") do set "BUNDLE=%%~fI"
cd /d "%BUNDLE%"

set "SICNU_OFFLINE=1"
rem The bundle's grading rules live in data\labs\grading; point the grader at
rem them explicitly - a clean classroom machine has no source tree to walk.
set "SICNU_LAB_RULES_DIR=%BUNDLE%\data\labs\grading"
rem Projection/GDAL databases shipped with the bundle (proj.db identifies the
rem EPSG authorities the grading rules compare against).
set "PROJ_DATA=%BUNDLE%\data\runtime\proj"
set "GDAL_DATA=%BUNDLE%\data\runtime\gdal"
if not defined QT_QPA_PLATFORM set "QT_QPA_PLATFORM=offscreen"
rem Runtime DLLs live next to the CLI (windeployqt + QGIS runtime in bin\).
set "PATH=%BUNDLE%\bin;%PATH%"

echo === [1/3] sample data ===
if exist "data\samples\landsat_sample.tif" (
  echo     data\samples already present - skipping generation.
) else (
  if not exist "bin\sicnu_generate_samples.exe" (
    echo ERROR: bin\sicnu_generate_samples.exe missing - bundle incomplete.
    pause
    exit /b 1
  )
  bin\sicnu_generate_samples.exe data\samples
  if errorlevel 1 (
    echo ERROR: sample generation failed.
    pause
    exit /b 1
  )
)

echo === [2/3] lab 1 pipeline ^(NDVI, offline^) ===
if not exist "bin\sicnu_geo_rs_cli.exe" (
  echo ERROR: bin\sicnu_geo_rs_cli.exe missing - bundle incomplete.
  pause
  exit /b 1
)
if not exist "output" mkdir "output"
bin\sicnu_geo_rs_cli.exe --offline --pipeline "labs\lab1\lab1_ndvi.pipeline.json"
if errorlevel 1 (
  echo ERROR: pipeline failed - see messages above.
  pause
  exit /b 1
)

echo === [3/3] grading lab 1 ===
bin\sicnu_geo_rs_cli.exe --offline lab --lab ndvi_basics --grade "output\lab1_ndvi.tif" --out "output\lab1_report.json"
set "RC=%errorlevel%"
echo.
if "%RC%"=="0" (
  echo RESULT: lab 1 PASSED. Report: output\lab1_report.json
) else if "%RC%"=="1" (
  echo RESULT: lab 1 graded BELOW the pass line. See output\lab1_report.json
) else (
  echo RESULT: lab 1 could not be graded ^(exit %RC%^). See messages above.
)
pause
exit /b %RC%
