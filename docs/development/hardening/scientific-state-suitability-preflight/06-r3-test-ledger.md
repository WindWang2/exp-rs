# R3 Test Ledger — scientific-state-suitability-preflight

Base `9ea5a2fd17`. Every fix below is RED-first on the unmodified tree (the
RED observation is named per item; compile-time RED is marked).

## Closed in R3

| # | Fix | RED evidence | Oracle |
|---|-----|--------------|--------|
| PF-1 | Variant-parameterized capability policies that match no variant are flagged (`variantPoliciesDropped`) and the five rule-family "no X declared" pass gates degrade to typed unknowns | Runtime: `unmatched.variantPoliciesDropped` false; integration `unknowns == 0`, verdict `ok` | test_preflight_provider (mirror flag semantics + rs:spectral_index integration, `requires_ack`) |
| PF-2 | Passport with unresolvable `identity.kind` → typed Unknown slot, not an Available "unknown"-kind slot every raster rule silently skips | Runtime: `slotFacts.status == Unknown` failed (was Available) | test_preflight_provider (unit + integration) |
| PF-3 | Extends chain deeper than merge bound recorded as problem at load, healthy()=false, queries Unavailable (header's fail-closed promise restored) | Runtime: `REQUIRE_FALSE(healthy())` failed; boundary chain pinned healthy | test_preflight_provider (6-entry chain + 5-entry boundary) |
| PF-4 | Non-finite pixel sizes → `finitePositive` gate → `SPF_RESOLUTION_UNKNOWN`; non-finite cloud → `SPF_QUALITY_UNKNOWN` (new code) | Runtime: `SPF_RESOLUTION_UNKNOWN` missing (was require_ack MISMATCH on NaN) | test_preflight_rules |
| PF-5 | `budgets` serialized into the canonical request → request/report digests distinguish truncation configurations | Runtime: digests equal across differing maxFindings | test_preflight_engine |
| PF-6 | Subject-scoped acknowledgements (`acknowledgedSubjects`): code+subject clears exactly that subject; code-only form kept | Compile-time RED (API did not exist); behavior pinned: 1 quiet + 1 loud, blanket form still clears all | test_preflight_engine |
| PF-7 | Passport lifecycle != ready → typed Unknown (stale/missing/error passports not judged "observed"); `temporalRefsTruncated` projected honestly | Runtime-anchored: old adapter had no lifecycle gate and hardcoded false; new assertions fail on that tree | test_preflight_provider (stale integration + truncation projection) |
| P3 | `PreflightReport::fromJson` rejects verdicts contradicting findings (engine derivation mirrored); ADR-0174 citation rot fixed (vocabulary documented inline) | Runtime: lying report (verdict ok + Block finding) accepted before | test_preflight_report_schema |
| SU-1 | Goal time windows and DatasetFacts temporal stamps bound to UTC via shared `suitability_time.h::parseIsoUtc` (LocalTime **and** TimeZone specs — Qt6 parses zoneless into either; scene reader's old `== LocalTime` check never fired in those environments) | Runtime: `timeSpec() == Qt::UTC` got 3 (system-backed); epoch assertion flips verdicts on TZ≠UTC hosts | test_suitability_spatial (goal windows + round-trip), test_suitability_spectral (facts) |
| SU-2 | Grid pair sampling tail-inclusive (`(checked+1)*totalPairs/pairsChecked-1`): last pair always sampled; sampled pass → Marginal (labels posture); clean unsampled pass stays Suitable | Runtime: lone tail scene at n=1000 → `blocking_pair_count == 0`, Suitable; all-equal sampled → Suitable | test_suitability_adversarial (3 cases incl. n=21 mirror update) |
| SU-3 | Model pins modality + empty modality pool → Unknown (`modality_evidence_absent`), mirroring the GSD-unverifiable posture | Runtime: criterion was Suitable | test_suitability_labels |
| SU-4 | Facet truth spelling three-state + case-insensitive (`True`/`Y` count; unknown spellings → `factsTruncated`, never fabricated zeros) | Runtime: `pseudoLabelCount == 2` failed (was 0) | test_suitability_store_provider |
| SU-5 | `suitability:assess` refuses a missing `dataset_db` instead of letting DatasetStore::open create it (read-only channel; typo'd paths no longer leave SQLite droppings) | Runtime: `refused` false (store silently created + assessed) | test_suitability_agent_tools |
| SU-7 | DatasetFacts claiming a temporal extent with unparsable stamps fail typed; zoneless stamps bind to UTC | Runtime: `has_value()` false failed (invalid QDateTime survived) | test_suitability_spectral |
| SU-8 | Goal scalar fields of wrong JSON type fail typed (`boundedDouble`); no more silent "unset" via toDouble()=0 | Runtime: string "40" accepted as unlimited | test_suitability_spatial |

## Verification state

All suites green at close of implementation:
- preflight: provider 12, engine 17, rules 16, golden 12, render 8, report_schema 7 (test cases)
- suitability: spatial 20, temporal 13, core 11, adversarial 25, spectral 11, labels 25, profiles 10, uncertainty 7, teaching 5, store_provider 6, agent_tools 6

(Digest-value note: PF-5 adds `budgets` to the canonical request document, so
stored request_digest values differ from pre-R3 runs — the schema shape is
unchanged (`sicnu.preflight.report/1`); digests were never stable across
engine revisions by design.)

## Known limitations / recorded, not fixed

- Ack severity vocabulary (`SPF_CAPABILITY_MIRROR_UNAVAILABLE` /
  `SPF_OPERATOR_UNKNOWN` = require_ack, golden-pinned) is unchanged: whether
  a mirror outage may be acknowledged is a product decision, not a defect.
- Claim lattice (known/inferred/assumed/conflicted → finding `basis`) is not
  threaded through `SlotFacts`; resolver clears conflicted values and R3
  gates non-ready lifecycles, but per-field evidence grades stay
  rule-constant. Needs a `SlotFacts` schema addition — recorded for the
  adapter-wiring slice.
- Extends depth check runs per document at `addDocument` time; a chain whose
  parent arrives in a later document can only be caught by the merge-time
  bound. Forward references within the real mirror layout do not occur.
- Mirror declares policy keys no rule consumes (e.g. `crs.requires_projected`
  family defaults) — strategy-vocabulary drift detection remains unbuilt.
- Empty registry still evaluates to `ok` with no typed marker (P3, cosmetic).
- Suitability `(other)` fold-bucket literal collision (P3, needs product
  decision on reserved values); `isSameCrs` authority-string noise (P3,
  conservative direction); `verdict_counts` vs `blocking_pair_count`
  semantics (P3, documentation).
