@echo off
rem gen_samples.cmd — generate the docs/labs sample data set (goal D1).
rem
rem usage:
rem   scripts\gen_samples.cmd [--profile=lab^|stress] [--seed=<n>] [--spec=<path>] ...
rem
rem Locates sicnu_generate_samples in a build tree next to this repo
rem (build\, build-*\, or %SICNU_GENERATE_SAMPLES%), generates into
rem data\samples by default, then verifies the manifest.
setlocal enabledelayedexpansion

set "repo_root=%~dp0.."
set "bin=%SICNU_GENERATE_SAMPLES%"
if not defined bin (
  if exist "%repo_root%\build\tools\sicnu_generate_samples.exe" set "bin=%repo_root%\build\tools\sicnu_generate_samples.exe"
  if not defined bin if exist "%repo_root%\build\sicnu_generate_samples.exe" set "bin=%repo_root%\build\sicnu_generate_samples.exe"
)
if not defined bin (
  for /d %%D in ("%repo_root%\build-*") do (
    if not defined bin if exist "%%D\tools\sicnu_generate_samples.exe" set "bin=%%D\tools\sicnu_generate_samples.exe"
    if not defined bin if exist "%%D\sicnu_generate_samples.exe" set "bin=%%D\sicnu_generate_samples.exe"
  )
)
if not defined bin (
  echo gen_samples: sicnu_generate_samples not found.
  echo   build it first:  cmake -S . -B build ^&^& cmake --build build --target sicnu_generate_samples
  echo   or set SICNU_GENERATE_SAMPLES to an existing binary.
  exit /b 1
)

pushd "%repo_root%"
set "out_arg=--out=%repo_root%\data\samples"
set "has_out=0"
for %%A in (%*) do (
  if "%%A"=="--verify" (
    echo gen_samples: --verify is run automatically; drop it.
    popd
    exit /b 2
  )
  echo %%A | findstr /b "--out=" >nul && set "has_out=1"
)
if "%has_out%"=="0" (
  "%bin%" "%out_arg%" %*
) else (
  "%bin%" %*
)
set "rc=%errorlevel%"
if not "%rc%"=="0" ( popd & exit /b %rc% )
if not "%has_out%"=="0" ( "%bin%" --verify ) else ( "%bin%" --verify "%out_arg%" )
set "vrc=%errorlevel%"
popd
exit /b %vrc%
