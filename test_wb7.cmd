@echo off
rem test_wb7.cmd <exe names...> - resource-bounded direct test runner
setlocal enabledelayedexpansion
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-professional-workbench-7\build-dev
set QT_QPA_PLATFORM=offscreen
set PATH=C:\Users\wangj.KEVIN\projects\exp-rs-professional-workbench-7\build-dev;C:\deps\Qt\6.8.0\msvc2022_64\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin;C:\Users\wangj.KEVIN\projects\exp-rs-win\build-win\vcpkg_installed\x64-windows\bin;%PATH%
for %%E in (%*) do (
  echo ==== %%E ====
  %%E.exe --reporter compact
  set RC=!errorlevel!
  rem crash exit codes are NEGATIVE - "if errorlevel 1" would miss them
  if not "!RC!"=="0" (
    echo RESULT %%E FAIL rc=!RC!
  ) else (
    echo RESULT %%E PASS
  )
)
exit /b 0
