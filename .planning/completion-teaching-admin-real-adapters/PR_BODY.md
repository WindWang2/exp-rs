# completion(teaching-admin): teacher-side authoring/grading/bundle real adapters

## Baseline

- Branch from origin/master `e4904cd3c568e1e730396237ec7034dc9215b272` (recon seed re-verified
  2026-09-24; `git fetch --all --prune` before start; open PRs #1277–#1284 and open issues checked —
  **no open PR touches `src/teaching_admin/**`, `src/app/teaching_admin/**`, `tests/test_teaching_admin_core.cpp`
  or the bundle/batch scripts**, so no scope shrink or interface conflicts).
- Merged #1240–#1276 with touching paths reviewed: #1260 (created this module), #1259 (cockpit),
  #1257 (grader schema), #1272 (curriculum/labspec fail-closed gates), #1246 (wiring-drift oracle).

## Current-master gaps proven (RED / structural evidence)

1. **G1 fabricated grader**: `teaching_admin_dock.cpp` batch callable returned `pass / score=80 /
   "local dry-run grade"` for every openable submission — a second grader in the UI.
2. **G2 pack validation discarded repoRoot** (`Q_UNUSED`) — confinement lexical only, no digest or
   byte-pin verification, `withinBudget` trivially satisfiable.
3. **G3 operator allow-list was a hardcoded demo** (`rs:extract_bands,rs:resample,rs:ndvi` line edit);
   an *empty* allow-list silently skipped operator checks.
4. **G4 LabSpec versions not fail-closed**: any non-empty `schema` string (e.g. `sicnu.labspec.v99`)
   accepted; `grading_ref.intent_ref` / `data.spec_ref` existence never checked.
5. **G5 no student projection / answer-leak guard** for authored LabSpecs (only teacher projections).
6. **G6 bundle adapter POSIX-only** (hardcoded `bash`, `.cmd` twin unused) and no version-pin
   visibility on `manifest.json`.
7. **G7 foundry-drift check existed in the repo** (`gen_lab_packs.py --check`) but no adapter.

## Authority boundaries (no second truth)

- **Grading**: the only new code path invokes `sicnu_geo_rs_cli lab --grade` — the process-isolated
  shell of `OutputVerifier::gradeArtifact` (ADR 0150), the same invocation
  `scripts/run_classroom_batch.py` drives. Exit contract 0/1/2/3 + `sicnu.lab.grade/1` transcript
  parsed; exit↔verdict mismatches refused (`grader_exit_verdict_mismatch`); the adapter computes no
  scores and no digests (hex shape only). Unavailable rows (missing CLI / timeout / crash /
  unverifiable artifact) carry verdict `unavailable` + mandatory reason + score<0 — the vocabulary of
  `src/cli/lab_batch_runner.h`. In-process linking of `sicnu_agent` remains out (forbidden-link guard
  intact: no Widgets/qgis_gui/Network/app).
- **Operators**: ids + param schemas read from `data/processing/algorithm_meta/capability/rs-*.json`
  (the `RSOperatorRegistry` derivative). Empty registry ⇒ fail-closed (every operator reference
  flagged), never a demo allow-list.
- **LabSpec**: version contract mirrors `data/labs/labspec.schema.json` (const `sicnu.labspec.v1`)
  and `data/schemas/labspec.schema.json` + `lab_spec_loader` (spec_version 1|2|3); the strict loader
  stays canonical — teaching_admin remains a pre-save authoring lint plus ref-existence checks.
- **Packs**: validation mirrors `src/agent/lab_data_pack.{h,cpp}` tier semantics (provenance strings,
  committed-fixture sha256/byte pins hard, generated soft, streamed 1 MiB hashing) without linking
  `sicnu_agent`; consolidating the two parsers into a Qt-free leaf is explicitly future work.
- **Bundle**: `scripts/verify_bundle_manifest.py` remains the only digest authority; teaching_admin
  only cross-checks the `bundle_version` metadata it was asked to pin and surfaces manifest schema.

## What landed (commits, one attributable change each)

| Commit | Change |
|---|---|
| `35b9cb17b` | grader CLI adapter + additive row fields (`grader_digest`/`top_deduction`/`unavailable_reason`, report `unavailable` counter, CSV columns) + plain-C++ fake CLI (same transcript contract) |
| `fdcb28569` | dock batch grading rewired to the CLI callable; fabricated lambda deleted; offscreen widget smoke target |
| `d4c2634d5` | operator catalog from real capability sidecars; empty-registry fail-closed; dock registry label |
| `1fd078686` | LabSpec version fail-closed + typed `dangling_ref`/`unsafe_ref`/`offline_required` |
| `1fcd69793` | pack containment (lexical+canonical), digest/byte pins with tier strength |
| `8295b0321` | student projection (masked answers) + adversarial leak oracle |
| `c693122b4` | portable bundle builder twins, manifest inspection + version pin, foundry drift adapter + dock button |
| `8f3802d79` | adversarial review round 1 fixes (see below) |
| `755a175cd` | callable lab_id backfill + opt-in real-CLI grading lane |
| `1e726c5df` | re-review P3 lanes: crash regression, max-weight top deduction, UB-free version check |

## Verification (no full build — narrow targets only)

- Built targets (all `-j2`): `test_teaching_admin_core`, `test_teaching_fake_grader_cli`,
  `test_teaching_admin_dock_smoke` (+ `sicnu_teaching_admin`). **No full-app/QGIS build was made**;
  configure ran once (`dev-default` + local prefix path).
- Final evidence, run twice consecutively:
  - `test_teaching_admin_core`: **309 assertions / 28 cases — all passed (×2)**
  - `test_teaching_admin_dock_smoke` (offscreen, compiles the dock TU, no QGIS): **16 assertions /
    2 cases — all passed (×2)**
- Mutation/adversarial oracles:
  - broken-authority lab (exit 0 + fail transcript) must be refused by the adapter;
  - leak oracle kills 4 tampered student views (teacher field re-add, paraphrased claim excerpt,
    unmasked param at position, grading path in note) — the first oracle draft was itself killed by
    mutation (2) and strengthened to 4-gram window detection before landing.

## Independent adversarial review

Full-diff review (`origin/master...HEAD`) by an independent reviewer: P1×1 + P2×4 all fixed and
re-verified (declared-bytes sum regression, top-deduction max-weight parity, QProcess crash
semantics plumbed to unavailable/unverifiable, pack validator authority parity, fake-CLI CMake
dependency) plus P3s (CSV formula-injection twin, integral spec_version, mask-literal guard,
sorted JSON). Re-review verdict: **READY / PROCEED** (fix verification incl. static check that all 19 committed packs / 111 inputs satisfy the strengthened validator).

## Known limits

- The dock grades through the CLI subprocess (process-isolated twin of the in-process
  `OutputVerifier` seam) — chosen so the QtCore-only core library and the widget smoke stay hermetic;
  an opt-in real-CLI lane (`SICNU_GEO_RS_CLI` + `SICNU_SOURCE_DIR`, `[real-cli]` tag) covers the real
  binary shape where available.
- Version pin in `verifyOfflineBundle` is metadata cross-check; digests stay with the canonical script.
- teaching_admin keeps its own QtCore pack projection mirroring `sicnu.lab-pack/1` semantics;
  consolidating parsers needs a Qt-free leaf move of `lab_data_pack` (out of this track's
  minimal-delta rule).
- Plan-doc notes: `.planning/completion-teaching-admin-real-adapters/{oracle,ledger}.md`
  (markdown-only whitelist entry in `.gitignore` per repo convention).

## Merge order / rollback

- No dependency on open PRs #1277–#1284 (no path collisions verified); merge order free. Rollback =
  revert the 8 commits; all JSON/CSV changes are additive keys/columns.
