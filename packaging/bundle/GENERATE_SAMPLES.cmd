@echo off
rem GENERATE_SAMPLES.cmd — regenerate the bundle's sample rasters offline (D7).
setlocal
set "BUNDLE=%~dp0"
for %%I in ("%BUNDLE:~0,-1%") do set "BUNDLE=%%~fI"
cd /d "%BUNDLE%"
set "SICNU_OFFLINE=1"

if not exist "bin\sicnu_generate_samples.exe" (
  echo ERROR: bin\sicnu_generate_samples.exe missing — bundle incomplete.
  exit /b 1
)
echo Generating deterministic sample rasters into data\samples ...
bin\sicnu_generate_samples.exe data\samples
set "RC=%errorlevel%"
if "%RC%"=="0" (
  echo Done. Files: data\samples\landsat_sample.tif, change_before.tif, change_after.tif, dem_sample.tif
) else (
  echo Generation FAILED ^(exit %RC%^).
)
exit /b %RC%
