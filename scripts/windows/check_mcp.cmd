@echo off
rem check_mcp.cmd — verify `sicnu_geo_rs --mcp` answers one discovery round
rem trip over Windows stdio (goal D7 / D9 runtime precondition).
rem
rem Spawns the binary in MCP mode, sends initialize + notifications/initialized
rem + tools/list (newline-delimited JSON-RPC), and asserts the responses name
rem exp-rs-mcp and carry a tools array. Fully local — no network.
setlocal
call "%~dp0_env.cmd"

set "BUILD=%SICNU_REPO_ROOT%\build-dev"
set "EXE=%BUILD%\sicnu_geo_rs.exe"
if not exist "%EXE%" ( echo ERROR: %EXE% not found — build the sicnu_geo_rs target first & exit /b 1 )

set "SICNU_OFFLINE=1"
set "QT_QPA_PLATFORM=offscreen"
set "PATH=%BUILD%;%SICNU_QT_DIR%\bin;%SICNU_QCA_DIR%\bin;%SICNU_KEYCHAIN_DIR%\bin;%SICNU_VCPKG_INSTALLED%\x64-windows\bin;%PATH%"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0check_mcp_stdio.ps1" -Exe "%EXE%"
exit /b %errorlevel%
