# Unified Scientific Verifier (RS14-10)

`src/verification/` turns *declared expectations* into *verdicts* — and, more
importantly, refuses to turn the absence of evidence into a success.

- Decision record: [`docs/adr/0172-unified-scientific-verifier.md`](../adr/0172-unified-scientific-verifier.md)
- Integration guide: [`INTEGRATION.md`](INTEGRATION.md)
- Teaching walkthrough: [`TEACHING_SCENARIO.md`](TEACHING_SCENARIO.md)

## The one idea

A verdict has **three** values, not two:

| Verdict | Meaning | What a caller should do |
| --- | --- | --- |
| `Pass` | Every declared expectation was met, and the evidence to say so was available | Proceed |
| `Fail` | A declared expectation was contradicted | Stop; the result is not trustworthy |
| `Indeterminate` | The expectations could not be concluded from the evidence available | Obtain evidence, or decide explicitly |

`Indeterminate` is the reason this module exists. Before it, "I could not read
the CRS" and "the CRS is correct" both looked like "no complaint", and a
pipeline that checked for complaints shipped the result.

**Indeterminate is never absorbed into Pass.** `combineStatus(Pass,
Indeterminate) == Indeterminate`, and the empty set is `Indeterminate` — not
`Pass`. That single rule is what makes the rest of the module safe.

## Layers

```
spec.h ──── VerificationSpec: what is expected
   │
   ├─ pack.h ── composePacks(): versioned bundles -> one spec
   │
   ▼
check_runner.h ── runSpec( spec, inputs ) ── dispatch + budget + roll-up
   │                    ▲
   │                    └── providers.h: StateProvider, ArtifactProvider,
   │                        MetricProvider, ProvenanceProvider, DigestProvider
   │                        each returning Availability{Found,Missing,Refused}
   ▼
report.h ── VerificationReport: per-check results, per-node outcomes,
   │        task outcome, budget usage, content-addressed digest
   │
   ├─ render_teaching.h ── "trustworthy / not_trustworthy / unverified"
   └─ render_agent.h ───── "pass / fail / indeterminate" + replan class
```

## The seven check families

| Family | File | Asks |
| --- | --- | --- |
| State invariant | `checks_state.cpp` | Does the pipeline state still satisfy what was declared? |
| Artifact shape | `checks_artifact.cpp` | Is the product what it claims to be (kind, CRS, grid)? |
| Numeric range | `checks_numeric.cpp` | Is a measured value inside its contracted interval? |
| Relational consistency | `checks_relational.cpp` | Do two facts that must agree actually agree? |
| Provenance completeness | `checks_provenance.cpp` | Is the declared provenance actually recorded? |
| Reproducibility digest | `checks_reproducibility.cpp` | Does a re-run produce the same bytes? |
| Cross-output consistency | `checks_cross_output.cpp` | Do two outputs that must agree, agree within a *declared* tolerance? |

## The failure code table

Closed, 18 entries, each with a category and a replan class. A non-`Pass`
verdict always names one:

| Code | Typical verdict |
| --- | --- |
| `VERIFY.NO_CHECKS` | Indeterminate — a spec with nothing to check |
| `VERIFY.UNSUPPORTED_CHECK_KIND` | Indeterminate — this build cannot evaluate that kind |
| `VERIFY.EVIDENCE_UNAVAILABLE` | Indeterminate — a provider answered Missing |
| `VERIFY.EVIDENCE_REFUSED` | Indeterminate — a provider refused |
| `VERIFY.BUDGET_EXCEEDED` | Indeterminate — the run could not finish |
| `VERIFY.SPEC_INVALID` | Indeterminate — the spec cannot be read |
| `VERIFY.STATE_TOKEN_MISSING` | Indeterminate — a state token was never supplied |
| `VERIFY.ARTIFACT_MISSING` | Indeterminate — the product was not found |
| `VERIFY.STATE_INVARIANT_VIOLATION` | Fail |
| `VERIFY.ARTIFACT_KIND_MISMATCH` | Fail |
| `VERIFY.ARTIFACT_GRID_MISMATCH` | Fail |
| `VERIFY.ARTIFACT_SCHEMA_MISMATCH` | Fail |
| `VERIFY.NUMERIC_OUT_OF_RANGE` | Fail |
| `VERIFY.NUMERIC_NOT_FINITE` | Fail |
| `VERIFY.RELATION_INCONSISTENT` | Fail |
| `VERIFY.PROVENANCE_INCOMPLETE` | Fail |
| `VERIFY.REPRODUCIBILITY_DIGEST_MISMATCH` | Fail |
| `VERIFY.CROSS_OUTPUT_INCONSISTENT` | Fail |

Note the shape of that table: **the Indeterminate codes are all "I could not
find out", and the Fail codes are all "I found out, and it is wrong."** The
distinction is the product.

## Building and running the tests

The module builds as `sicnu_verification`; the seven lanes link it (and, being
Qt-free, link neither Qt nor QGIS):

```
ctest -R "^test_verifier_(core|checks|provenance|packs|render|drift|adversarial)_14::"   # all seven lanes
ctest -R "^test_verifier_checks_14::"                                                    # just one
```

Every lane passes `TEST_PREFIX`, the repo's D15 convention, so each discovered
case is named `<target>::<case>`. Anchor the regex on `^` and the trailing `::`:
`ctest -R test_verifier_` also matches a test *binary* by prefix, so a future
track that adds a target sharing that prefix would be silently swept into the
gate — and a gate that selects more than it means to reports success for work it
never ran.

| Lane | Covers |
| --- | --- |
| `test_verifier_core_14` | Lattice, code table, canonical JSON, digests |
| `test_verifier_checks_14` | All seven families |
| `test_verifier_provenance_14` | Provenance / reproducibility / cross-output, roll-up |
| `test_verifier_packs_14` | Composition: conflicts, overrides, empty compositions |
| `test_verifier_render_14` | Teaching and agent renderers |
| `test_verifier_drift_14` | The mirrored `kFactKeys` vocabulary matches the harness |
| `test_verifier_adversarial_14` | The fail-open battery |

## What the adversarial suite is for

`test_verifier_adversarial_14` does not test that the verifier produces the right
verdict. It tests that the verifier **cannot produce `Pass` without evidence**.
Thirteen cases, each a different way to try:

- a spec with zero checks
- a check kind this build has never heard of
- providers answering `Refused` / `Missing`, or not wired at all
- `Sampled` evidence presented without its sample frame
- unavailable evidence that still declares an expectation
- a budget exhausted partway through
- an empty report rendered for a human
- a node that is `Indeterminate` under a summary that claims `Pass`
- every combination of the three-valued lattice

If a change ever lets a `Pass` escape one of these, that lane goes red. It is
the regression fence for this module's core promise.
