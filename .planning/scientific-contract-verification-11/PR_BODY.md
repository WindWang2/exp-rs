# PR_BODY — scientific-contract-verification-11

> P0 (out of scope, blocking, fixed minimally here): origin/master does not
> compile on MSVC — `src/workflow/pipeline_run_coordinator.cpp` uses
> `_O_WRONLY|_O_BINARY` in its Q_OS_WIN branch but includes `<fcntl.h>` only
> in the `!Q_OS_WIN` branch (introduced by c5d4aafe / PR #991), and
> `src/agent/data_platform_tools.cpp` (D19, PR #992) uses
> `sicnu::experiment::BenchmarkService` unqualified without a using/qualifier.
> Every test executable links `sicnu_workflow` transitively, so these two TUs
> blocked the ENTIRE local verification platform. Both fixes are one-liners;
> both files are also touched by open PR #1009 (trivially re-appliable on
> rebase). Evidence: `git diff origin/master` proves neither defect was
> introduced by this branch.

**Local evidence only; no online CI dependency.**

## Baseline & parallel-track dedupe

- Baseline: `origin/master` @ `a5b11b7f` (Phase 0 refresh; the prompt
  snapshot's `ebcafb4d` was stale — #991/#992 had merged).
- Open PRs at start: #1008 (radiometric-spectral, CONFLICTING) and #1009
  (execution-runtime, MERGEABLE). File-level ownership in
  `.planning/scientific-contract-verification-11/PARALLEL_OWNERSHIP.md`:
  no overlap with this track's business surface; shared append-only files
  (`tests/CMakeLists.txt`, `.gitignore`, CHANGELOG) only.
- Open issues #1001–#1007 deduped (BASELINE.md): all fail-open/silent-semantics
  defects in io/workflow/dataset/georef — OUT OF SCOPE for this track; the
  failure lane (F1–F5) is the mechanism-level defense for exactly that class.

## What this delivers (F09 · Scientific Contract & Verification Platform 11.0)

Mission: 全算子确定性/数值/NoData/取消/provenance 契约和高可信验证体系.

1. **Contract census 2.0** (`src/contracts/determinism_census.{h,cpp}`,
   snapshot `data/contracts/determinism_census.snap.json`, byte gate): a
   source-grounded PROJECTION of every registered operator's determinism facts
   — class override literal, direct schema stamp, capability-sidecar claim,
   runtime `determinism()` — plus contract coverage. No second truth: the
   census reads the existing authorities and the gates bind them together.
2. **Contract-or-exemption over the whole live registry** (Oracle-1):
   `scientific_contract.cpp` gains first-party prefixes (rs:/gdal:/io:/
   cartography:), 14 io: + 5 cartography: records, and — caught live by the
   new coverage gate — **17 registered rs: operators that had NO record**
   (model-task trio, InSAR chain, temporal-10.0 set, CN import, spectral
   tools); all declared from their own headers. `gdal:`/`otb:`/`opencv:`
   adapters carry reviewed exemptions
   (`data/contracts/contract_exemptions.json`), never shadowing a record.
   Vocabulary: `phase`, `displacement` added (conscious closed-vocabulary
   update).
3. **Determinism truth** (Oracle-2): cross-family replay corpus
   (test_contract_determinism_11) — identical input twice → byte-identical
   product; every corpus claim must be TRIPLE-published (class scan ≡ schema
   stamp ≡ sidecar) so a sidecar can no longer self-certify. First
   execution-evidenced stamps: rs:band_ratio, rs:threshold_raster, rs:mosaic.
4. **Real defect found & fixed**: the metamorphic M4 lane proved
   `rs:band_ratio` emits ratio 1.0 inside declared-NoData holes (contract says
   `propagate`). Fix: `bandRatioTile` masking overload (IHS #380 semantics
   applied to the ratio path), wired in `band_tools.cpp`.
5. **Metamorphic oracle** (6 relations with sensitivity controls) +
   **independent numeric references** (long-double textbook NDVI/SAVI,
   analytic translate/clip/threshold) + **mutation kill** (10 injected mutants
   all caught — Oracle-3) + **failure/cancel/atomic lane** (typed refusals
   with zero partial artifacts).
6. **Cross-surface welding**: help↔registry, agent-capability↔
   {live registry, agent tool implementation surface} anti-phantom gates;
   monotone help-coverage baseline; contract-graph snapshot ↔ live registry.
7. **Capability-aware ladder** (package G): Windows `.exe` resolution,
   host capability declarations with per-item `requires` → explicit
   `skipped(reason)`, L2 timeouts sized from measured wall time; the seven
   11.0 suites joined L2; READINESS gained the 11.0 capability rows.

## Architecture decisions

- D-1 baseline rescope (census = exemption mechanism + non-rs coverage, not
  more rs: rows) · D-2 evidence-driven stamp convergence (exemption table +
  gates instead of mass edits) · D-3 census is a projection, not an authority ·
- D-5 failure lane consumes only master-stable seams (no #1009 runtime APIs) ·
- D-9 the two P0 host-portability fixes despite another PR owning the files.
  Full list with alternatives: `.planning/.../DECISIONS.md`.

## Compatibility

- `validateScientificContract` widens the accepted id prefixes (rs:+io:+gdal:+
  cartography:) — additive; all existing rs: records unchanged.
- `stampDeterminismGrade` behavior untouched; three operators now publish an
  explicit grade (schema-visible change, reviewable per ADR 0124).
- `bandRatioTile` keeps the unmasked overload; the masked overload is used by
  the band_ratio operator path only (NoData holes now NaN — scientifically
  correct, previously silently 1.0).
- Ladder/READINESS schema `exp.verification.ladder.v1` additive (host block,
  requires clauses).

## Local tests & evidence (all RUN_EXIT:0)

scientific_contract_10 **1480** · census_11 **768** · science_verification_10
**1187** · metamorphic_11 **1122** · cross_surface_11 **361** ·
numeric_reference_11 **234** · determinism_11 **138** · mutation_kill_11 **42** ·
failure_11 **32** · known_answer_corpus **105** — plus ladder
`L0 ok / L1 ok / L2 15 passed` (JSON in planning). Full command/exit log:
`.planning/scientific-contract-verification-11/EVIDENCE.md`.

## Known limitations (honest)

- 5 L2 non-passing items are **pre-existing master failures** (spectral
  unmixing sidecar drift, 3 duplicate capability entries, cartography.repair
  help gap, CATEGORICAL_MISMATCH page gap, matched_filter projection) — none
  of the owning files intersect this diff; plus `fuzz_ipc` timeout on Windows
  named-pipe emulation (host limitation).
- Temporal time-shift metamorphic relation not implemented (fixture cost) —
  recorded in CAPABILITY_MATRIX.
- Ladder lanes L3–L5/L7 targets not built on this host (reported `not-built`,
  never pass).
- Graph scanner cannot see lambda-registered/inline-schema operators (the 5
  cartography + 4 io-fabric nodes) — platform-9-owned surface, pinned to an
  exact missing set in the cross-surface gate; extension is a follow-up.

## Review & follow-ups

- Independent adversarial review (subagent #2): findings + dispositions in
  `.planning/scientific-contract-verification-11/REVIEW_LOG.md`; P0=P1=0 at
  merge-candidate state.
- Follow-ups: fix the 5 pre-existing master data/test failures (owner tracks);
  dedupe capability entries; extend OperatorParamScanner to lambda
  registrations; temporal time-shift metamorphic lane; band_ratio IHS-mode
  NoData audit.
