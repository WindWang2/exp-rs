#!/bin/sh
# gen_samples.sh — generate the docs/labs sample data set (goal D1).
#
# usage:
#   scripts/gen_samples.sh [--profile=lab|stress] [--seed=<n>] [--spec=<path>] ...
#
# The script locates the sicnu_generate_samples binary in a build tree next to
# this repo (build/, build-*/, or $SICNU_GENERATE_SAMPLES), runs it against
# data/samples by default, and then verifies the manifest. Pass --out=<dir> to
# write elsewhere. Never blocks on CI; generation is fully local.
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

out_arg="--out=$repo_root/data/samples"
has_out=0
for arg in "$@"; do
  case "$arg" in
    --out=*) has_out=1 ;;
  esac
done

cd "$repo_root"
if [ "$has_out" -eq 0 ]; then
  "$bin" "$out_arg" "$@"
else
  "$bin" "$@"
fi
"$bin" --verify $out_arg
