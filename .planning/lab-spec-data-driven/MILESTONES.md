# MILESTONES — LabSpec Data-Driven Labs

| MS | Gate | Status |
|----|------|--------|
| M0 | Baseline audited; capability matrix committed to planning dir | done (2026-09-12) |
| M1 | `labspec.schema.json` + gitignore exception + `data/labs/` tracked | done — commit 8051d7a162; `git add data/labs/probe.lab.json` ⇒ `A`；`data/samples/x.tif` 仍匹配 `data/*` |
| M2 | Loader + widget load from JSON; factories gone; app builds | code done (9b7fcb45e2) — build in progress |
| M3 | ≥10 lab JSONs valid; all operator ids resolve; params schema-clean | 11 labs committed (7fc57ce967), jsonschema-validated; C++ registry/params guard awaiting test run |
| M4 | `gen_lab_docs.py` zero-diff regeneration of `docs/labs/` | done — 3980de6cf6; `--check` ok (12 docs) |
| M5 | `test_labspec` + behavioural widget tests green offscreen; no workbench/app regression | done — test_labspec 5 cases / 91 assertions ✓，test_guided_workflow_widget 5 / 60 ✓，ctest 汇总 10/10 ✓，workbench 4 套件 23 cases ✓（详见 EVIDENCE.md） |
| M6 | Adversarial review P0=P1=0 | done — round1: 1×P0+3×P1+8×P2 全部处置（REVIEW_LOG.md），round2 自审通过 |
| M7 | PR created (not merged), URL reported | done — https://github.com/WindWang2/exp-rs/pull/949（不合并，不等 checks；worktree 保留至 merge） |
