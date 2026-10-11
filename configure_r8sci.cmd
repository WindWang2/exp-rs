@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;C:\deps\winflexbison;%PATH%
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-r8-science
set CMAKE_BUILD_PARALLEL_LEVEL=4
set CTEST_PARALLEL_LEVEL=2
cmake -S . -B build-r8sci -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install" -DCMAKE_TOOLCHAIN_FILE=C:\deps\vcpkg\scripts\buildsystems\vcpkg.cmake -DFETCHCONTENT_SOURCE_DIR_CATCH2="C:/deps/catch2-src" %*
