# Plan — RS14-17-reproducibility-capsule

## Problem statement

A remote-sensing experiment today can export a run-scoped reproduction *bundle directory* (ADR 0138), but there is no **single, canonical, versioned document** that:

- a student can attach to a lab report and a grader can *validate* (not just open),
- an agent can emit as the final artifact of a task and another agent/machine can consume,
- can be **diffed** against another capsule to answer "what changed between these two experiments?" with typed semantics,
- whose identity survives machine relocation (absolute paths are not identity),
- can be checked for **replay readiness on THIS machine** without the original store.

## User stories

**Undergraduate (teaching mode)**
- As a student finishing a lab, I export a capsule for my run; the export refuses (typed error) if my run record is incomplete, so I learn *what evidence is missing* rather than shipping a hollow artifact.
- As a grader, I import/validate a submitted capsule and get a typed verdict (schema / digest / canonical-form / secret-scan / path-portability) plus replay-readiness against my machine — I never silently accept a tampered or machine-bound capsule.
- As a student comparing two attempts, I diff the two capsules and see which differences are *identity breaks* (different inputs/parameters/capability) versus *environment notes* (different machine, same experiment).

**AI Agent (agent mode)**
- As an agent, I produce a capsule as part of final delivery; its JSON is stable and digest-addressable so my consumer can verify integrity without trusting me.
- As a consuming agent, I call a machine-readable command (`capsule validate|diff|readiness --json`) and get typed, schema-versioned results; every unsafe/unknown/unsupported case is a typed verdict, never a silent fallback.
- As an orchestrator, I check replay readiness of a capsule against the local machine's hooks and receive a missing-dependency diagnostic list I can act on.

## Architecture

New module `src/experiment/capsule/` (static lib `sicnu_experiment_capsule`, alias `Sicnu::ExperimentCapsule`; deps: PUBLIC `Qt6::Core`, `Sicnu::experiment`, `Sicnu::dataset`). Zero dependency on `src/agent`, GUI, network. Projection-not-computation doctrine inherited from `evidence.h`/`lab_report.h`.

```
CapsuleDocument (value)          capsule_document.h
  ├─ schema {id="sicnu.capsule", version=1}
  ├─ sections: goal/software/capabilities/inputs/parameters/plan/
  │            environment/outputs/evidence/provenance
  └─ digest {algorithm, value}                       ← over canonical form
CapsuleHooks (provider seam)     capsule_document.h
  ├─ capabilityDescriptor(algorithmId) -> QJsonObject   (digest pinned by builder)
  ├─ verifierSummary(runId) -> QJsonObject              (pre-computed summaries only)
  └─ lineageSlice(runId) -> QJsonObject                 (LineageGraph projection)
CapsuleBuilder                   capsule_builder.h/.cpp
  └─ build(runId, options, hooks) -> Result<CapsuleDocument>
       projects: ExperimentStore run + dataset pins + EvidenceProjector
       + hooks; normalizes paths via PortabilityPolicy; NEVER computes science
CapsulePortability               capsule_portability.h/.cpp
  ├─ PortabilityPolicy { workspaceRoot; mode }
  ├─ toPortableRef(absPath) -> "workspace:relative" | "external:<digest>"
  └─ normalizeForDigest(doc) — strip machine-only fields (deterministic order)
CapsuleIO                        capsule_io.h/.cpp
  ├─ exportCapsule(doc, path) -> Result<...>   (canonical bytes + envelope)
  ├─ loadCapsule(path) -> Result<CapsuleDocument>
  └─ validate(bytes|doc) -> CapsuleValidation  (schema gate, digest gate,
       canonical-form gate, secret scan, absolute-path scan — typed issues)
CapsuleDiff                      capsule_diff.h/.cpp
  └─ diff(a, b) -> CapsuleDiffReport {level, sectionDiffs[], identityBreaks[]}
CapsuleReadiness                 capsule_readiness.h/.cpp
  └─ assess(doc, hooks, datasetStore*) -> ReplayReadinessReport-shaped result
       (reuses ReplayCheck / ReproductionLevel; unwired hook ⇒ Unknown)
```

### Public API / data schema

Schema id `sicnu.capsule`, integer `version = 1` (reader refuses unknown versions with a typed issue; version constant `kCapsuleSchemaVersion`). Wire document (canonical JSON, UTF-8):

```json
{
  "schema": {"id": "sicnu.capsule", "version": 1},
  "capsule_id": "capsule-<runId>",
  "created_utc": "…",
  "goal": {"experiment_id": "…", "lab_id": "…|null", "objective_digest": "sha256…"},
  "software": {"revision": "…", "platform": "…", "qt_version": "…", "build_abi": "…"},
  "capabilities": [{"id": "rs:ndvi", "digest": "…", "source": "hook|record"}],
  "inputs": [{"kind": "dataset_version|asset", "id": "…", "digest": "…",
               "state": "…", "portable_ref": "workspace:data/…|external:<digest>"}],
  "parameters": { /* canonical run parameters */ },
  "plan": {"workflow_id": "…|null", "definition_digest": "…|null"},
  "environment": { /* RunEnvironment::redacted().toJson() */ },
  "outputs": [{"portable_ref": "workspace:…", "digest": "…", "size_bytes": 0}],
  "evidence": {"schema_version": 1, "dimensions": [/* EvidenceProjector */],
                "verifier": {/* hook summaries verbatim */}},
  "provenance": {"lineage_slice_digest": "sha256…"},
  "digest": {"algorithm": "sha256-canonical-json", "value": "hex…"}
}
```

- `capsuleDigest(doc)` = SHA-256 over `canonicalizeJsonRfc8785(doc without the digest section)` — same doctrine as `runExecutionFingerprint`.
- Diff semantics: sections `goal|software|capabilities|inputs|parameters|plan|outputs|provenance` changing ⇒ `IdentityBreak` (semantic); `environment|evidence.verifier|created_utc|capsule_id` changing ⇒ `Reported` (same experiment, different machine/moment — ADR 0137). Overall level: `Identical | EquivalentRerun | IdentityBreak`.
- Typed failures everywhere: `sicnu::Result<T>` + `Diagnostic`, machine-readable `code` fields (`capsule.schema-unknown`, `capsule.digest-mismatch`, `capsule.not-canonical`, `capsule.secret-detected`, `capsule.absolute-path`, `capsule.run-missing`, `capsule.incomplete-evidence`, …).

### Migration / compatibility

None required: additive module, no store schema change, no existing API touched. The capsule coexists with the bundle (bundle = full directory with checksums + optional payload; capsule = identity document). `docs/integration.md` records the future agent-tool wiring point (`AgentToolCatalog` provider consuming `CapsuleIO`) without implementing it.

### Observability

- Every builder/validation verdict is a typed JSON object (`toJson()` on all report structs), CLI prints via `CliIO::finish` (`--json`).
- Validate reports list per-gate evidence (schema ok, digest ok, canonical ok, secret-scan ok, path-scan ok) — pass or fail with detail, mirroring `ReplayCheck` style.

### Security / trust boundary

- Secrets: environment section only ever embedded from `RunEnvironment::redacted()`; `CapsuleIO::validate` re-runs a denylist scan over the *whole document* and refuses with `capsule.secret-detected` (fail closed) instead of silently redacting.
- Paths: builder normalizes absolute paths to `workspace:` refs (or `external:` + content digest); validator refuses absolute-path identity with `capsule.absolute-path`.
- Integrity: exported file carries the self digest; loader recomputes and refuses mismatch (`capsule.digest-mismatch`); canonical-form gate (`capsule.not-canonical`) prevents hand-crafted non-canonical lookalikes from shipping.

### Performance budget

- Capsule build: O(size of projected JSON) — no payload reads except optional output digesting, each capped (reuse `SICNU_CACHE_INPUT_DIGEST_MAX_MB`-style cap via options `maxDigestBytes`, default 64 MiB per file; over-cap ⇒ digest recorded as `external:too-large` with explicit state, never silent).
- Diff/validate/readiness: O(sections + entries), no I/O beyond the capsule file itself (readiness uses hooks only).
- Memory: single document; no accumulation. Tests are pure-CPU, <1s each.

### Test strategy

One light Catch2 executable `test_experiment_capsule` (no QGIS/agent link). Per-slice RED→GREEN. Coverage classes per behavior: happy path, invalid/unsafe input, boundary, serialization determinism, cross-machine portability, teaching/agent semantic consistency (same verdicts through CLI JSON as through library). Deterministic-replay tests: identical inputs ⇒ byte-identical canonical export; workspace relocation ⇒ identical digest.

### Work packages

Slices A–G in `slices.md`, each independently red→green→committed. Then docs (`docs/integration.md` + ADR), CLI wiring, review gate, regression, PR.

### Rollback / kill-switch

Purely additive: deleting the `capsule/` subdirectory + the one `add_subdirectory` line + the CLI command arm + the test block restores master exactly. No behavior of existing modules changes (verified by untouched-file audit before PR).

## Track-specific Definition of Done

1. Undergraduate value: end-to-end teaching scenario — build capsule for a lab run, validate a *tampered* submission (typed refusal), diff two attempts — exercised in tests and CLI JSON.
2. Agent value: `sicnu.capsule.v1` + CLI `--json` verdicts are machine-readable; schema documented; integration seam written down.
3. Single source of truth: every section projected from existing records; tests assert projected-equality with store facts.
4. Typed failures: no silent fallback for unknown schema/digest mismatch/secrets/absolute paths/incomplete evidence.
5. Offline: no network in module or tests.
6. Budgets: digest cap on outputs; document-level bounds asserted in tests.
7. Dynamic dedup re-run before PR (fetch + open PR/issues scan) recorded in `progress.md`.
