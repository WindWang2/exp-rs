@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-r6-persistence
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
if "%1"=="cfg" (
  C:\Qt\Tools\CMake_64\bin\cmake.exe -B build-dev -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_PREFIX_PATH="C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install" -DVCPKG_INSTALLED_DIR=C:/Users/wangj.KEVIN/projects/exp-rs/build-dev/vcpkg_installed -DCMAKE_TOOLCHAIN_FILE=C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake -DBISON_EXECUTABLE=C:/deps/winflexbison/win_bison.exe -DFLEX_EXECUTABLE=C:/deps/winflexbison/win_flex.exe
) else (
  C:\Qt\Tools\CMake_64\bin\cmake.exe --build build-dev -j2 %1 %2 %3 %4
)
