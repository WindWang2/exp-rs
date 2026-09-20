@echo off
rem build.cmd - resource-safe local build/test wrapper (Windows lane).
rem
rem Mirrors scripts/build.sh (POSIX) and reuses the landed scripts\windows\_env.cmd
rem toolchain probe. Resource discipline (ADR 0147 / D7) is identical:
rem   * parallelism: default 1, hard cap 2, -j3+ refused (exit code 2);
rem     inherited CMAKE_BUILD_PARALLEL_LEVEL / CTEST_PARALLEL_LEVEL are clamped;
rem   * ctest always -j1 with QT_QPA_PLATFORM=offscreen;
rem   * every run tees a log plus a failure SUMMARY.txt;
rem   * "smoke" configures from an EMPTY directory so a stale CMakeCache can
rem     never masquerade as a working configure.
rem
rem usage:
rem   scripts\windows\build.cmd selftest                  (hermetic: no toolchain)
rem   scripts\windows\build.cmd doctor [--build-dir D]
rem   scripts\windows\build.cmd configure [--build-dir D] [--preset P]
rem   scripts\windows\build.cmd build [--build-dir D] [--jobs N] [target ...]
rem   scripts\windows\build.cmd test  [--build-dir D] [-R regex]
rem   scripts\windows\build.cmd smoke [--build-dir D] [--base-cache CACHE]
rem
rem The hermetic selftest builds its Unicode fixture path through PowerShell
rem (batch files cannot carry non-ASCII literals safely across console code
rem pages), so selftest requires powershell on PATH - present on Windows
rem 10/11 and Windows Server, absent on WinPE/Server Core images.
rem
rem _env.cmd (called below) pins CMAKE_BUILD_PARALLEL_LEVEL=2 and
rem CTEST_PARALLEL_LEVEL=1 for this lane; an explicit --jobs above the cap is
rem still refused.
setlocal enabledelayedexpansion

rem Capture the script path BEFORE any shift: after shift, %0 becomes the
rem subcommand name, so %~f0 would resolve to a nonexistent file.
set "SELF=%~f0"
for %%I in ("%SELF%") do set "SELF_DIR=%%~dpI"

set "SICNU_BUILD_CAP=2"
if not "%SICNU_BUILD_JOBS_CAP%"=="" set "SICNU_BUILD_CAP=%SICNU_BUILD_JOBS_CAP%"

rem Resolve repo root from this script's location (spaces-safe).
set "SCRIPT_DIR=%~dp0"
if "%SCRIPT_DIR:~-1%"=="\" set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"
set "REPO_ROOT=%SCRIPT_DIR%\..\.."
for %%I in ("%REPO_ROOT%") do set "REPO_ROOT=%%~fI"

set "BUILD_DIR_DEFAULT=%REPO_ROOT%\build-dev"
set "LOG_ROOT=%REPO_ROOT%\build-logs"

goto :main

rem ---------------------------------------------------------------------------
rem resolve_jobs <explicit-or-empty> -> JOBS (validated, clamped, exported)
rem ---------------------------------------------------------------------------
:resolve_jobs
set "REQ=%~1"
if "%REQ%"=="" set "REQ=%SICNU_BUILD_JOBS%"
if "%REQ%"=="" set "REQ=1"
echo %REQ%| findstr /r "^[0-9][0-9]*$" >nul || set "REQ=1"
if "%REQ%"=="0" set "REQ=1"
if %REQ% GTR %SICNU_BUILD_CAP% (
  echo build.cmd: refusing jobs=%REQ% - parallel cap is %SICNU_BUILD_CAP% ^(raise only with SICNU_BUILD_JOBS_CAP^) 1>&2
  exit /b 2
)
set "JOBS=%REQ%"
rem Clamp an oversized inherited value (stale environment must not defeat the cap).
if not "%CMAKE_BUILD_PARALLEL_LEVEL%"=="" (
  echo %CMAKE_BUILD_PARALLEL_LEVEL%| findstr /r "^[0-9][0-9]*$" >nul && (
    if %CMAKE_BUILD_PARALLEL_LEVEL% GTR %SICNU_BUILD_CAP% (
      echo build.cmd: clamped CMAKE_BUILD_PARALLEL_LEVEL=%CMAKE_BUILD_PARALLEL_LEVEL% -^> %SICNU_BUILD_CAP% 1>&2
      set "CMAKE_BUILD_PARALLEL_LEVEL=%SICNU_BUILD_CAP%"
    )
  )
)
if not "%CTEST_PARALLEL_LEVEL%"=="" (
  echo %CTEST_PARALLEL_LEVEL%| findstr /r "^[0-9][0-9]*$" >nul && (
    if %CTEST_PARALLEL_LEVEL% GTR 1 (
      echo build.cmd: clamped CTEST_PARALLEL_LEVEL=%CTEST_PARALLEL_LEVEL% -^> 1 1>&2
      set "CTEST_PARALLEL_LEVEL=1"
    )
  )
)
exit /b 0

rem ---------------------------------------------------------------------------
rem selftest - hermetic cap + quoting assertions (no toolchain required)
rem ---------------------------------------------------------------------------
:cmd_selftest
shift
set "ST_FAILED=0"
echo build.cmd selftest: resource caps + path/space quoting

rem The default is 1 on EVERY lane; an inherited CMAKE_BUILD_PARALLEL_LEVEL
rem (scripts/windows/_env.cmd pins 2 per ADR 0147) only clamps, it never sets
rem the request - same rule as scripts/build.sh.
call :resolve_jobs ""
if not "%JOBS%"=="1" (echo selftest FAIL: default jobs expected 1, got %JOBS% 1>&2 & set "ST_FAILED=1") else echo   ok: default jobs = 1
call :resolve_jobs "1"
if not "%JOBS%"=="1" (echo selftest FAIL: jobs 1 1>&2 & set "ST_FAILED=1") else echo   ok: jobs 1
call :resolve_jobs "2"
if not "%JOBS%"=="2" (echo selftest FAIL: jobs 2 ^(cap^) 1>&2 & set "ST_FAILED=1") else echo   ok: jobs 2 ^(cap^)
call :resolve_jobs "3" 2>nul
set "RC3=%ERRORLEVEL%"
if not "%RC3%"=="2" (echo selftest FAIL: jobs 3 expected exit 2, got %RC3% 1>&2 & set "ST_FAILED=1") else echo   ok: jobs 3 refused with exit 2
call :resolve_jobs "8" 2>nul
set "RC8=%ERRORLEVEL%"
if not "%RC8%"=="2" (echo selftest FAIL: jobs 8 expected exit 2, got %RC8% 1>&2 & set "ST_FAILED=1") else echo   ok: jobs 8 refused with exit 2

rem --- quoting: build a fixture directory with spaces + Unicode via PowerShell
rem (avoids batch-file codepage mangling of non-ASCII literals), then operate
rem from inside it and pass spaced arguments through the wrapper.
for /f "usebackq delims=" %%F in (`powershell -NoProfile -Command "$env:TEMP + '\s' + [char]0xfc + ' b' + [char]0xfc + 'ild ' + [char]0x8def + [char]0x5f84 + '\build portability'"`) do set "FIXTURE=%%F"
if "%FIXTURE%"=="" (echo selftest FAIL: could not build fixture path 1>&2 & set "ST_FAILED=1" & goto :st_quoting_done)
if exist "%FIXTURE%" rmdir /s /q "%FIXTURE%" 2>nul
mkdir "%FIXTURE%" 2>nul || (echo selftest FAIL: cannot create %FIXTURE% 1>&2 & set "ST_FAILED=1" & goto :st_quoting_done)

rem Arguments containing spaces must arrive as ONE argument each.
rem NOTE: the capture writes to a file and counts it with usebackq file iteration
rem on purpose - `for /f ... in ('cmd "arg with spaces"')` re-parses the command
rem through a second cmd /c layer that mangles quoted spaced arguments (CreateProcess
rem error 183), so a direct invocation + file capture is the reliable pattern here.
set "ARGOUT=0"
set "ARGFILE=%FIXTURE%\echo-args.txt"
call "%SELF%" --echo-args "%FIXTURE%\a b" "%FIXTURE%\c d" plain > "%ARGFILE%" 2>&1
if errorlevel 1 (echo selftest FAIL: --echo-args exited %ERRORLEVEL% 1>&2 & set "ST_FAILED=1")
for /f "usebackq delims=" %%L in ("%ARGFILE%") do set /a ARGOUT+=1
rem Two spaced args (the fixture path carries spaces and non-ASCII characters)
rem plus one plain arg must yield exactly 3 output lines - no word splitting.
if not "%ARGOUT%"=="3" (echo selftest FAIL: echo-args produced %ARGOUT% lines, expected 3 1>&2 & set "ST_FAILED=1") else echo   ok: spaced args stay one argument each

pushd "%FIXTURE%"
call "%SELF%" --print-jobs >nul 2>&1
if errorlevel 1 (echo selftest FAIL: wrapper failed from spaced/Unicode cwd 1>&2 & set "ST_FAILED=1") else echo   ok: wrapper runs from spaced/Unicode cwd
popd

rmdir /s /q "%FIXTURE%" 2>nul
:st_quoting_done

if "%ST_FAILED%"=="0" (echo selftest: ALL PASS & exit /b 0)
echo selftest: FAILED 1>&2
exit /b 1

rem ---------------------------------------------------------------------------
rem run_logged <name> <cmd...>: redirect to log dir, type it back, SUMMARY on failure
rem NOTE: %* does NOT reflect shift inside a called label, so the command is
rem rebuilt argument by argument, each quoted - that is what keeps paths with
rem spaces (C:/Program Files/...) in one piece through the final `call`.
rem ---------------------------------------------------------------------------
:run_logged
set "LGNAME=%~1"
shift
set "RLARGS="
:rl_collect
if "%~1"=="" goto :rl_run
set "RLARGS=%RLARGS% "%~1""
shift
goto :rl_collect
:rl_run
set "LGDIR=%LOG_ROOT%\%LGNAME%"
if not exist "%LGDIR%" mkdir "%LGDIR%"
call %RLARGS% > "%LGDIR%\run.log" 2>&1
set "LGRC=%ERRORLEVEL%"
type "%LGDIR%\run.log"
if not "%LGRC%"=="0" (
  > "%LGDIR%\SUMMARY.txt" echo command: %RLARGS%
  >> "%LGDIR%\SUMMARY.txt" echo exit: %LGRC%
  >> "%LGDIR%\SUMMARY.txt" echo --- error/failed lines ---
  findstr /n /r /c:"error:" /c:"FAILED:" /c:"failed" /c:"CMake Error" "%LGDIR%\run.log" >> "%LGDIR%\SUMMARY.txt"
  >> "%LGDIR%\SUMMARY.txt" echo --- last 20 lines ---
  for /f "delims=" %%L in ('powershell -NoProfile -Command "Get-Content -Tail 20 '%LGDIR%\run.log'"') do >> "%LGDIR%\SUMMARY.txt" echo %%L
  echo build.cmd: FAILED ^(%LGNAME%^) - see %LGDIR%\SUMMARY.txt 1>&2
)
exit /b %LGRC%

rem ---------------------------------------------------------------------------
:cmd_doctor
shift
set "BDIR=%BUILD_DIR_DEFAULT%"
:doctor_args
if "%~1"=="--build-dir" (set "BDIR=%~2" & shift & shift & goto :doctor_args)
if not "%~1"=="" (echo build.cmd: doctor: unknown option: %~1 1>&2 & exit /b 2)
if not exist "%BDIR%\CMakeCache.txt" (
  echo build.cmd: doctor: not configured: %BDIR%\CMakeCache.txt 1>&2
  echo   run: scripts\windows\build.cmd configure --build-dir %BDIR% 1>&2
  exit /b 1
)
echo === SICNU dependency summary ^(%BDIR%^) ===
set "MISSING=0"
for %%K in (Qt6_DIR VCPKG_INSTALLED_DIR CMAKE_TOOLCHAIN_FILE Qt6Keychain_DIR QCA_INCLUDE_DIR BISON_EXECUTABLE FLEX_EXECUTABLE) do call :cache_line "%%K"
if "%MISSING%"=="0" (echo doctor: no obvious holes & exit /b 0)
echo doctor: see [missing] rows above 1>&2
exit /b 1

:cache_line
for /f "usebackq delims=" %%V in (`powershell -NoProfile -Command "$m=Select-String -Path '%BDIR%\CMakeCache.txt' -Pattern '^%~1:[A-Z]*=(.*)$' | Select-Object -Last 1; if($m){$m.Matches[0].Groups[1].Value}"`) do set "CV=%%V"
if not "%CV%"=="" (
  if exist "%CV%\*" (echo   [ok] %~1 = %CV%) else (if exist "%CV%" (echo   [ok] %~1 = %CV%) else (echo   [missing] %~1 = %CV% & set "MISSING=1"))
)
exit /b 0

rem ---------------------------------------------------------------------------
:cmd_configure
shift
set "BDIR=%BUILD_DIR_DEFAULT%"
set "PRESET="
:configure_args
if "%~1"=="--build-dir" (set "BDIR=%~2" & shift & shift & goto :configure_args)
if "%~1"=="--preset" (set "PRESET=%~2" & shift & shift & goto :configure_args)
if defined PRESET (
  call :run_logged configure cmake --preset "%PRESET%"
) else (
  call :run_logged configure cmake -S "%REPO_ROOT%" -B "%BDIR%"
)
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------------------
:cmd_build
shift
set "BDIR=%BUILD_DIR_DEFAULT%"
set "XJOBS="
:build_args
if "%~1"=="--build-dir" (set "BDIR=%~2" & shift & shift & goto :build_args)
if "%~1"=="--jobs" (set "XJOBS=%~2" & shift & shift & goto :build_args)
call :resolve_jobs "%XJOBS%" || exit /b 2
set "BUILDCMD=cmake --build "%BDIR%" --parallel %JOBS%"
if not "%~1"=="" set "BUILDCMD=%BUILDCMD% -- %*"
rem Delegate through a subshell so the command line with targets and spaces
rem reaches cmake as one argument list.
call :run_logged build %BUILDCMD%
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------------------
:cmd_test
shift
set "BDIR=%BUILD_DIR_DEFAULT%"
set "REGEX="
:test_args
if "%~1"=="--build-dir" (set "BDIR=%~2" & shift & shift & goto :test_args)
if "%~1"=="-R" (set "REGEX=%~2" & shift & shift & goto :test_args)
set "TESTCMD=ctest --test-dir "%BDIR%" --output-on-failure -j1"
if not "%REGEX%"=="" set "TESTCMD=%TESTCMD% -R "%REGEX%""
set "QT_QPA_PLATFORM=offscreen"
call :run_logged test %TESTCMD%
exit /b %ERRORLEVEL%

rem ---------------------------------------------------------------------------
:cmd_smoke
shift
set "BDIR=%REPO_ROOT%\build-smoke"
set "BASECACHE="
set "CATCH2SRC="
:smoke_args
if "%~1"=="--build-dir" (set "BDIR=%~2" & shift & shift & goto :smoke_args)
if "%~1"=="--base-cache" (set "BASECACHE=%~2" & shift & shift & goto :smoke_args)
if "%~1"=="--catch2-source" (set "CATCH2SRC=%~2" & shift & shift & goto :smoke_args)
rem A relative build dir would land in the CURRENT directory (e.g. under
rem scripts\windows); anchor it to the repo root like the POSIX wrapper does.
if "%BDIR:~1,2%"==":\" goto :smoke_bdir_done
if "%BDIR:~1,1%"==":" goto :smoke_bdir_done
set "BDIR=%REPO_ROOT%\%BDIR%"
:smoke_bdir_done
if exist "%BDIR%" (
  echo build.cmd: removing stale build dir %BDIR% ^(clean-tree gate^)
  rmdir /s /q "%BDIR%" 2>nul
  rem Windows indexers/AV scanners routinely hold a handle for a second after
  rem a configure; retry once so the gate is not flaky for that reason alone.
  if exist "%BDIR%" (
    timeout /t 3 /nobreak >nul 2>&1
    rmdir /s /q "%BDIR%" 2>nul
  )
)
mkdir "%BDIR%" 2>nul
if exist "%BDIR%\CMakeCache.txt" (
  echo build.cmd: smoke: cannot clean %BDIR% ^(another process holds it?^) 1>&2
  exit /b 1
)
rem Seed only toolchain LOCATION variables from an existing configured tree -
rem every dependency is re-discovered fresh, and the vcpkg installed tree is
rem reused read-only so the gate stays offline (same discipline as build.sh).
rem Seed toolchain locations through a CMake initial-cache script (-C) instead
rem of -D command-line arguments: values here routinely contain spaces
rem (C:/Program Files/...), and a nested for/f + quoted-token chain through
rem run_logged mangles them. A script file has no quoting problem, and every
rem dependency is still re-discovered fresh (only locations are seeded; the
rem vcpkg installed tree is reused read-only so the gate stays offline).
rem NOTE: this block uses delayed expansion (!VAR!) on purpose - %VAR% inside a
rem parenthesized block is expanded when the BLOCK is parsed, i.e. before the
rem set commands inside it have run.
set "SEED="
if not "%BASECACHE%"=="" goto :smoke_seed
if not "%CATCH2SRC%"=="" goto :smoke_seed
goto :smoke_seed_done
:smoke_seed
if not "%BASECACHE%"=="" (
  set "INIT=!BDIR!\seed-toolchain.cmake"
  powershell -NoProfile -ExecutionPolicy Bypass -File "!SELF_DIR!seed_toolchain_cache.ps1" -BaseCache "%BASECACHE%" -Out "!INIT!" -Catch2Source "%CATCH2SRC%"
  if errorlevel 1 (echo build.cmd: smoke: seeding failed 1>&2 & exit /b 1)
  if not exist "!INIT!" (echo build.cmd: smoke: seed script not written 1>&2 & exit /b 1)
  set "SEED=-C "!INIT!""
  rem quoted: a called batch label splits arguments at '=', so an unquoted
  rem -DKEY=VALUE would arrive at the callee as two arguments.
  set "SEED=!SEED! "-DVCPKG_MANIFEST_MODE=OFF""
  echo build.cmd: seeded toolchain locations from %BASECACHE% ^(dependencies re-discovered fresh^)
) else (
  set "INIT=!BDIR!\seed-offline.cmake"
  powershell -NoProfile -ExecutionPolicy Bypass -File "!SELF_DIR!seed_toolchain_cache.ps1" -Out "!INIT!" -Catch2Source "%CATCH2SRC%"
  if errorlevel 1 (echo build.cmd: smoke: seeding failed 1>&2 & exit /b 1)
  if not exist "!INIT!" (echo build.cmd: smoke: seed script not written 1>&2 & exit /b 1)
  set "SEED=-C "!INIT!""
  echo build.cmd: seeded offline Catch2 source ^(no base cache given^)
)
:smoke_seed_done
if not "%CATCH2SRC%"=="" (
  rem Offline escape hatch: the repo's own Catch2 FetchContent (locked tag
  rem v3.7.1) is the only network step in configure. The prepared clone goes
  rem through the same -C initial-cache script as the toolchain keys, so no
  rem path with spaces ever travels the command line.
  echo build.cmd: using local Catch2 source %CATCH2SRC% ^(via the -C script^)
)
call :run_logged configure cmake -G Ninja -S "%REPO_ROOT%" -B "%BDIR%" %SEED%
if errorlevel 1 (echo build.cmd: smoke: clean-tree configure FAILED 1>&2 & exit /b 1)
findstr /c:"=== SICNU dependency summary ===" "%LOG_ROOT%\configure\run.log" >nul
if errorlevel 1 (
  echo build.cmd: smoke: FAILED - the configure log has no dependency summary block 1>&2
  echo   ^(cmake\SicnuDepDoctor.cmake wiring missing? see %LOG_ROOT%\configure\run.log^) 1>&2
  exit /b 1
)
echo build.cmd: smoke: clean-tree configure OK
echo   build dir: %BDIR% ^(started empty; no stale cache reused^)
echo   configure log: %LOG_ROOT%\configure\run.log
echo   dependency summary: present
exit /b 0


rem ---------------------------------------------------------------------------
:main
call "%~dp0_env.cmd"
rem MSVC builds need the developer environment (rc.exe, INCLUDE, LIB) before any
rem configure/build - the same step the landed setup.cmd performs. The guard
rem makes a pre-loaded environment (e.g. a Developer Prompt) reusable.
if defined SICNU_VCVARS if not defined SICNU_VCVARS_LOADED (
  call "%SICNU_VCVARS%" >nul 2>&1
  if errorlevel 1 (echo build.cmd: vcvars64.bat failed: %SICNU_VCVARS% 1>&2 & exit /b 1)
  set "SICNU_VCVARS_LOADED=1"
)
if "%~1"=="--print-jobs" goto :mode_print_jobs
if "%~1"=="--echo-args" goto :mode_echo_args
if "%~1"=="selftest" goto :cmd_selftest
if "%~1"=="doctor" goto :cmd_doctor
if "%~1"=="configure" goto :cmd_configure
if "%~1"=="build" goto :cmd_build
if "%~1"=="test" goto :cmd_test
if "%~1"=="smoke" goto :cmd_smoke
echo usage: build.cmd ^[selftest ^| doctor ^| configure ^| build ^| test ^| smoke^] 1>&2
exit /b 2

rem NOTE: dispatch uses goto+label (never "shift" inside a parenthesized block):
rem Batch expands %1/%JOBS% at PARSE time, so a shift inside parentheses cannot
rem affect the arguments the label afterwards sees - the classic quoting bug.
:mode_print_jobs
call :resolve_jobs ""
echo %JOBS%
exit /b %ERRORLEVEL%

:mode_echo_args
shift
:mode_echo_args_loop
if "%~1"=="" exit /b 0
echo ^<%~1^>
shift
goto :mode_echo_args_loop
