@echo off
rem R23-D ctest wrapper: isolated build-D, offscreen Qt, vcpkg DLLs on PATH.
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CTEST_PARALLEL_LEVEL=1
set QT_QPA_PLATFORM=offscreen
set QT_QPA_PLATFORM_PLUGIN_PATH=C:\deps\Qt\6.8.0\msvc2022_64\plugins\platforms
set PROJ_DATA=C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\vcpkg_installed\x64-windows\share\proj
set GDAL_DATA=C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\vcpkg_installed\x64-windows\share\gdal
set PATH=C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D\vcpkg_installed\x64-windows\bin;C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z\build-D;%PATH%
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z
ctest --test-dir build-D --output-on-failure --parallel 1 %*
exit /b %ERRORLEVEL%
