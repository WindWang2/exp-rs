# Plugin Platform 9.0 — Issue & PR triage (re-verified on latest master, 2026-09-12)

## Open issues

`gh issue list --state open` → **0 open issues**. Nothing to re-verify.

## Recently closed issues — plugin-scope relevance check

The last 45 issues (#838–#882) were swept. None is plugin-platform scoped:
they cover terrain flow/hydrology (#848, #853), selection context (#849),
vector writer (#850), TaskCenter/data-manager concurrency (#851, #852, #860–
#862), workbench teardown/empty-state (#857–#859, #882), SAR/spectral
operators (#854–#856, #872, #873), help system (#869–#871, #881), cartography
solver/MapSpec (#865–#868, #877–#880), scan pool (#861), split engine (#875),
sentinel handling (#874), and the 6.x-era P2/P3 batch (#773–#817, closed by
`fix/resolve-open-issues-773-817`, PR #822).

Classification for the whole closed set relative to THIS track:
- `out-of-scope` — all of the above (different ownership directories).
- `fixed-by-later-merge` — n/a beyond the fixing commits already on master
  (f316dfdbb4, 8f6293bceb).

Searches performed (all on latest master):
- `gh issue list --search "plugin" --state all` hits are historical
  (5.0/6.0-era plugin issues, closed by PRs #822/#830 lineage).
- `grep -rn "TODO\|FIXME" src/plugins src/sdk/exprs` — reviewed; findings
  folded into the gap matrix (CAPABILITY_MATRIX.md), not issue-driven.

## Historical plugin branches (residue check)

| Branch | ahead/behind master | Verdict |
|---|---|---|
| feat/plugin-platform-8 | 0 / 32 | merged residue (PR #844) |
| feat/plugin-isolation-runtime-5 | 0 / 251 | merged residue (PR #830) |

No unmerged plugin work exists upstream; this track starts clean from
`f316dfdbb4`.

## Open PRs (overlap check)

| PR | Track | Plugin-file overlap |
|---|---|---|
| #883 scientific-algorithms-9 | src/processing, src/operators | none |
| #884 model-runtime-multimodal-9 | src/model | none (registry seam only) |
| #885 spatial-scientist-harness-9 | src/agent, pi | none |
| #886 scientific-mlops-9 | src/dataset, src/experiment | none |

Milestone → issue mapping: empty by evidence (no open issues). The 8.0
review-log dispositions (`plugin-platform-8/REVIEW_LOG.md`) were re-checked;
their follow-ups are carried as gaps G0.1 (A-P2-3) etc. in
CAPABILITY_MATRIX.md rather than as GitHub issues.
