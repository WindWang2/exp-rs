#!/usr/bin/env bash
# Recovery script: run when GitHub becomes reachable again.
# Usage: bash .planning/zcode-r25-B/RECOVER_PUSH_PR.sh
set -euo pipefail
BRANCH="agent/r25-B-data-model-runtime-20261028T140313Z"
cd "$(dirname "$0")/../.."
echo "== fetch + re-verify master progress =="
git fetch origin --prune
echo "behind/ahead: $(git rev-list --left-right --count origin/master...HEAD)"
echo "== push =="
git push -u origin "$BRANCH"
echo "== create (or update) PR =="
if gh pr list --repo WindWang2/exp-rs --state open --json headRefName \
     | grep -q "$BRANCH"; then
  echo "PR already exists for this branch — updating body only"
  gh pr edit "$BRANCH" --repo WindWang2/exp-rs \
    --body-file .planning/zcode-r25-B/PR_BODY.md
else
  gh pr create --repo WindWang2/exp-rs --base master --head "$BRANCH" \
    --title "r25-B: 地理空间数据资产、EO 模型推理与实验可复现工程深化" \
    --body-file .planning/zcode-r25-B/PR_BODY.md
fi
echo "== post-push regression re-verification =="
export QT_QPA_PLATFORM=offscreen CMAKE_BUILD_PARALLEL_LEVEL=2
ctest --test-dir build-r25-B --output-on-failure --parallel 1 -R "r25_b" | tail -3
python3 scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs | tail -2
