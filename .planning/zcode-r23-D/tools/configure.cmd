@echo off
rem R23-D isolated build configure (worktree exp-rs-D-20261026T165151Z)
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set PATH=C:\Qt\Tools\Ninja;%PATH%
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
set QT_QPA_PLATFORM=offscreen
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-D-20261026T165151Z
cmake -S . -B build-D -G Ninja ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DENABLE_TESTS=ON ^
  -DCMAKE_TOOLCHAIN_FILE=C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_INSTALLED_DIR=C:/Users/wangj.KEVIN/projects/exp-rs-D-20261026T165151Z/build-D/vcpkg_installed ^
  -DVCPKG_MANIFEST_INSTALL=OFF ^
  -DCMAKE_PREFIX_PATH=C:\deps\Qt\6.8.0\msvc2022_64;C:\deps\qca-install;C:\deps\kc-install ^
  -DBISON_EXECUTABLE=C:/deps/winflexbison/win_bison.exe ^
  -DFLEX_EXECUTABLE=C:/deps/winflexbison/win_flex.exe ^
  -DSICNU_NODE_EXECUTABLE="C:/Program Files/nodejs/node.exe" ^
  %*
exit /b %ERRORLEVEL%
