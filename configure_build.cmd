@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d C:\Users\wangj.KEVIN\projects\exp-rs-pi-harness-4
set CMAKE_BUILD_PARALLEL_LEVEL=2
set CTEST_PARALLEL_LEVEL=1
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe -DCMAKE_C_COMPILER="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130/bin/Hostx64/x64/cl.exe" -DCMAKE_CXX_COMPILER="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130/bin/Hostx64/x64/cl.exe" -DCMAKE_TOOLCHAIN_FILE=C:/deps/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_INSTALLED_DIR=C:/Users/wangj.KEVIN/projects/exp-rs-win/build-win/vcpkg_installed -DBISON_EXECUTABLE=C:/deps/winflexbison/win_bison.exe -DFLEX_EXECUTABLE=C:/deps/winflexbison/win_flex.exe -DQt6Keychain_DIR=C:/deps/kc-install/lib/cmake/Qt6Keychain -DQCA_INCLUDE_DIR=C:/deps/qca-install/include/Qca-qt6/QtCrypto -DQCA_LIBRARY=C:/deps/qca-install/lib/qca-qt6.lib -DCMAKE_PREFIX_PATH="C:/deps/Qt/6.8.0/msvc2022_64"
