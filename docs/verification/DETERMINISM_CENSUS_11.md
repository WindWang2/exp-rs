# Determinism Census & Contract Coverage — Verification Platform 11.0

> docs/verification/DETERMINISM_CENSUS_11.md
> Track: `zcode/scientific-contract-verification-11` · planning:
> `.planning/scientific-contract-verification-11/`
> Predecessor: [SCIENTIFIC_CONTRACT_10.md](SCIENTIFIC_CONTRACT_10.md) — reused,
> not replaced.

## What this adds

Platform 10.0 made the scientific-semantics registry complete for the `rs:`
operators and bound its two PUBLISHED determinism truths (schema stamp ≡
sidecar grade) while explicitly recording that ~80+ sidecar claims were
"unproven" (no code-side stamp; the un-overridden virtual default means
"unproven", not "tolerance" — see the 10.0 drift-gate rationale). Platform
11.0 attacks that debt with evidence instead of mass-edits:

| Deliverable | Authority / location |
|---|---|
| Determinism & contract-coverage census | `src/contracts/determinism_census.{h,cpp}` — a source-grounded PROJECTION of four existing authorities (schema stamp, class-level override scan, sidecar claim, runtime `determinism()`), never a second truth |
| Census snapshot (byte gate) | `data/contracts/determinism_census.snap.json` — regenerated only as a conscious diff via `contract_inventory --census-out` |
| Contract-or-exemption coverage | first-party prefixes `rs:`/`gdal:`/`io:`/`cartography:` may carry records (14 new `io:` + 5 new `cartography:` records in `scientific_contract.cpp`); `gdal:`/`otb:`/`opencv:` adapters carry reviewed exemptions in `data/contracts/contract_exemptions.json` (schema `exp.contract_exemptions.v1`) |
| Execution evidence | `tests/test_contract_determinism_11.cpp` — cross-family replay corpus; identical input twice → byte-identical product; every corpus claim must be triple-published (class scan ≡ schema stamp ≡ sidecar) so replay EVIDENCES the claim and a sidecar can no longer self-certify |
| Metamorphic oracle | `tests/test_verification_metamorphic_11.cpp` — six relations (scale, band-order, geolocation-independence, NoData monotonicity, identity-reprojection, mosaic order) with sensitivity controls |
| Independent numeric reference | `tests/test_verification_numeric_reference_11.cpp` — long-double textbook NDVI/SAVI + analytic translate/clip/threshold structure; expectations are never built from implementation kernels |
| Oracle discrimination | `tests/test_mutation_kill_11.cpp` — six formula mutants, three input-sensitivity mutants and a flipped-threshold mask must be CAUGHT |
| Failure / cancel / atomic lane | `tests/test_verification_failure_11.cpp` — corrupt/missing input, bad parameters, pre-set cancellation, read-only output → typed refusals with zero partial artifacts |
| Cross-surface welding | `tests/test_contract_cross_surface_11.cpp` — help↔registry, agent-knowledge↔registry anti-phantom gates; monotone help-coverage; committed contract-graph snapshot ↔ live surface |
| Capability-aware ladder | `scripts/verification_ladder.py` — Windows `.exe` resolution, host capability declarations, per-item `requires` → explicit `skipped(reason)`; the seven 11.0 suites joined L2 |

## Census row semantics

Each census row records, for one registered operator id:

- `schemaGrade` — the determinism literal recovered from the implementing
  class (`""` = the class does not override; the framework default applies);
- `runtimeGrade` — the effective `determinism()` (scanned literal, or the
  framework default `bit_exact`);
- `sidecarGrade` / `sidecarStochastic` — the capability sidecar claim;
- `hasScientificContract` / `seedPolicy` — contract-registry membership;
- `exempted` / `exemptionReason` — a reviewed exemption record;
- `source` — the registration site, implementing class, override literals
  and inheritance depth of the scan itself.

Grade spelling is bridged (`bit-exact` ≡ `bit_exact`), never rewritten.

## Gates (tests, all live-registry-bound)

| Gate | Mechanism |
|---|---|
| census completeness (`test_contract_census_11`) | every live registered id (all prefixes) appears in the census |
| contract-or-exemption | every live id has exactly one of {record, exemption} |
| override truth | scanned class literal ≡ live schema stamp; un-overridden classes never publish a non-default stamp |
| runtime truth | scanned `determinism()` literal ≡ live virtual dispatch |
| scanner honesty | the scan recovers a synthetic registration tree (temp dir), including inherited overrides |
| snapshot | committed census snapshot byte-matches a fresh generation |
| replay (`test_contract_determinism_11`) | corpus × {run twice → byte-identical}; triple-publication of every corpus claim |
| metamorphic / reference / kill / failure | see table above |
| cross-surface | help and agent knowledge resolve against the live registry; graph snapshot mirrors it |

## Regenerating the snapshots

```sh
contract_inventory --source-root . --out data/contracts/contract_graph.snap.json
contract_inventory --source-root . --census-out data/contracts/determinism_census.snap.json
```

Both are byte-compared by tests; a stale snapshot fails loudly with the
regeneration remedy. Review the diff as a conscious contract update.
