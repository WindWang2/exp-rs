#!/usr/bin/env bash
# MSVC host environment for the exp-rs worktrees, WITHOUT vcvars64.bat.
#
# Why: vcvars64.bat internally invokes reg.exe, which the sandbox program
# blacklist blocks, so `call vcvars64.bat` cannot run here. The variables it
# would set are reproduced explicitly below (values read from the warm
# build-dev/CMakeCache.txt, not guessed).
#
# Usage: source tools/verification12/msvc_env.sh

MSVC_ROOT="/c/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130"
SDK_ROOT="/c/Program Files (x86)/Windows Kits/10"
SDK_VER="10.0.22621.0"

export PATH="$MSVC_ROOT/bin/Hostx64/x64:$SDK_ROOT/bin/$SDK_VER/x64:/c/Qt/Tools/Ninja:/c/deps/winflexbison:$PATH"

# Windows-style paths are what the toolchain expects.
export INCLUDE="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130/include;C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/ucrt;C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/shared;C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/um;C:/Program Files (x86)/Windows Kits/10/Include/10.0.22621.0/winrt"

export LIB="C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.38.33130/lib/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/ucrt/x64;C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/x64"

export CMAKE_BUILD_PARALLEL_LEVEL=1
export CTEST_PARALLEL_LEVEL=1
