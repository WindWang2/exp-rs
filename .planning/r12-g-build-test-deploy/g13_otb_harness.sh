#!/usr/bin/env bash
# R12-G G13-2 harness: runs ONLY the OTB block of packaging/build-appimage.sh
# (old = HEAD, new = working tree) against a stub OTB source tree.
set -u
cd "/c/Users/wangj.KEVIN/projects/exp-rs-r12-g-build-test" || exit 1
base="${TEMP:-/tmp}/r12g-otb-$$"
mkdir -p "$base"

awk '/^# Bundle OTB binaries/,/^# Bundle GDAL tools/' packaging/build-appimage.sh \
  | head -n -1 > "$base/new.sh"
git show HEAD:packaging/build-appimage.sh \
  | awk '/^# Bundle OTB binaries/,/^# Bundle GDAL tools/' \
  | head -n -1 > "$base/old.sh"
echo "old block lines: $(wc -l < "$base/old.sh")   new block lines: $(wc -l < "$base/new.sh")"

empty="$base/empty"; mkdir -p "$empty/bin"
full="$base/full"; mkdir -p "$full/bin" "$full/lib" "$full/share/otb"
echo x > "$full/bin/otbcli_Edge"; echo x > "$full/bin/otbcli"
echo x > "$full/lib/libotb.so";   echo x > "$full/share/otb/app.txt"

n=0
run() { # $1=block $2=src $3=label
  n=$((n + 1))
  local appdir="$base/app$n"
  mkdir -p "$appdir"
  echo "--- $3 ---"
  ( export APPDIR="$appdir"; export OTB_INSTALL_DIR="$2"; bash "$1" )
  echo "exit=$?"
  echo "copied_files=$(find "$appdir" -type f 2>/dev/null | wc -l)"
  echo
}

run "$base/old.sh" "$empty" "RED  旧版(HEAD) + 空 OTB 源"
run "$base/new.sh" "$empty" "GREEN1 新版 + 空 OTB 源"
run "$base/new.sh" "$empty" "GREEN2 新版 + 空 OTB 源"
run "$base/new.sh" "$full"  "GREEN1 新版 + 有内容 OTB 源"
run "$base/new.sh" "$full"  "GREEN2 新版 + 有内容 OTB 源"
