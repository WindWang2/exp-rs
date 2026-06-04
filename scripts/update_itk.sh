#!/usr/bin/env bash
# Update the vendored ITK source tree to a new version.
# Usage: ./scripts/update_itk.sh [tag]
# Default tag: v5.4.0
set -euo pipefail

TAG="${1:-v5.4.0}"
REMOTE="itk-upstream"

if ! git remote get-url "$REMOTE" &>/dev/null; then
    echo "Adding ITK remote..."
    git remote add -f "$REMOTE" https://github.com/InsightSoftwareConsortium/ITK.git
fi

echo "Fetching ITK $TAG..."
git fetch "$REMOTE" "$TAG"

echo "Merging ITK $TAG into itk_ref/..."
git subtree pull --prefix=itk_ref "$REMOTE" "$TAG" --squash

echo "ITK updated to $TAG."
