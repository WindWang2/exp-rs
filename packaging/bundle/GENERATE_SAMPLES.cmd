@echo off
rem GENERATE_SAMPLES.cmd - regenerate the bundle's sample rasters offline (D7).
rem ASCII-only text: classroom consoles are not guaranteed to be UTF-8.
rem
rem With no arguments it (re)generates the default data\samples set and then
rem verifies it against the foundry manifest, so an interrupted disk can never
rem look healthy. Extra arguments are forwarded to sicnu_generate_samples
rem verbatim (e.g. GENERATE_SAMPLES.cmd --seed=7 --out=data\samples_demo);
rem the post-generate verify pass only runs for the default target.
rem Unattended runs: set SICNU_NO_PAUSE=1 to skip the interactive pause.
setlocal
set "BUNDLE=%~dp0"
for %%I in ("%BUNDLE:~0,-1%") do set "BUNDLE=%%~fI"
cd /d "%BUNDLE%"
set "SICNU_OFFLINE=1"
set "PROJ_DATA=%BUNDLE%\data\runtime\proj"
set "GDAL_DATA=%BUNDLE%\data\runtime\gdal"

if not exist "bin\sicnu_generate_samples.exe" (
  echo ERROR: bin\sicnu_generate_samples.exe missing - bundle incomplete.
  call :Pause
  exit /b 1
)
set "GEN_ARGS=%*"
if "%~1"=="" set "GEN_ARGS=--out=data\samples"
echo Generating deterministic sample rasters ...
echo     bin\sicnu_generate_samples.exe %GEN_ARGS%
bin\sicnu_generate_samples.exe %GEN_ARGS%
set "RC=%errorlevel%"
if not "%RC%"=="0" (
  echo Generation FAILED ^(exit %RC%^).
  call :Pause
  exit /b %RC%
)
if not "%GEN_ARGS%"=="--out=data\samples" goto :done
echo Verifying generated samples against the manifest ...
bin\sicnu_generate_samples.exe --verify "--out=data\samples"
set "RC=%errorlevel%"
if not "%RC%"=="0" (
  echo VERIFY FAILED ^(exit %RC%^) - the sample directory is not intact.
  call :Pause
  exit /b %RC%
)
:done
echo Done. Sample data is in place and verified.
call :Pause
exit /b %RC%

:Pause
if not defined SICNU_NO_PAUSE pause
exit /b
