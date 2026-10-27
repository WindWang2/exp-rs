#!/bin/sh
# tests/test_r23_b_provider_stub_compile.sh — R23-B provider TU gate.
#
# The optional TensorRT/OpenVINO provider translation units only compile when
# SICNU_ENABLE_TENSORRT/SICNU_ENABLE_OPENVINO are ON, and no CI lane or host
# build turns those options on — so a union merge that drops a definition or
# renames a member in those files ships silently (exactly what merge
# 9988aa15a9 did to #1576/#1577: a dangling selectDevice() call, an
# undeclared m_artifactPath and a const method locking a non-mutable mutex).
# This gate compiles both TUs with -fsyntax-only against the minimal stub SDK
# headers in tests/fixtures/provider_stubs so any such loss is a hard test
# failure. Syntax-only: no SDK runtime, no linking, offline.
#
# Usage (registered from tests/CMakeLists.txt):
#   sh test_r23_b_provider_stub_compile.sh --repo-root <repo> --cxx <compiler> \
#        -I <include-dir> [-I <include-dir> ...]
set -eu

repo_root=''
cxx=''
includes=''

while [ $# -gt 0 ]; do
  case "$1" in
    --repo-root) repo_root="$2"; shift 2 ;;
    --cxx) cxx="$2"; shift 2 ;;
    -I) includes="$includes -I $2"; shift 2 ;;
    *) echo "test_r23_b_provider_stub_compile.sh: unknown argument: $1" >&2; exit 2 ;;
  esac
done

if [ -z "$repo_root" ] || [ -z "$cxx" ]; then
  echo "test_r23_b_provider_stub_compile.sh: --repo-root and --cxx are required" >&2
  exit 2
fi

stubs="$repo_root/tests/fixtures/provider_stubs"
for header in openvino/openvino.hpp NvInfer.h NvInferVersion.h cuda_runtime_api.h; do
  if [ ! -f "$stubs/$header" ]; then
    echo "missing stub header: $stubs/$header" >&2
    exit 2
  fi
done

# shellcheck disable=SC2086  # includes is intentionally word-split flag pairs
compile_tu()
{
  tu="$1"
  define="$2"
  # shellcheck disable=SC2086
  "$cxx" -fsyntax-only -std=gnu++20 -fPIC "$define" \
    -I "$repo_root/src" -I "$stubs" $includes "$repo_root/src/operators/runtime/$tu"
}

fail=0
for tu_def in 'openvino_provider.cpp -DSICNU_WITH_OPENVINO' \
              'tensorrt_provider.cpp -DSICNU_WITH_TENSORRT'; do
  # shellcheck disable=SC2086
  if compile_tu $tu_def; then
    echo "stub-compile OK: ${tu_def%% *}"
  else
    echo "stub-compile FAILED: ${tu_def%% *} (see compiler errors above)" >&2
    fail=1
  fi
done

exit "$fail"
