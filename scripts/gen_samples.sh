#!/bin/sh
# gen_samples.sh — generate the docs/labs sample data set (goal D1).
#
# usage:
#   scripts/gen_samples.sh [--profile=lab|stress] [--seed=<n>] [--spec=<path>]
#                          [--out=<dir>] [--verify]
#
# Locates the sicnu_generate_samples binary in a build tree next to this repo
# (build/, build-*/ — build*/tools/ included — or $SICNU_GENERATE_SAMPLES).
# Default output is data/samples; any flags, including --out=<dir> and
# --verify, are forwarded to the CLI (so a custom --out is honored by the
# verify pass too). Fully local; never blocks on CI.
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

bin=${SICNU_GENERATE_SAMPLES:-}
if [ -z "$bin" ]; then
  for candidate in \
      "$repo_root"/build/tools/sicnu_generate_samples \
      "$repo_root"/build/sicnu_generate_samples \
      "$repo_root"/build-*/tools/sicnu_generate_samples \
      "$repo_root"/build-*/sicnu_generate_samples; do
    if [ -x "$candidate" ]; then
      bin=$candidate
      break
    fi
  done
fi
if [ -z "$bin" ]; then
  echo "gen_samples: sicnu_generate_samples not found." >&2
  echo "  build it first:  cmake -S . -B build && cmake --build build --target sicnu_generate_samples" >&2
  echo "  or point SICNU_GENERATE_SAMPLES at an existing binary." >&2
  exit 1
fi

# Verify the directory that was actually written: the user's --out when given,
# otherwise the default data/samples. Skip the forced verify pass for --help
# / -h and when the caller already passed --verify.
out_dir="$repo_root/data/samples"
skip_verify=0
for arg in "$@"; do
  case "$arg" in
    --out=*) out_dir=${arg#--out=} ;;
    --help|-h) skip_verify=1 ;;
    --verify) skip_verify=1 ;;
  esac
done

cd "$repo_root"
"$bin" "$@"
if [ "$skip_verify" -eq 0 ]; then
  "$bin" --verify "--out=$out_dir"
fi
