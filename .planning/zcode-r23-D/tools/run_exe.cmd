@echo off
rem usage: run_exe.cmd <exe-relative-path> [catch2 args...]
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set QT_QPA_PLATFORM=offscreen
set QT_QPA_PLATFORM_PLUGIN_PATH=C:\deps\Qt\6.8.0\msvc2022_64\plugins\platforms
set PROJ_DATA=C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\vcpkg_installed\x64-windows\share\proj
set GDAL_DATA=C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\vcpkg_installed\x64-windows\share\gdal
set PATH=C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\vcpkg_installed\x64-windows\bin;C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D;C:\deps\Qt\6.8.0\msvc2022_64\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin;%PATH%
C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\%1 %2 %3 %4 %5
exit /b %ERRORLEVEL%
