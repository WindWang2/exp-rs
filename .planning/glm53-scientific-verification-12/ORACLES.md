# ORACLES — glm53-scientific-verification-12

Objective, falsifiable completion conditions. "Done" is never a judgement
call. Each Oracle names an executable command and a pass criterion.

Inherited from the track brief, restated as testable claims:

## O-1 — contract-or-exemption closure over the live registry

**Claim:** every first-class operator/tool in the live registry has a
scientific contract **or** an explicit, reason-carrying exemption; the
uncovered set is **0**.

**Command:** `test_contract_census_11` + `test_verification12_registry_closure`
**Pass:** uncovered set ∅, and no id carries *both* a record and an exemption.
**12.0 addition:** the closure must be computed from a **live** registry
projection in the same process that enumerates the operators (no snapshot
substitution), and the failure message must print the exact missing ids.

## O-2 — independent numeric Oracle + metamorphic relation per core algorithm

**Claim:** every algorithm in the selected cross-domain core set has ≥1
independent numeric reference **and** ≥1 metamorphic relation, and the
relation is **sensitive** (a deliberately wrong implementation fails it).

**Command:** `test_verification_numeric_reference_11`,
`test_verification_metamorphic_11`, `test_verification12_<lane>`
**Pass:** for each algorithm: reference within tolerance **AND** relation
holds **AND** the sensitivity control (a mutated/wrong path) is rejected.

## O-3 — determinism & reproducibility with a declared equivalence class

**Claim:** same input + seed + manifest ⇒ same result, metadata, provenance
and fingerprint, **twice**; and the platform states *explicitly* whether the
guarantee is bit-identical or within a declared tolerance.

**Command:** `test_contract_determinism_11` + `test_verification12_repro`
**Pass:** two consecutive runs agree at the declared equivalence level, and
the declared level is itself machine-readable (not prose).

## O-4 — failure / cancel / atomicity: no false success, no undeclared partial

**Claim:** for every operator under test, exactly one of
- a typed refusal **and** zero artifacts, or
- a published artifact that is **readable, declared shape, non-degenerate**

Any third outcome is a failure. Cancellation surfaces as the typed
`Cancelled` error wherever the contract declares a granularity.

**Command:** `test_verification12_success_truth`, `test_verification_failure_11`
**Pass:** zero `[false-success]` outcomes; declaration↔behaviour agreement.

## O-5 — mutation / test potency

**Claim:** the new lanes actually **kill** plausible wrong implementations —
a green run must not be achievable by a vacuous test.

**Command:** `test_verification12_mutation`, `test_mutation_kill_11`
**Pass:** every injected mutant is caught; and each new lane has a
**red-direction control** proving it can fail.

## O-6 — contract snapshot ≡ live registry, unknown drift fails loudly

**Claim:** the generated contract/census snapshot matches the live registry;
unknown drift fails the test and prints a readable diff.

**Command:** `test_contract_census_11 --snapshot`, `contract_inventory`
**Pass:** byte-equal after regeneration; drift → FAIL with a diff and a
regeneration remedy in the message.

## O-7 — the platform documents tolerance, seed, fixtures, provenance, portability

**Claim:** a machine-checked documentation artifact exists stating tolerance,
seed policy, fixture provenance and cross-platform limits — and the docs
agree with the code (not prose that can rot).

**Command:** `test_verification12_docs_bound`
**Pass:** every declared tolerance/seed in the registry has a matching doc
row, and vice versa.

## O-8 — the platform itself is GREEN (added from R0.5 evidence)

**Claim:** the verification platform's own suites pass on the merge
candidate — twice, consecutively.

**Rationale:** measured at baseline the 11.0 platform is **red** (see
EVIDENCE.md). A verification platform that cannot stay green cannot certify
anything. This Oracle is therefore load-bearing.

**Command:** all `verification12` + inherited 11.0 lanes
**Pass:** two consecutive identical green runs.

---

## Oracle status board

| Oracle | Status | Evidence |
|---|---|---|
| O-1 registry closure | inherited-green (census) | census 768 ✓ @11.0; re-verify @12.0 |
| O-2 numeric + metamorphic | **RED at baseline** | numeric_reference ✗, metamorphic ✗ |
| O-3 determinism/repro | **RED at baseline** | determinism_11 ✗ (2 asserts) |
| O-4 failure/atomicity | **RED at baseline** | failure_11 ✗ (F4,F5) |
| O-5 mutation potency | inherited-green | mutation_kill 42 ✓ @11.0 |
| O-6 snapshot drift | **RED at baseline** | census snapshot stale ✗ |
| O-7 docs bound | not started | — |
| O-8 platform green | **RED at baseline** | 6 / 8 suites failing |
