@echo off
rem VERIFY.cmd - check this bundle's integrity (every file against the
rem SHA-256 manifest). Use after copying to a USB stick or a new machine.
setlocal
set "BUNDLE=%~dp0"
for %%I in ("%BUNDLE:~0,-1%") do set "BUNDLE=%%~fI"
powershell -NoProfile -ExecutionPolicy Bypass -File "%BUNDLE%\VERIFY.ps1" -Root "%BUNDLE%"
set "RC=%errorlevel%"
if "%RC%"=="0" (
  echo Bundle integrity OK.
) else (
  echo Bundle integrity FAILED - copy the bundle again from the original.
)
pause
exit /b %RC%
