@echo off
rem GRADE_ALL.cmd - batch-grade a folder of student submissions offline (D7).
rem
rem Usage (from the bundle root):
rem   GRADE_ALL.cmd <submissions_dir> [lab_id] [out.csv]
rem     submissions_dir  one artifact file per student; student_id = file name
rem                      without extension (e.g. 20240101.tif).
rem     lab_id           grading rules id (default: ndvi_basics).
rem     out.csv          result CSV (default: grades.csv next to the
rem                      submissions folder), UTF-8 with BOM + CRLF so Excel
rem                      opens Chinese correctly.
rem
rem The run streams one submission at a time: a corrupt or unreadable file is
rem recorded in the CSV and does NOT abort the remaining submissions.
rem ASCII-only text: classroom consoles are not guaranteed to be UTF-8.
setlocal
set "BUNDLE=%~dp0"
for %%I in ("%BUNDLE:~0,-1%") do set "BUNDLE=%%~fI"
cd /d "%BUNDLE%"
set "SICNU_OFFLINE=1"
set "SICNU_LAB_RULES_DIR=%BUNDLE%\data\labs\grading"
set "PROJ_DATA=%BUNDLE%\data\runtime\proj"
set "GDAL_DATA=%BUNDLE%\data\runtime\gdal"
if not defined QT_QPA_PLATFORM set "QT_QPA_PLATFORM=offscreen"
set "PATH=%BUNDLE%\bin;%PATH%"

if "%~1"=="" (
  echo usage: GRADE_ALL.cmd ^<submissions_dir^> [lab_id] [out.csv]
  echo   example: GRADE_ALL.cmd D:\lab1_submissions ndvi_basics grades.csv
  pause
  exit /b 2
)
set "SUBS=%~f1"
set "LAB=%~2"
if "%LAB%"=="" set "LAB=ndvi_basics"
set "CSV=%~3"
if "%CSV%"=="" set "CSV=%SUBS%\grades.csv"

if not exist "%SUBS%\" (
  echo ERROR: submissions folder not found: %SUBS%
  pause
  exit /b 2
)
if not exist "bin\sicnu_geo_rs_cli.exe" (
  echo ERROR: bin\sicnu_geo_rs_cli.exe missing - bundle incomplete.
  pause
  exit /b 1
)

echo Batch grading lab "%LAB%" over "%SUBS%" into "%CSV%" ...
bin\sicnu_geo_rs_cli.exe --offline lab --lab "%LAB%" --batch "%SUBS%" --csv "%CSV%"
set "RC=%errorlevel%"
echo.
if "%RC%"=="0" (
  echo Done. All submissions graded. CSV: %CSV%
) else (
  echo Finished with isolated error rows ^(exit %RC%^) - they are kept in the CSV.
)
pause
exit /b %RC%
