# Recon — RS14-17-reproducibility-capsule

Date: 2026-09-21 · Baseline: `origin/master` @ `4f6632e1f6bb41f90729800d0c7bf569ff34edb3` (PR #1145 merged).
Dynamic dedup at recon time: **no open PRs**; open issues match the avoid-list in the track prompt (all #1146–#1187 families). None of them block this direction.

## 1. What already exists (the capsule MUST project these, never re-implement)

| Existing fact source | Location | Relevant surface |
|---|---|---|
| Canonical JSON + hashing doctrine | `src/data/execution_fingerprint.h:142` | `sicnu::data::canonicalizeJsonRfc8785(QJsonObject) -> QByteArray` (sorted keys, shortest round-trip numbers; self-consistent hashing, RFC 8785-compatible not byte-identical) |
| Three-hash identity doctrine (ADR 0137) | `src/experiment/experiment_types.h/.cpp` | `runConfigHash`, `runExecutionFingerprint(RunExecutionIdentity)`, `runResultFingerprint`; env deliberately NOT an identity pin |
| Run-scoped reproduction bundle (ADR 0138) | `src/experiment/reproduction_bundle.h/.cpp` | DIRECTORY bundle: manifest/dataset_refs/split/run_config/environment/software/model_refs/workflow/metrics/provenance/README/checksums; Reference vs Portable mode; `validateBundle` with `ReproductionHooks` (unwired hook ⇒ Unknown, never fake-Exact) |
| Bundle import as evidence | `src/experiment/reproduction_bundle_import.h/.cpp` | checksums-first integrity gate, schema gate, installs run as Created-status evidence, idempotent re-import |
| Replay readiness | `src/experiment/replay_readiness.h/.cpp` | `ReplayReadinessReport{level, checks, notes}` over `ReplayCheck{dependency, Ok/Missing/Mismatched/Unknown, detail}`; `ReproductionLevel::{Exact,BestEffort,Impossible}` (`src/dataset/dataset_types.h:256`); `missingDependencyDiagnostics()` |
| Evidence projection | `src/experiment/evidence.h/.cpp` | `EvidenceProjector::summarize` over 7 closed dimensions (identity/environment/artifacts/metrics/steps/timing/protocol); "PROJECTS recorded truth — never computes"; `kEvidenceSchemaVersion = 1` |
| Lineage graph | `src/experiment/lineage.h/.cpp` | `LineageGraph` with ancestors/descendants, dangling tombstones |
| Environment capture + secret denylist | `src/experiment/experiment_types.cpp:196-330` | `RunEnvironment::captureCurrent` closed field allowlist; `filterSecrets`/`redacted`/`redactSecretKeys`; re-applied at the export boundary |
| Repeat-execution classifier | `src/experiment/repeat_execution.h/.cpp` | New/SameExecution/SameIdentity/EquivalentRerun/Deviated verdicts; execution fingerprints |
| Capability knowledge (3 layers) | `src/agent/harness/capability_catalog.h`, `data/processing/algorithm_meta/capability/rs-<slug>.json` (~115 sidecars, `schema_version` 2, **no per-capability version field**), `data/agent/capabilities/*.json` (preflight families) | `CapabilityCatalog::entryIds()`, `validateEntry`; lives in `src/agent` ⇒ **experiment must not depend on it** (dependency direction: agent → experiment) |
| Agent tool surface | `src/agent/tool_catalog/agent_tool.h`, `tool_provider.h`, `agent_tool_catalog.h` | `AgentTool` descriptor + `ToolProvider` base + `AgentToolCatalog::instance()`; table-driven meta protocol tools are the "one table is the schema authority" pattern |
| CLI surface | `src/cli/cli_commands.cpp:2612-2679`, `src/cli/cli_dataset_commands.cpp` | static allowlist `isCliCommand()` + if-chain `dispatchCliCommand()`; `experiment`/`reproduce` subcommands already export/validate bundles; `CliIO::finish()` JSON envelope |
| Lab↔experiment join | `src/experiment/bridge/lab_report.h:109` | **`labId == experimentId`**; `LabSpecCatalog` (`src/agent/harness/lab_spec.h`) for teaching specs; `LabRunRecorder` makes every lab execution a first-class experiment run |
| Verifier concepts | `src/agent/output_verifier.h` (`OutputVerification{ok,kind,summary,issues,warnings}`, `LabDeduction`), `src/agent/harness/harness_verification.h` (tri-state Verdict) | all in `src/agent` ⇒ capsule in `src/experiment` must accept verifier summaries as *inputs* via a provider hook, not compute them |

## 2. Gaps this track fills (the actual delta)

1. **Single-document capsule** (`sicnu.capsule.v1`): the existing bundle is a run-scoped *directory*; there is no canonical, digest-addressable *single document* that a student can submit as an attachment or an agent can emit/consume. The capsule is a **projection** of recorded truth (run + dataset + evidence + lineage + hooks), versioned, with a self-describing digest over the canonical form.
2. **Capsule diff**: no structural, section-typed, machine-readable diff of two reproduction documents exists. Identity-section deltas are *semantic breaks*; environment deltas are *reported, not punished* (ADR 0137 doctrine).
3. **Path portability / privacy**: raw absolute paths flow into run artifacts (`run_bridge.cpp:421-431` → `run_recorder.cpp:159` store verbatim) and are consumed raw by the bundle validator (`reproduction_bundle.cpp:470`). **No path normalization exists in src/experiment** — absolute machine paths are not yet barred from being identity. The capsule needs a portable path scheme (`workspace:`-relative tokens + digests) so the capsule digest is machine-independent.
4. **Offline capsule → replay readiness**: `ReplayReadiness::assess` works on a live run + DatasetStore. A capsule carried to another machine needs "what is missing *here* to replay" answered from the document + local hooks only.
5. **Teaching/agent attachment semantics**: validate-on-submit with typed refusal (unknown schema version, digest mismatch, non-canonical bytes, secret leakage, absolute-path identity) instead of silent best-effort.

## 3. What we will NOT do

- Not a second ExperimentStore / second provenance / second registry. The capsule stores nothing; it is a value object + projector + IO + diff/readiness over existing stores.
- Not modifying `ExperimentStore` schema or integrity behavior (avoids the #1161–#1173 issue family entirely).
- No payload bytes inside the capsule (no binary imagery; refs + digests only, mirroring bundle Reference mode).
- No new capability registry: capability references arrive via an injected provider hook (`CapsuleHooks::capabilityDescriptor`), so `src/experiment` keeps zero dependency on `src/agent`.
- Not touching: SAR (#1146/#1147/#1164/#1165), Mission Runtime (#1148/#1149/#1168–#1170), workflow cancellation (#1152/#1158), ImportCenter (#1153), jsoncpp depth (#1154/#1155), spectral NoData/perf (#1150/#1183), capability mirror red lights (#1151/#1187), plugin lifecycle (#1156/#1157/#1181), TaskCenter (#1159/#1182), VRAM ledger (#1160), dataset/experiment consistency (#1161–#1173/#1184), geospatial mirror (#1162/#1163), publish atomicity/Windows (#1174/#1175/#1178), WBF perf (#1176), observatory baselines (#1177), oracle potency (#1179), georef UAF (#1180), CLI async hang (#1185), P3 batch (#1186). If encountered: record `blocked/observed`, no scope growth.

## 4. Extension seams chosen

- New code in **`src/experiment/capsule/`** (own `CMakeLists.txt`, static lib `sicnu_experiment_capsule`, alias `Sicnu::ExperimentCapsule`), depending only on `Sicnu::experiment` + `Sicnu::dataset` (PUBLIC Qt6::Core). Central CMake delta = 1 `add_subdirectory` in `src/experiment/CMakeLists.txt`.
- New test executable **`tests/test_experiment_capsule.cpp`** using the *light* link pattern (Catch2 + Sicnu::ExperimentCapsule + Sicnu::experiment + Sicnu::dataset + SQLite) — NOT the heavy default `sicnu_add_test` chain (qgis_core/agent/operators), keeping the build cheap and the module GUI-free. One new block in `tests/CMakeLists.txt`.
- CLI: one new `capsule` command following the `isCliCommand`/`dispatchCliCommand` allowlist pattern (`src/cli/cli_capsule_commands.{h,cpp}`) with `--json` machine output.
- Agent wiring documented as a *future* seam in `docs/integration.md`; the machine-readable interface this track ships is the CLI JSON envelope + the versioned capsule schema itself (the agent's `OutputVerification`/harness can consume it without new agent-side code). GUI is not required for DoD and stays out.

## 5. Risks

| Risk | Mitigation |
|---|---|
| Capsule drifts into a parallel truth source | Builder refuses to *compute* anything: every section is projected from recorded store facts or explicitly-marked hook inputs; tests assert projected-equal-to-source for each section |
| Absolute paths leak into capsule digest | Portability policy normalizes at build time; dedicated cross-machine test relocates the workspace and asserts identical digest |
| Secret leakage through projections | Re-use `RunEnvironment::redacted()`/`redactSecretKeys` at the capsule boundary; validate step re-scans and *refuses* (typed error) instead of silent redaction drift |
| Build resource contention with parallel tracks | `-j1` default, light test target only, no clean rebuilds |
| jsoncpp depth bombs (#1154/#1155) | Capsule IO uses QJsonDocument (like the rest of src/experiment), never jsoncpp default readers |

## 6. Schema sketch (v1, detail in plan.md)

`sicnu.capsule.v1` — single JSON object: `schema` `{id, version}`, `capsule_id`, `created_utc`, `goal` `{lab_id/experiment_id, objective_digest}`, `software` `{revision, platform, qt_version, build_abi}`, `capabilities` `[{id, digest, source}]`, `inputs` `[{kind, id, digest, state, portable_ref}]`, `parameters` (canonical JSON), `plan` `{workflow_id, definition_digest}`, `environment` (redacted `RunEnvironment`), `outputs` `[{path_portable, digest, size_bytes}]`, `evidence` `{completeness dimensions, verifier summaries}`, `provenance` `{lineage_slice_digest}`, `digest` `{algorithm: sha256-canonical-json, value}`.
