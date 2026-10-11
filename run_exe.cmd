@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set SRC=C:\Users\wangj.KEVIN\projects\exp-rs-r8-science
set BUILD=%SRC%\build-r8sci
set PATH=%BUILD%;C:\deps\Qt\6.8.0\msvc2022_64\bin;C:\deps\qca-install\bin;C:\deps\kc-install\bin;%BUILD%\vcpkg_installed\x64-windows\debug\bin;%BUILD%\vcpkg_installed\x64-windows\bin;%PATH%
set PROJ_DATA=%BUILD%\vcpkg_installed\x64-windows\share\proj
set PROJ_LIB=%BUILD%\vcpkg_installed\x64-windows\share\proj
cd /d "%BUILD%\tests"
%BUILD%\%~1.exe %2 %3 %4 > "%SRC%\.planning\r8-science-algorithm-correctness\exe_%~1_%~2.log" 2>&1
echo RC=%errorlevel%
