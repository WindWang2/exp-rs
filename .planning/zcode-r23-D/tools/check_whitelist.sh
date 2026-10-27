#!/bin/sh
# check_whitelist.sh — R23-D push 前机器校验：实际 diff 必须全部落在允许前缀内；
# tests/CMakeLists.txt 只允许 append-only；冲突标记必须为 0。
# 用法: sh .planning/zcode-r23-D/tools/check_whitelist.sh <base_ref>
set -eu
BASE="${1:-origin/master}"
cd "$(git rev-parse --show-toplevel)"

echo "== files changed vs ${BASE} =="
git diff --name-only "${BASE}...HEAD" | tee /tmp/r23d-changed.txt
echo

FAIL=0
while IFS= read -r f; do
  [ -z "$f" ] && continue
  case "$f" in
    src/lab/*|src/lab_pack/*|src/grader/*|src/recipes/*|src/faultlab/*|src/verification/*|src/verify_adapters/*) ;;
    data/labs/*|data/faultlab/*) ;;
    tests/*) ;;
    .planning/zcode-r23-D/*) ;;
    docs/*) ;;
    .gitignore) ;;
    *)
      echo "VIOLATION: $f"
      FAIL=1
      ;;
  esac
done < /tmp/r23d-changed.txt

echo "== .gitignore append-only check =="
if git diff "${BASE}...HEAD" -- .gitignore | grep -E '^-' | grep -vE '^---' | grep -qE '.+'; then
  echo "VIOLATION: .gitignore diff contains removed/changed lines"
  git diff "${BASE}...HEAD" -- .gitignore | grep -E '^-' | grep -vE '^---' | head -5
  FAIL=1
else
  echo "OK: pure additions"
fi

echo "== tests/CMakeLists.txt append-only check =="
if git diff "${BASE}...HEAD" -- tests/CMakeLists.txt | grep -E '^-' | grep -vE '^---' | grep -qE '.+'; then
  # deletions of existing lines are not append-only — inspect whether they are
  # purely our own block being restaged (rare); default: violation
  echo "VIOLATION: tests/CMakeLists.txt diff contains removed/changed lines:"
  git diff "${BASE}...HEAD" -- tests/CMakeLists.txt | grep -E '^-' | grep -vE '^---' | head -5
  FAIL=1
else
  echo "OK: pure additions"
fi

echo "== conflict marker scan (production + cmake files) =="
MARKERS=$(git grep -n -E '^(<<<<<<< |>>>>>>> |=======)$' -- ':!*.md' ':!.planning' | wc -l | tr -d ' ')
if [ "$MARKERS" != "0" ]; then
  echo "VIOLATION: ${MARKERS} conflict marker lines:"
  git grep -n -E '^(<<<<<<< |>>>>>>> |=======)$' -- ':!*.md' ':!.planning' | head -5
  FAIL=1
else
  echo "OK: 0 markers"
fi

echo
[ "$FAIL" = "0" ] && echo "WHITELIST: PASS" || { echo "WHITELIST: FAIL"; exit 1; }
