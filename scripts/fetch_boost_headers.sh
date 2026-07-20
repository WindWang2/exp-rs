#!/usr/bin/env bash
# Fetch Arch Linux boost headers matching installed boost-libs (no sudo).
# OTB links against system boost-libs; headers must be the same major.minor.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/vendor/boost_sys"
VER="$(pacman -Q boost-libs 2>/dev/null | awk '{print $2}' | cut -d- -f1 || true)"

if [[ -z "${VER}" ]]; then
  echo "boost-libs not found. Install: sudo pacman -S boost-libs" >&2
  exit 1
fi

PKG_VER="${VER}-1"
URL="https://geo.mirror.pkgbuild.com/extra/os/x86_64/boost-${PKG_VER}-x86_64.pkg.tar.zst"

mkdir -p "${DEST}"
cd "${DEST}"

if [[ -f usr/include/boost/version.hpp ]]; then
  HDR_VER=$(grep '#define BOOST_LIB_VERSION' usr/include/boost/version.hpp | sed 's/.*"\(.*\)"/\1/' | tr '_' '.')
  echo "Headers already present (Boost ${HDR_VER})"
  exit 0
fi

echo "Downloading boost ${PKG_VER} headers..."
curl -fsSL --retry 3 -o boost.pkg.tar.zst "${URL}"
tar -xf boost.pkg.tar.zst
rm -f boost.pkg.tar.zst
echo "Installed headers to ${DEST}/usr/include"