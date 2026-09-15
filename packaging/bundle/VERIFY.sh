#!/bin/sh
# VERIFY.sh — in-bundle integrity check for Linux/macOS target machines
# (goal D7 layout, added by deployment-packaging-11/F19; the Windows twins
# are VERIFY.cmd / VERIFY.ps1).
#
# Delegates to the canonical verifier shipped inside this bundle at
# tools/verify_bundle_manifest.py — identical rules to the dev-host verifier,
# so a PASS here means the same thing as a PASS at build time.
# Declared dependency: python3 (same as the POSIX builder's verify path).
set -eu
bundle_root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
verifier="$bundle_root/tools/verify_bundle_manifest.py"
if [ ! -f "$verifier" ]; then
    echo "VERIFY.sh: shipped verifier missing: tools/verify_bundle_manifest.py" >&2
    exit 2
fi
exec python3 "$verifier" "$bundle_root"
