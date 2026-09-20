# DECISIONS — flash-workflow-engine-12

Baseline: `adf8f98952422fe9c386c56d64d5fb6a4a6642f1` (origin/master at worktree creation).

## D1 — Kernel of record: the D17/IR2 stack

The repo carries two workflow stacks:

- **Engine 2.0** (`workflow_run_coordinator`, `workflow_checkpoint`, `workflow_run`, jsoncpp) —
  production TaskCenter bridge: run locks, crash recovery, GC. Touch only at its
  documented seams.
- **D17/IR2** (`workflow_ir_v2`, `pipeline_run_coordinator`, `plan_optimizer`,
  `workflow_dag_analyzer`, `contract_checker`, Qt/JSON) — typed ports, lineage
  signatures, frontier scheduler, checkpoint/resume. **This is the kernel the
  track evolves**: every Oracle (typed composition, lineage-stable resume,
  artifact contract, provenance, deterministic planning) maps onto it.

Rationale: Engine 2.0 is a singleton bridge into TaskCenter scheduling
(explicitly out of scope); IR2 is where versioned documents and signatures
already live. Engine 2.0's stronger checkpoint hygiene (bounded reads,
version checks, artifact identity) is used as the *model* for hardening D17,
not merged into it.

## D2 — Checkpoint envelope: strict `kind` + closed version set

`persistCheckpoint` writes `kind="d17_pipeline_checkpoint"`, `version`. The
reader now validates both, fail-closed:

- wrong/missing `kind` → reject (`d17.checkpoint_kind`);
- `version` not in `{1.0, 1.1}` → reject (`d17.checkpoint_version`);
- `1.0` is *accepted* but its nodes carry no artifact identity → every node
  degrades to recompute (safe, correct, just slower). `1.1` adds
  `artifactSize`, `artifactMtimeMs`, `artifactFingerprint` per node.

Two-tier failure policy: **structural corruption is fatal** (bad envelope,
unknown node ids, unknown state keys, missing statuses) while **artifact-level
anomalies are per-node cache misses** (tampered/moved/foreign file → that node
recomputes). A corrupt document must never wedge or silently reinterpret;
a suspicious file must never be *served*.

## D3 — Artifact identity: size + mtime + bounded digest, both directions

New `NodeStatusSnapshot` fields: `artifactSizeBytes`, `artifactLastModifiedMs`,
`artifactFingerprint` (`"sha256fl:<hex>"` = SHA-256 over
`[8B LE size][first ≤1MiB][last ≤1MiB]`).

- Why not whole-file SHA-256: raster products can be GBs; head+tail+size is
  O(2 MiB) deterministic and catches truncation/extension/head-or-tail
  rewrite. A mid-file-only rewrite of a >2 MiB artifact could evade the
  digest — mitigated by also pinning size+mtime and by containment; the
  `sha256fl` scheme tag leaves room for a future `sha256full` mode.
- **Write-time containment**: on node success, the canonical artifact path
  must live inside the canonical run directory (symlink-resolved), else the
  node fails `ir2.artifact_outside_run:` — an executor may not launder
  foreign paths into the run record.
- **Resume-time verification**: CacheHit requires recorded Succeeded +
  signature match + artifact exists + contained + size/mtime/fingerprint
  equal. Any mismatch → Pending → recompute. `1.0` checkpoints lack identity
  → all nodes recompute (no stale trust).
- Executor contract unchanged: the coordinator fingerprints the file itself;
  executor-reported metadata would weaken the guarantee.

## D4 — Strict state vocabulary

`stateFromKey` mapped unknown state strings to `Pending` — silent corruption.
It now returns `std::optional<ExecutionState>`; unknown keys reject the load
with the offending key named. New states require a checkpoint version bump —
the closed vocabulary is part of the format contract.

## D5 — Shared bounded read

`workflow_checkpoint.cpp`'s file-local `kMaxCheckpointBytes` (16 MiB) is
extracted to `workflow_limits.h` as `kMaxCheckpointDocumentBytes` and reused
by the D17 reader (which previously did an unbounded `readAll`).

## D6 — `resumedDef` redeclaration (master build break)

`resumeFromCheckpoint` declared `const WorkflowDocument resumedDef` at the
validation pass and re-declared it at the parent-count pass — a hard error on
conforming compilers (all 4 open workflow-touching PRs carry an independent
workaround). Fixed by reusing the outer variable. This is the canonical fix
the parallel PRs will rebase onto.

## D7 — WP1 versioning contract (next iteration)

`WorkflowDocument.version` stays a string; `fromJson` accepts the known set
`{"2.0","2.1"}`, migrates 2.0→2.1 in-memory (new fields default), and rejects
anything else with the offending version named — an older build refuses a
newer document *explicitly* instead of silently dropping its fields. New
first-class fields (subflow ref, origin path) land under 2.1; extension data
that must survive older readers stays inside `metadata`/`parameters`, which
round-trip verbatim already.

## D8 — WP2 composition sketch (next iterations)

`operatorId == "workflow:subflow"` marks a fragment instance;
`parameters.fragment` = embedded WorkflowDocument (hermetic) or a file
reference; `parameters.bindings` = template substitution map. Expansion
(`workflow_composer`) flattens fragments into the parent document with
namespaced ids `subflowNodeId/internalNodeId` — lineage stays deterministic
because expanded ids are a pure function of the authored graph. Origin
attribution rides in expanded nodes' `metadata.originNodeId` so failures and
provenance point back at the designer-level node. Typed-port compatibility
at fragment boundaries reuses `contract_checker`'s closed mismatch
vocabulary rather than a second ruleset.

## D9 — WP5 provenance sketch

Per-run `provenance_<runId>.json` in the run directory with the same
envelope discipline (`kind`, closed `version` set): nodes = run / nodeExec /
inputArtifact / outputArtifact / cacheHit / retryAttempt; edges = consumed /
produced / reusedFrom / retriedAs. Query API answers artifact→producer and
node→inputs without re-parsing checkpoints. Built on checkpoint data + run
record, emitted at finalize — no second truth.

## D10 — WP6 determinism

`computeLineageSignatures` is already order-insensitive in doc order and
port-sensitive. Add `computePlanSignature(def)` = digest over the sorted node
signature multiset + canonical edge set, so "same workflow, reordered
serialization" is provably identical and CSE cannot collide across port
renames (rename = different signature = different cache identity — correct).
