@echo off
rem gen_samples.cmd — generate the docs/labs sample data set (goal D1).
rem
rem usage:
rem   scripts\gen_samples.cmd [--profile=lab^|stress] [--seed=<n>] [--spec=<path>]
rem                           [--out=<dir>] [--verify] [--help]
rem
rem Locates sicnu_generate_samples in a build tree next to this repo
rem (build\, build-*\ — build*\tools\ included — or %SICNU_GENERATE_SAMPLES%).
rem Default output is data\samples; flags are forwarded to the CLI with each
rem argument quoted individually (a bare %* would split on spaces) and the
rem verify pass runs against the directory that was actually written — unless
rem the caller already passed --verify or asked for --help (exit 0, no data).
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

rem Collect the arguments, each quoted on its own (a bare %* would lose the
rem quoting of paths with spaces), and read --out=/--verify/--help without a
rem word-splitting for loop. The batch argument parser splits --key=value into
rem two tokens, so a flag is re-joined with its value below; %~1 strips the
rem caller's quotes so the comparisons and the --out= substring stay exact.
set "args="
set "out_dir="
set "skip_verify="

:parse_args
if "%~1"=="" goto args_parsed
set "arg=%~1"
if not defined arg goto arg_collected
if "%arg:~0,2%"=="--" goto maybe_rejoin

:arg_collected
set "args=%args% "%arg%""
if "%arg:~0,6%"=="--out=" set "out_dir=%arg:~6%"
if "%arg%"=="--verify" set "skip_verify=1"
if "%arg%"=="--help" set "skip_verify=1"
if "%arg%"=="-h" set "skip_verify=1"
shift
goto parse_args

rem Re-join --key value into --key=value: the next token is a value only when
rem it does not itself start with a dash. The shift must sit outside any
rem parenthesized block, where %~1 would be frozen.
:maybe_rejoin
if "%~2"=="" goto arg_collected
set "next=%~2"
if "%next:~0,1%"=="-" goto arg_collected
shift
set "arg=%arg%=%next%"
goto arg_collected

:args_parsed
rem Verify the directory that was actually written: the user's --out when
rem given, otherwise the default data\samples (the CLI's own default).
if not defined out_dir set "out_dir=%repo_root%\data\samples"

pushd "%repo_root%"
"%bin%" %args%
set "rc=%errorlevel%"
if not "%rc%"=="0" ( popd & exit /b %rc% )
if defined skip_verify ( popd & exit /b 0 )
"%bin%" --verify "--out=%out_dir%"
set "vrc=%errorlevel%"
popd
exit /b %vrc%
