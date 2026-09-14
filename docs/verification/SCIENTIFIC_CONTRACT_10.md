# Scientific Contract Registry — Verification Platform 10.0

> docs/verification/SCIENTIFIC_CONTRACT_10.md
> Track: `zcode/scientific-contract-verification-10` · planning:
> `.planning/scientific-contract-verification-10/`
> Predecessor: [CONTRACT_PLATFORM_9.md](CONTRACT_PLATFORM_9.md) — reused, not
> replaced.

## What this adds

Contract Platform 9 proves *mechanical* consistency: schema parameters vs
implementation reads, command references, diagnostics, capability floors.
Platform 10 extends the authority to the **scientific semantics** of the
first-party `rs:` operators — the facts a scientist (or an agent composing a
workflow) must know and that until now lived in no machine-readable place:

| Dimension | Authority since 10.0 |
|---|---|
| input/output numeric domain (DN, reflectance, radiance, temperature, sigma0/gamma0/beta0, dB, index, probability, mask, classes, features, count, …) | `src/contracts/scientific_contract.cpp` |
| scale/offset policy (identity / param_driven / product_metadata) | same |
| NoData semantics (propagate / read_metadata / fail_closed / internal_sentinel) | same |
| categorical encoding + enclosed class-id range | same |
| time alignment (single_scene / stack_dates / increasing_dates / matched_grid) | same |
| wavelength policy (band_roles / srf_or_center) | same |
| seed policy (none / deterministic_internal / seed_param) | same |
| cancellation granularity (operator/step/tile/row_block level) | same |
| atomic publication (no_partial_output / staged_rename / json_result_only) | same |
| provenance expectation + refusal codes + evidence anchor | same |

## Authority discipline (no third truth)

Dimensions that ALREADY had an authority were **not duplicated**:

- modality, band roles, crs, memory policy, io ports → capability sidecars
  (`data/processing/algorithm_meta/capability/`, ADR 0154);
- determinism grade → the live schema stamp (`stampDeterminismGrade`,
  ADR 0124) *and* the sidecar grade — `test_drift_projection_10` binds the
  two published truths to agreement (spelling-normalized: `bit-exact` ≡
  `bit_exact`), while contract-9 monotone-tracks stamp coverage;
- io parameters → live schema; `test_drift_projection_10` binds sidecar
  parameter rows to the live schema in BOTH directions.

The registry is one file (`scientific_contract.cpp`), family-templated with
per-operator overrides, and every record carries a machine-checked
`evidence` anchor (a test, a review finding, or an explicit family + schema
read declaration).

## Gates

| Gate (test) | Mechanism |
|---|---|
| completeness (`test_scientific_contract_10`) | live registry `rs:` ids == registry keys, computed with `initBuiltinRsOperators()`, never a snapshot |
| vocabulary + evidence validity | closed-vocabulary validation, red direction proven by mutating a record |
| canonical JSON round-trip | `exp.scientific_contract.v1`, parse-equals across all records |
| known answers | pinned records (qa_mask fail_closed, spectral_resample srf_or_center, kmeans deterministic_internal, sar_calibrate sigma0/product_metadata, infer staged_rename/tile_level) |
| refusal codes | every declared code exists in the live ErrorCode taxonomy |
| snapshot projection (`test_contract_platform_9`) | scientific contracts appear as graph nodes + `scientific_contract_for` edges in the byte-compared snapshot |

## Drift gates (new in 10.0)

| Gate | Closes |
|---|---|
| schema `determinismGrade` ≡ sidecar `capability.determinism.grade` | two-truth drift on the reproducibility fact |
| sidecar io.parameters ≡ live schema parameters (both directions) | agent knowledge lagging (or lying about) the operator surface |
| LabSpec `operator_id` resolve in the live registry | labs referencing phantom/renamed operators |

## Verification lanes (new in 10.0)

`test_science_verification_10` — all offline, deterministic, bounded:

| Lane | Claim made testable |
|---|---|
| NDVI band-scale invariance (metamorphic) | index semantics hold for ANY input, not a fixture |
| band_ratio replay | deterministic-grade operators publish byte-identical outputs |
| kmeans replay | the `deterministic_internal` seed policy is real |
| CRS refusal fuzz | malformed CRS through the warp seam is always a typed refusal; a publish implies a real file |
| qa_mask provenance | outputs carry their declared SICNU_* derivation metadata |

## Regenerating the contract snapshot

```sh
cmake --build build --target contract_inventory
./build/src/contracts/contract_inventory \
    --source-root . --out data/contracts/contract_graph.snap.json
```

The snapshot now pins the scientific contract layer too; a record change is
a conscious diff.
