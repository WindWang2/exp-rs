@echo off
rem setup.cmd — Windows one-click: dependency check -> configure -> build (D7).
rem Hardened descendant of configure_wb7.cmd / build_wb7.cmd: same toolchain,
rem same resource bounds (-j2), no hardwired user paths.
setlocal enabledelayedexpansion
call "%~dp0_env.cmd"

echo === SICNU GEO RS setup ===
echo repo root : %SICNU_REPO_ROOT%
echo.

rem ---------------- 1. dependency check --------------------------------------
set "MISSING=0"
call :need vcvars  SICNU_VCVARS
call :need cmake   SICNU_CMAKE
call :need ninja   SICNU_NINJA
if not exist "%SICNU_QT_DIR%\bin" (
  echo [missing] Qt6 kit: %SICNU_QT_DIR% ^(override with SICNU_QT_DIR^)
  set "MISSING=1"
)
if not exist "%SICNU_WINFLEXBISON%\win_bison.exe" (
  echo [missing] win_flex_bison: %SICNU_WINFLEXBISON% ^(override with SICNU_WINFLEXBISON^)
  set "MISSING=1"
)
if "%MISSING%"=="1" (
  echo.
  echo Missing dependencies — install them or set the SICNU_* overrides, then re-run.
  exit /b 1
)
echo [ok] toolchain found. Details:
echo   vcvars : %SICNU_VCVARS%
echo   cmake  : %SICNU_CMAKE%
echo   ninja  : %SICNU_NINJA%
echo   qt     : %SICNU_QT_DIR%
echo.

rem ---------------- 2. vcvars -------------------------------------------------
call "%SICNU_VCVARS%" >nul 2>&1
if errorlevel 1 ( echo vcvars64.bat failed & exit /b 1 )

rem ---------------- 3. configure (mirrors configure_wb7.cmd) ------------------
if not exist "%SICNU_REPO_ROOT%\build-dev\CMakeCache.txt" (
  echo === configuring build-dev ===
  "%SICNU_CMAKE%" -S "%SICNU_REPO_ROOT%" -B "%SICNU_REPO_ROOT%\build-dev" -G Ninja ^
    -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON ^
    -DCMAKE_MAKE_PROGRAM="%SICNU_NINJA%" ^
    -DCMAKE_TOOLCHAIN_FILE="%SICNU_VCPKG%\scripts\buildsystems\vcpkg.cmake" ^
    -DVCPKG_INSTALLED_DIR="%SICNU_VCPKG_INSTALLED%" -DVCPKG_TARGET_TRIPLET=x64-windows ^
    -DSICNU_BUILD_OTB=OFF -DSICNU_EMBED_PYTHON=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF -DSICNU_VENDOR_GDAL=OFF ^
    -DBISON_EXECUTABLE="%SICNU_WINFLEXBISON%\win_bison.exe" -DFLEX_EXECUTABLE="%SICNU_WINFLEXBISON%\win_flex.exe" ^
    -DQt6Keychain_DIR="%SICNU_KEYCHAIN_DIR%\lib\cmake\Qt6Keychain" ^
    -DQCA_INCLUDE_DIR="%SICNU_QCA_DIR%\include\Qca-qt6\QtCrypto" ^
    -DQCA_LIBRARY="%SICNU_QCA_DIR%\lib\qca-qt6.lib" ^
    -DCMAKE_PREFIX_PATH="%SICNU_QT_DIR%" || exit /b 1
) else (
  echo === build-dev already configured — skipping configure ^(delete build-dev to force^) ===
)

rem ---------------- 4. build at the bounded parallelism ------------------------
echo === building ^(ninja -j2, CMAKE_BUILD_PARALLEL_LEVEL=%CMAKE_BUILD_PARALLEL_LEVEL%^) ===
"%SICNU_NINJA%" -C "%SICNU_REPO_ROOT%\build-dev" -j2 sicnu_geo_rs_cli sicnu_generate_samples || exit /b 1

echo.
echo === setup complete ===
echo Next: run_lab.cmd   (offline lab 1 end-to-end)
echo       grade_all.cmd (batch-grade a submissions folder)
exit /b 0

:need
if not defined %2 (
  echo [missing] %1 — set the corresponding SICNU_* override or install it.
  set "MISSING=1"
)
exit /b 0
