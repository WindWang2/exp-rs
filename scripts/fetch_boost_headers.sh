#!/usr/bin/env bash
# Fetch Arch Linux boost headers matching installed boost-libs (no sudo).
# OTB links against system boost-libs; headers must be the same major.minor.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="${ROOT}/vendor/boost_sys"
# pacman's sync databases (world-readable) carry the expected digest for every
# package in the enabled repos.
SYNC_DIR="/var/lib/pacman/sync"
# The full installed version (pkgrel included), not just upstream: the headers
# must match the binary ABI of the installed boost-libs exactly, and pkgrel is
# part of that.
FULL_VER="$(pacman -Q boost-libs 2>/dev/null | awk '{print $2}')"

if [[ -z "${FULL_VER}" ]]; then
  echo "boost-libs not found. Install: sudo pacman -S boost-libs" >&2
  exit 1
fi

mkdir -p "${DEST}"
cd "${DEST}"

if [[ -f usr/include/boost/version.hpp ]]; then
  HDR_VER=$(grep '#define BOOST_LIB_VERSION' usr/include/boost/version.hpp | sed 's/.*"\(.*\)"/\1/' | tr '_' '.')
  echo "Headers already present (Boost ${HDR_VER})"
  exit 0
fi

# PSB-6 (swarm review 2026-11-04): this used to download a binary package from
# a hand-written mirror URL and untar it straight into the include path with
# ZERO integrity verification — a compromised mirror, a truncated transfer or a
# stale URL all landed in vendor/boost_sys as trusted headers. The URL now
# comes from pacman's configured mirrors and the expected SHA-256 from the
# sync database pacman already trusts (world-readable, so still no sudo), so a
# mismatch refuses the download instead of unpacking it.
# (LC_ALL=C: pacman localizes its output; the awk filter needs canonical text.)
PKG_URL="$(LC_ALL=C pacman -Sp --noconfirm "boost=${FULL_VER}" 2>/dev/null | head -n1)"
EXPECTED_SHA="$(
  for db in "${SYNC_DIR}"/*.db; do
    [[ -r "${db}" ]] || continue
    tar --zstd -xOf "${db}" "boost-${FULL_VER}/desc" 2>/dev/null || continue
  done | awk '/^%SHA256SUM%$/ { if ((getline line) > 0) print line; exit }'
)"
if [[ -z "${PKG_URL}" || -z "${EXPECTED_SHA}" ]]; then
  echo "cannot resolve boost ${FULL_VER} URL/checksum from the pacman databases;" >&2
  echo "refresh them (sudo pacman -Sy) and retry" >&2
  exit 1
fi
if [[ ! "${PKG_URL}" =~ ^https?:// ]]; then
  echo "refusing non-HTTP(S) package source: ${PKG_URL}" >&2
  exit 1
fi

echo "Downloading boost ${FULL_VER} headers from ${PKG_URL}..."
curl -fsSL --retry 3 -o boost.pkg.tar.zst "${PKG_URL}"
ACTUAL_SHA="$(sha256sum boost.pkg.tar.zst | awk '{print $1}')"
if [[ "${ACTUAL_SHA}" != "${EXPECTED_SHA}" ]]; then
  echo "checksum mismatch for boost ${FULL_VER}: expected ${EXPECTED_SHA}, got ${ACTUAL_SHA}" >&2
  rm -f boost.pkg.tar.zst
  exit 1
fi
tar -xf boost.pkg.tar.zst
rm -f boost.pkg.tar.zst
echo "Installed headers to ${DEST}/usr/include"