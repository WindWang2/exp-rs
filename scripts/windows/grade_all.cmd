@echo off
rem grade_all.cmd — repo-side batch grading over a submissions folder (D7).
rem Usage: scripts\windows\grade_all.cmd <submissions_dir> [lab_id] [out.csv]
rem Streams one submission at a time; corrupt files are isolated as CSV rows,
rem never aborting the run. UTF-8 BOM CSV for Excel.
setlocal
call "%~dp0_env.cmd"

set "BUILD=%SICNU_REPO_ROOT%\build-dev"
set "CLI=%BUILD%\sicnu_geo_rs_cli.exe"
if not exist "%CLI%" ( echo ERROR: %CLI% not found — run setup.cmd first & exit /b 1 )

set "SICNU_OFFLINE=1"
set "PATH=%BUILD%;%SICNU_QT_DIR%\bin;%SICNU_QCA_DIR%\bin;%SICNU_KEYCHAIN_DIR%\bin;%SICNU_VCPKG_INSTALLED%\x64-windows\bin;%PATH%"
cd /d "%SICNU_REPO_ROOT%"

if "%~1"=="" (
  echo usage: grade_all.cmd ^<submissions_dir^> [lab_id] [out.csv]
  exit /b 2
)
set "LAB=%~2"
if "%LAB%"=="" set "LAB=ndvi_basics"
set "CSV=%~3"
if "%CSV%"=="" set "CSV=grades.csv"

"%CLI%" --offline lab --lab "%LAB%" --batch "%~f1" --csv "%CSV%"
set "RC=%errorlevel%"
if "%RC%"=="0" ( echo Done — all submissions graded, CSV: %CSV%
) else ( echo Finished (exit %RC%^) — error rows are isolated in the CSV )
exit /b %RC%
