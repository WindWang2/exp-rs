@echo off
rem gen_samples.cmd — generate the docs/labs sample data set (goal D1).
rem
rem usage:
rem   scripts\gen_samples.cmd [--profile=lab^|stress] [--seed=<n>] [--spec=<path>]
rem                           [--out=<dir>] [--verify]
rem
rem Locates sicnu_generate_samples in a build tree next to this repo
rem (build\, build-*\ — build*\tools\ included — or %SICNU_GENERATE_SAMPLES%).
rem Default output is data\samples; flags are forwarded to the CLI and the
rem verify pass runs against the directory that was actually written.
setlocal

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

rem Verify the directory that was actually written: the user's --out when
rem given, otherwise the default data\samples (the CLI's own default).
rem Quote each forwarded argument. Parse --out= without `for %*` so paths
rem with spaces stay intact. Skip the forced verify pass for --help / -h
rem and when the caller already passed --verify.
set "out_dir=%repo_root%\data\samples"
set "skip_verify="
set "args="

:parse_args
if "%~1"=="" goto args_done
set "arg=%~1"

if /I "%arg%"=="--help" set "skip_verify=1"
if /I "%arg%"=="-h" set "skip_verify=1"
if /I "%arg%"=="--verify" set "skip_verify=1"

if /I "%arg:~0,6%"=="--out=" set "out_dir=%arg:~6%"

set args=%args% "%~1"
shift
goto parse_args

:args_done
pushd "%repo_root%"
"%bin%" %args%
set "rc=%errorlevel%"
if not "%rc%"=="0" (
  popd
  exit /b %rc%
)
if defined skip_verify (
  popd
  exit /b 0
)
"%bin%" --verify "--out=%out_dir%"
set "vrc=%errorlevel%"
popd
exit /b %vrc%
