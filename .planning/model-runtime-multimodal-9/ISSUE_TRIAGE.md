# ISSUE_TRIAGE — model-runtime-multimodal-9

Re-verified against `origin/master` @ `132da5e998` (2026-09-11).
Open issue range at baseline: #848–#882.

## Model-runtime domain

| Issue | Title | Verdict on origin/master | Owner | Milestone | Verification |
|---|---|---|---|---|---|
| #872 | rs:infer parameter schema drift (`device` parsed in run(), declared in outputs, MISSING from `schema()["properties"]`) | **still-valid** — verified: `git show origin/master:src/operators/rs/rs_inference_operator.cpp` has `device` parse at ~465 + output decl at 235, no `props["device"]` | THIS track | M0 | regression test asserting schema ⊇ parsed params (old code fails: schema missing `device`) |
| #871 | HelpContentStore double-load duplicate IDs | still-valid but help-domain (Track: unified-help) — NOT ours | other track | — | — |
| #870 | Diagnostic catalog error-code drift | drift-class; Track 10 governance. Runtime error-code projection is ours — audit `errorCodeForInferenceFailure` codes appear consistently (M0) | shared boundary | M0 audit | projection test |
| #869 | Shell command drift in help | help-domain, not ours | other track | — | — |
| #879 | atomic fusion operator schema drift | fusion operators are rs-domain but NOT model runtime; local WIP diff already touches `rs_fusion_aliases.cpp` (another track) | other track | — | — |
| #880 | io:inspect schema drift | io-domain | other track | — | — |

## Overlap discipline for #848–#882 WIP

The MAIN worktree's uncommitted diff fixes #848–#882 across workbench/
cartography/help/sar/terrain — including `rs_inference_operator.cpp`
(#872). When my branch edits the same file, expect a textual conflict at
final sync; resolution: keep BOTH (their fix and mine touch different
regions — schema() vs run(); mine is the schema property + a regression
test). See OVERLAP_MAP.md.

## Issue → ownership → milestone → verification map

- #872 → this track → M0 → `test_model_runtime_9` schema-truth case:
  for every parameter parsed by `run()`, schema properties must contain
  the key (mechanical, prevents the whole drift class for THIS operator).
- #870 (runtime side) → M0 audit → projection test already exists in
  model failure matrix; extend to any new 9.0 error kinds.
