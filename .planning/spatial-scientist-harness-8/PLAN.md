> Reconstructed 2026-09 from ADR 0144 + commit evidence; the original planning files were never committed (gitignore whitelist omission). Do not treat as contemporaneous artifacts.

# PLAN — Pi Spatial Scientist Harness 8.0 (decisions → landed commits)

No milestone/ownership/architecture record survives; this maps the ADR 0144
decisions onto the commits that actually landed on
`feat/spatial-scientist-harness-8` (merged as PR #842, 2026-09-11). Scopes
are real `git show <sha> --stat` numbers, not plans.

| ADR 0144 decision | Commit | Landed scope |
|---|---|---|
| Typed spatial context 2.0 (D1) | `c1ee4cbb` | 8 files, +474/−7: typed product slots (`sensor`, `product_type`, `acquisition_time`, `radiometric_state`, ...), sparse `nodata`/`quality_masks`, per-asset `ContextLedger` with stale detection, bounded model contracts; `test_harness_grounding` +147 |
| Capability knowledge completion (D2) | `2dfc2443` | 9 files, +1217/−7: `surface` discriminator (operator / spatial_tool / data_platform_tool) in `capability_knowledge.*`, `data/agent/capabilities/tools.json` (+473), full-registry + platform-tool drift pins in `test_capability_drift` (+250) |
| Evidence sidecars + verification 8.0 (D3/D5, areas F/G) | `5e933ba4` | 7 files, +650/−4: `evidence.{h,cpp}` atomic (QSaveFile) writers for `provenance`/`uncertainty`/`verification` sidecars, `expected_band_count` check, post-verification verdict recompute (FAIL-never-success kept), `test_harness_evidence` (+316) |
| Plans 8.0 + explainability (D4/D7, area E/J) | `d6872a42` | 7 files, +557/−6: identity `pins` validated at execute with `IDENTITY_MISMATCH`, `cleanup`/step-`role` vocabularies, SHA-256 `plan_fingerprint` into bindings/run docs/workflow `metadata`/sidecars, read-only `harness:explain` (plan_tools +387) |
| Intent/feasibility 2.0, preflight assumptions, eval corpus (D6/D8/D9) | `dff5fd3e` | 15 files, +2511/−1: `resolve_intent` `missing_facts`/`preparations`/`solution_paths`, preflight `assumptions`, `MODALITY_MISMATCH`, `data/agent/evals/cases/*.json` + deterministic Tier-A runner `test_harness_eval_corpus` (+483), capability graph (+236) |
| Adversarial-review remediation | `42fe3cc0` | 17 files, +566/−90: two-subagent review of the full 8.0 diff; read-only explain hardened, write-once evidence, pin-gate hardening; all P0/P1 and reasonable P2/P3 findings fixed |

Docs trail: `ef9f1c45` added ADR 0144, the `docs/agent` 8.0 sections
(evidence sidecars, assumptions, resolve_intent 2.0, plan additions,
knowledge tool surface, eval corpus) and the CHANGELOG entry — 7 files,
+277. This commit is also where these planning files were meant to land;
`.gitignore` silently skipped them (see README.md). `7cf9548a` fixed an
Int64 literal in the quota test before merge.

## Explicit non-goals (ADR 0144, unchanged by the landed work)

- No second agent loop, scheduler, memory store, or model runtime.
- No uncertainty fabrication: operators own algorithm-specific facts.
- Transient-failure engine scenarios and dataset/experiment store
  interactions stayed in their existing suites; the corpus covers only the
  tool-contract plane.
- Cleanup/pins forwarding into workflow `metadata` relies on the engine
  parser tolerating unknown keys; stricter consumption was left to
  execution-plane-8.

Sequencing followed dependency shape: context → knowledge → evidence →
plans → intent/corpus → review remediation → docs.
