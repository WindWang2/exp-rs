# ADR 0163: Scientific Workflow Compiler & Grounding 11.0 — Fact Model 2.0, Bounded Probes, Execution Provenance

Status: accepted · Branch `zcode/scientific-workflow-compiler-11` ·
Baseline `origin/master@a5b11b7f` · Owner: agent/harness

## Context

Compiler 10.0 (ADR 0149) shipped the typed WorkflowIR, 17 static check
families, contract-driven repair/refusal, and the harness session checkpoint.
Three gaps remained (recorded in the 10.0 follow-ups and the F08 goal):

1. **Facts stopped at the single scene.** The understanding carried one
   `acquisition_time` string, raw `pixel_size`/`extent` keys, and a mask-role
   band list — no cadence/regularity, no unit-aware resolution, no product
   generation parse, no normalized model-task or resource facts. Every
   consumer re-derived (or skipped) temporal semantics.
2. **Grounding was all-or-nothing.** `spatial:understand` answers every fact
   at once; there was no bounded, scoped probe, no budgeted failure semantics,
   and no model-manifest probe. Callers who need two facts still ground the
   whole dataset; callers cannot cite *why* a fact is unknown.
3. **Compiler decisions evaporated at the execution boundary.** The
   workflow JSON's `metadata` root key carried `plan_fingerprint`/pins (the
   recorded "execution-plane consumption is a cross-track follow-up"), but
   the facts, check ledger, repairs and refusals behind a compile had no
   stable, auditable projection.

## Decision

1. **Fact model 2.0** (`src/agent/harness/workflow_facts.*`): pure,
   deterministic extractors over understanding documents, each result carrying
   per-key `fact_status`:
   - *Time*: `parseInstant` anchors on QDateTime (platform authority); naive
     timestamps are UTC by convention and stamped as an assumption. Cadence
     facts over a date list (scene times, folded collection dates) derive
     `regularity` (closed vocabulary: single/regular/near_regular/irregular),
     median-gap `cadence_days`, and a hand-computable label
     (`16d`/`monthly`/`annual`). Unparseable declared timestamps are LISTED in
     the wire form, never dropped. Collection descriptors
     (`temporal:create_collection` sidecars) are grounded as collection facts
     with their date list (planner wiring; the descriptors are documents, not
     GDAL datasets).
   - *Space*: unit-aware resolution — WKT `UNIT[...]` parsing (last UNIT wins
     for a PROJCS, first for a GEOGCS), a closed geographic-authid table for
     degree CRSs, and a closed resolution-class table (fine ≤10 m, medium
     ≤30 m, coarse >30 m). Geographic degrees stay `unknown_meters` — no
     silent latitude-dependent conversion. Both `pixel_size` wire shapes
     ({x,y} objects from the inspect tools, [x,y] arrays in declared
     documents) are read; extent is validated (min<max).
   - *Quality masks*: the canonical harness-side mask-role predicate
     (`isMaskRoleName`); a unit test pins it to the producer-side vocabulary
     in spatial_contracts via a real `datasetUnderstandingFromRasterInspect`
     round-trip.
   - *Products*: mechanical processing-level parse (`L2A`/`Level-1C`/`2A` →
     {generation_level, suffix}) with NO radiometric semantics — domain
     decisions stay in the analysis.
   - *Models*: free-form catalog task strings normalize into a closed family
     vocabulary (`change_detection` before `detection` in matching);
     readiness/compatibility/cost pass through with provenance.
   - *Resources*: one normalization for node estimates (declared),
     capability demand (derived), and expectations budget (declared);
     `over_budget` is derived ONLY when both sides are known.
2. **Bounded grounding probes** (`grounding_probes.*`, tools
   `harness:probe_facts` / `harness:probe_model`): a closed scope vocabulary
   (identity, grid, crs, extent, bands, temporal, nodata, quality_masks,
   radiometric, modality, product) validated BEFORE any I/O; resolution runs
   through the ONE entity resolver; the grounding call delegates to the ONE
   `spatial:understand` tool and reuses its (path, revision|stat) cache via
   the now-public `understandingCacheKeyFor` — the probe never opens datasets
   and never forks a second cache. Deadlines are DECLARED and honestly
   reported (`elapsed_ms`, `deadline_exceeded`): a synchronous tool cannot be
   interrupted, and the probe does not pretend otherwise (async cancellation
   stays a recorded follow-up). Model manifests probe the ledger contract
   plus stat-only presence/size of the artifact path.
3. **Analysis 2.0** (four check families, additive, same ledger):
   `temporal_calendar` (declared `expectations.temporal` contract vs observed
   cadence facts — a declared contract with NO observable dates is a skip,
   never a faked pass), `numeric_domain_chain` (closed pair table: DN vs
   reflectance/index, dB vs linear power = errors; TOA vs surface reflectance,
   index vs continuous = warnings), `band_identity` (same source wired into
   two ports of a node whose contract demands distinct roles), and
   `output_identity` (declared output kind vs producing port artifact kind;
   duplicate port declarations). Four codes joined the single error-taxonomy
   table: TEMPORAL_CALENDAR_CONFLICT, NUMERIC_DOMAIN_CHAIN,
   BAND_IDENTITY_MISMATCH, OUTPUT_IDENTITY_MISMATCH.
4. **Repair 2.0 — prepared decisions** (`workflow_repair.*`): the existing
   `planRepairs` pass stays the ONLY mutation path; a new pure
   `planPreparedDecisions` converts its outcome into ordered decision
   documents (risk class, closed cost rank, evidence rank, exact wiring
   params). `IrRepairRecord` now carries the inserted node's `params`, so an
   applied repair is replayable. Refusals are NEVER auto-applicable in the
   plan — auto-applicable means shape_preserving and fact-backed, structurally.
5. **Execution provenance projection** (`provenance_projection.*`): one
   canonical `metadata.compiler` block (schema-versioned, digest-stamped,
   bounded, with honest `truncated_keys`) built from (IR, analysis, repairs,
   refusals); attaching is additive over engine metadata keys and preserves a
   superseded block one level deep; `<output>.compile.json` sidecars are
   QSaveFile-atomic with typed failures. The planner's lower stage attaches
   the block to the lowered engine JSON — `src/workflow/**` is untouched (the
   engine parsers ignore unknown root keys, per ADR 0149).
6. **Decision traceback** (`workflow_explain.*`): `explainDecisionChain`
   answers "which fact/contract/decision produced this outcome" from the
   compiler's own records (issues → repairs → refusals → the run's typed
   error), ordered run-failure → check-failure → refusal → auto-repair, with
   closed zh-CN one-liners per error code, per-string clamps, an 8-cause cap
   and a serialized-bytes budget that is measured and reported.
7. **Corpus + bridge guards**: eval-corpus cases for the calendar conflict,
   the numeric-domain chain, and the probe surface (data only, runner-owned
   schema); a Pi node test pins the compiler tools to the shared McpBridge
   transport, anchors the schema constants cross-language, and behaviorally
   round-trips a max-bounds IR (skipped with an explicit canary verdict on
   hosts that cannot exec the fake MCP server — a pre-existing limitation
   shared with no_drift.test.mjs).

## Non-goals

- No async/interruptible grounding (the probe reports budgets honestly
  instead); recorded follow-up.
- No execution-plane code changes: consuming `metadata.compiler` inside the
  engine remains the execution track's decision.
- No new dependencies; time parsing rides QDateTime, digests ride
  QCryptographicHash.

## Consequences

- Same facts → same cadence/regularity/resolution/projection bytes
  (determinism is test-pinned).
- UNKNOWN stays UNKNOWN: every new fact key ships a `fact_status`, every new
  check has a skip path with a reason, and no auto-insertion path exists for
  science-changing repairs.
- The compiler's decisions survive the compile boundary: a run artifact can
  cite `metadata.compiler.digest`, and a failure can be traced back to the
  exact check/fact/decision chain in bounded zh-CN.
