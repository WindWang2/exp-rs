@echo off
rem R24-A science track configure (mirrors prior tracks' vcvars64 + Ninja pattern,
rem vcpkg_installed pre-populated from the identical-manifest build-dev tree).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-r24_a-20261027T022732Z
cmake -S . -B build-r24_a -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install" -DCMAKE_TOOLCHAIN_FILE=C:\deps\vcpkg\scripts\buildsystems\vcpkg.cmake -DVCPKG_MANIFEST_INSTALL=OFF -DVCPKG_TARGET_TRIPLET=x64-windows -DBISON_EXECUTABLE=C:/deps/winflexbison/win_bison.exe -DFLEX_EXECUTABLE=C:/deps/winflexbison/win_flex.exe %*
