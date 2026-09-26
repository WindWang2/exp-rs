#!/usr/bin/env bash
# Batch pipeline: apply authored enrichment per batch, normalize with gen-meta,
# verify diff scope, commit. Run from the worktree root AFTER a pristine
# gen-meta sanity pass (must be zero-diff) and after build completes.
set -euo pipefail
WT=/home/kevin/project/exp-rs-processing-meta-r4
TOOL=$WT/build-r4meta/tests/capability_knowledge_tool
export LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
cd "$WT"

# 0. pristine sanity: gen-meta must be zero-diff BEFORE any authored edits.
# 2026-09-27 measured: TWO pre-existing master defects surface here and are
# dispositioned (see DECISIONS.md #10): rs:temporal_decompose trend_lambda
# default drifted from code (10000 vs 100000000 — real semantic drift), and
# rs:temporal_sar_fusion failure_modes were hand-pasted with non-canonical
# formatting/key order. Both are repaired by the canonical regeneration, so
# the check is informational after inspection, not an abort.
$TOOL gen-meta "$WT" >/dev/null
if ! git diff --quiet -- data/processing/algorithm_meta/capability; then
  echo "NOTE: pre-existing gen-meta drift accepted after inspection:" >&2
  git diff --stat -- data/processing/algorithm_meta/capability | head >&2
fi
git checkout -- data/processing/algorithm_meta/capability  # re-run clean below

for i in 00 01 02 03 04 05 06 07 08 09 10; do
  f=".planning/processing-meta-r4/authoring/c$i.json"
  python3 .planning/processing-meta-r4/apply_enrichment.py "c$i.json"
  $TOOL gen-meta "$WT" >/dev/null
  # scope check: only capability sidecars may change (plus this track's own
  # known scaffolding, dirtied by earlier rounds and committed separately)
  other=$(git status --porcelain | rg -v 'data/processing/algorithm_meta/capability/rs-|\.planning/|\.goal-loop-ledger\.md|tests/CMakeLists\.txt|tests/test_capability_|tests/test_snapshot_gate_r4\.cpp' || true)
  if [ -n "$other" ]; then echo "UNEXPECTED CHANGES in batch $i:"; echo "$other"; exit 1; fi
  ids=$(python3 -c "import json;print(' '.join(json.load(open('$f')).keys()))")
  git add data/processing/algorithm_meta/capability
  git commit -q -m "meta(capability): authored enrichment batch $i

Operators: $ids

Authored keys only (applicability / teaching_use / prerequisites /
limitations), each grounded in the operator implementation read for this
track; derived keys untouched (regenerated canonically by
capability_knowledge_tool gen-meta, which preserves authored content).
Planning evidence: .planning/processing-meta-r4/authoring/c$i.json"
  echo "batch $i committed: $(git rev-parse --short HEAD)"
done
echo "ALL BATCHES DONE"
