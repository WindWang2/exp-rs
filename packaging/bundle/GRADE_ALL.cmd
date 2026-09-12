@echo off
rem GRADE_ALL.cmd — batch-grade a folder of student submissions offline (D7).
rem
rem Usage (from the bundle root):
rem   GRADE_ALL.cmd <submissions_dir> [lab_id] [out.csv]
rem     submissions_dir  one artifact file per student; student_id = file name
rem                      without extension (e.g. 20240101.tif).
rem     lab_id           grading rules id (default: ndvi_basics).
rem     out.csv          result CSV (default: grades.csv), UTF-8 with BOM so
rem                      Excel shows Chinese correctly.
rem
rem The run streams one submission at a time: a corrupt or unreadable file is
rem recorded in the CSV and does NOT abort the remaining submissions.
setlocal
set "BUNDLE=%~dp0"
for %%I in ("%BUNDLE:~0,-1%") do set "BUNDLE=%%~fI"
cd /d "%BUNDLE%"
set "SICNU_OFFLINE=1"
if not defined QT_QPA_PLATFORM set "QT_QPA_PLATFORM=offscreen"
set "PATH=%BUNDLE%\bin;%PATH%"

if "%~1"=="" (
  echo usage: GRADE_ALL.cmd ^<submissions_dir^> [lab_id] [out.csv]
  echo   example: GRADE_ALL.cmd D:\lab1_submissions ndvi_basics grades.csv
  exit /b 2
)
set "SUBS=%~f1"
set "LAB=%~2"
if "%LAB%"=="" set "LAB=ndvi_basics"
set "CSV=%~3"
if "%CSV%"=="" set "CSV=grades.csv"

if not exist "%SUBS%\" (
  echo ERROR: submissions folder not found: %SUBS%
  exit /b 2
)
if not exist "bin\sicnu_geo_rs_cli.exe" (
  echo ERROR: bin\sicnu_geo_rs_cli.exe missing — bundle incomplete.
  exit /b 1
)

echo Batch grading lab "%LAB%" over "%SUBS%" into "%CSV%" ...
bin\sicnu_geo_rs_cli.exe --offline lab --lab "%LAB%" --batch "%SUBS%" --csv "%CSV%"
set "RC=%errorlevel%"
echo.
if "%RC%"=="0" (
  echo Done. All submissions graded. CSV: %CSV%
) else (
  echo Finished with %RC%^) — some submissions were isolated as errors ^(rows kept in the CSV^).
)
exit /b %RC%
