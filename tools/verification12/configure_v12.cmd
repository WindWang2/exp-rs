@echo off
REM Configure the verification-12 worktree build dir (mirrors configure_dev.cmd).
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;C:\deps\winflexbison;%PATH%
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-worktrees\glm53-scientific-verification-12
set CMAKE_BUILD_PARALLEL_LEVEL=1
set CTEST_PARALLEL_LEVEL=1
C:\Qt\Tools\CMake_64\bin\cmake.exe -S . -B build-v12 -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DENABLE_TESTS=ON ^
  -DENABLE_LOCAL_BUILD_SHORTCUTS=ON ^
  -DCMAKE_TOOLCHAIN_FILE="C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake" ^
  -DCMAKE_PREFIX_PATH="C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install" ^
  -DFETCHCONTENT_SOURCE_DIR_CATCH2="C:/deps/catch2-src"
