# ADR 0172 — Unified Scientific Verifier (RS14-10)

- **Status:** Accepted
- **Date:** 2026-09-21
- **Track:** RS14-10-unified-verifier
- **Supersedes:** the per-track verification helpers scattered across the
  spectral / workflow / experiment tracks (they are not removed by this ADR,
  but new verification code should target this module)

## Context

By the time this track started, four separate subsystems had grown their own
notion of "did this result come out right?":

- the spectral tracks checked closed-form expectations with inline tolerance
  arithmetic;
- the workflow compiler validated structure, not results;
- the experiment layer recorded evidence but never judged it;
- the teaching lab wanted to explain a verdict to a student.

Every one of them had the same latent defect, and none of them could see it
because each expressed it differently: **the absence of evidence was
indistinguishable from success.** A check that could not obtain a band count, a
CRS, a metric or a digest simply did not fire, and "the checks that fired all
passed" reads exactly like "everything passed".

That is a fail-open, and in a scientific pipeline it is the worst class of bug:
the output is not merely wrong, it is *certified*.

Two concrete shapes of the defect appeared repeatedly during reconnaissance:

1. **Empty set collapses to success.** A spec built by filtering ("keep the
   checks that apply to this product") can end up with zero checks; a fold that
   seeds with `Pass` then reports success over nothing.
2. **Unknown input is skipped, not surfaced.** A check whose kind a given build
   cannot evaluate, or a fact key outside the known vocabulary, was silently
   dropped — which is indistinguishable from that expectation having held.

## Decision

Introduce `src/verification/` as the single place that turns declared
expectations into verdicts, built on three decisions that everything else
follows from.

### 1. Three-valued logic, with `Indeterminate` as a first-class verdict

```cpp
enum class CheckStatus { Pass, Fail, Indeterminate };
```

The lattice is **Fail-dominant**, and `Indeterminate` is *never* absorbed by
`Pass`: `combineStatus(Pass, Indeterminate) == Indeterminate`. The empty set is
`Indeterminate` — this is the rule that closes the fail-open, because "no
evidence" and "no problem" are now different values rather than the same one.

`Indeterminate` is not an error state and not a placeholder. It is the honest
answer to "I could not establish this", and it is what a report must say when a
provider answers `Missing` or `Refused`, when a budget is exhausted, or when a
check kind is unknown.

### 2. Evidence carries an `Availability`, not a bare value

Providers do not return `double` / `Json::Value` / `nullptr`. They return

```cpp
enum class Availability { Found, Missing, Refused };
```

so that "the artifact declares no CRS" and "I could not read the artifact" are
different answers that a caller cannot accidentally conflate. `Missing` and
`Refused` route to `Indeterminate` with a typed code; they never become an empty
observation. A `nullptr` provider member is mapped to `Missing`, so a caller who
forgets to wire a provider gets `Indeterminate` rather than a free pass.

### 3. Verdicts are content-addressed and clock-free

A report is reproducible byte-for-byte: no timestamps, no iteration-order
dependence, sorted members, floats pinned to 12 significant digits, explicit
depth and size caps. `specDigest` addresses the **set** of checks (sorted by
`id`, `kind`, `title`), so `pack A+B` and `pack B+A` are the same specification
— a cache keyed on the digest cannot be fooled by declaration order.

The single SHA-256 is the repository's existing `sicnu::geo::sha256Hex`
(`src/geospatial/util/sha256.cpp`), compiled into this library rather than
re-implemented, so the repo's one-hash rule stays true.

### Shape of the module

| Layer | Files | Responsibility |
| --- | --- | --- |
| Core semantics | `status_lattice`, `failure_codes`, `canonical_json`, `verification_types`, `spec`, `digest` | The lattice, the closed 18-code table, canonical bytes |
| Evidence | `availability`, `evidence`, `providers` | The three-valued provider seam and evidence records |
| Execution | `check_runner`, `report`, `availability`, `evidence` | Dispatch, budget enforcement, two-level roll-up; the three-valued seam (`Found`/`Missing`/`Refused`) and the provenance record attached to every conclusion |
| Families | `checks_state`, `checks_artifact`, `checks_numeric`, `checks_relational`, `checks_provenance`, `checks_reproducibility`, `checks_cross_output` | One file per family |
| Composition | `pack`, `packs_builtin` | Versioned bundles; projection from the contract registry |
| Presentation | `render_teaching`, `render_agent` | The same verdict in two vocabularies |

The module is **Qt-free, QGIS-free, I/O-free, clock-free and thread-free** by
construction, so it is usable from the CLI and testable headless. This is
enforced by the build: its test lanes link neither `Qt6::Core` nor `qgis_core`.

### The check kind is a wire string, not an enum

`VerificationCheck::kind` is a `std::string`, not a `CheckKind`. A future
producer can emit a kind this build has never heard of, and it must survive
deserialisation in order to become `VERIFY.UNSUPPORTED_CHECK_KIND`. Typing it as
an enum would fail at parse time and drop the check — which is the fail-open,
reintroduced at the earliest possible moment.

### The failure code table is closed

18 codes, each with a category, a `ReplanClass` and a hint. A verdict is
`Indeterminate` or `Fail` **for a named reason**; an unnamed non-Pass is a bug.
The table is closed so that adding a code is a deliberate act with a replan
consequence, not a string typed at a call site.

## Consequences

**Gained**

- A result cannot be certified without evidence. This is asserted by a dedicated
  adversarial suite (`test_verifier_adversarial_14`, 13 cases) whose only job is
  to try to produce a `Pass` without evidence and fail if it can.
- One vocabulary for verdicts across CLI, GUI, agent and teaching surfaces.
- `Indeterminate` is explainable to a human: the teaching renderer turns it into
  "This result cannot be judged yet" plus reasons, and the agent renderer turns
  it into a replan class rather than a retry.

**Costs / accepted trade-offs**

- **More `Indeterminate` than before.** Code that used to receive a boolean now
  receives a third value and must handle it. This is the point, but it is real
  work for callers and it will surface previously-silent gaps as visible ones.
- **A mirrored vocabulary.** `kFactKeys` lives in an anonymous namespace in
  `src/agent/harness/workflow_ir.cpp` and cannot be linked. The verifier mirrors
  its 30 keys and proves they agree with a drift guard that reads the harness
  source at test time (`test_verifier_drift_14`). The guard's own failure mode —
  a reworded anchor silently yielding an empty parse — is itself tested
  (mutation G-2).
- **`Sampled` evidence must carry its frame.** A sample without
  `details.sample_size` and `details.population` is refused rather than trusted.
  See ADR 0170 for the cluster-level version of this rule; this is the
  verifier-level consequence.
- **No silent truncation under budget.** When a budget is exhausted the
  unevaluated checks are `Indeterminate` and the overrun is visible in
  `budgetUsage`, because a truncated run and a complete run must not look alike.

## Alternatives considered

**A boolean plus a separate `reason` string.** Rejected: the fail-open is
precisely that `true` with an empty reason is indistinguishable from a real
pass, and nothing in the type system stops a caller from checking only the
boolean.

**Throw on missing evidence.** Rejected: a verification run should produce a
report even when it cannot conclude, because "we could not verify this" is itself
the finding a pipeline needs to act on. Exceptions would push the decision to an
unwind path where it is easy to swallow.

**Link `kFactKeys` instead of mirroring it.** Not possible without editing
`src/agent/harness/workflow_ir.cpp` to export it, which was out of scope for this
track. The drift guard is the honest substitute, and it is tested.

**Reuse `src/agent/cartography/quality.h`'s `structuralDigest` as the digest
precedent.** Rejected on inspection: that implementation uses
`QCryptographicHash`, so it is Qt-coupled and cannot serve as a Qt-free
precedent, and the repo already has a single SHA-256 in `geospatial/util`.

## References

- Plan and slices: `.planning/RS14-10-unified-verifier/{plan.md,slices.md}`
- Evidence (per-slice RED/GREEN and the full mutation table):
  `.planning/RS14-10-unified-verifier/progress.md`
- Teaching scenario: `docs/verification/TEACHING_SCENARIO.md`
- Integration notes: `docs/verification/INTEGRATION.md`
- Related: ADR 0137/0138 (experiment reproducibility), ADR 0149 (typed
  WorkflowIR and its closed fact vocabulary), ADR 0170 (cluster-level sampling
  frame)
