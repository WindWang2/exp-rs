# DECISIONS — automatic decisions & trade-offs (autonomy=full)

## D1 — Verification strategy (environment constraint)

The full app build (`sicnu_geo_rs` compiles the vendored QGIS subset from `src/core`) is
out of proportion in this environment: no complete prebuilt tree exists to reuse, other
tracks' builds are partial, and the predecessor track #1121 hit the same wall. Decision:
the mission runtime's *logic* is deliberately QGIS-free (value model, persistence,
bridge, tool objects) and is verified by an out-of-tree Catch2 harness compiling the
**real repo sources** with Qt6::Core + Qt6::Xml + jsoncpp. Shell hunks
(`main_window_workbench.cpp`, `command_defs.cpp`, `mcp_server.cpp`) are pattern-matched
to existing code in the same files and verified by source-text gates where possible;
their inability to compile here is recorded as a known limitation, exactly as in #1121.
Alternative rejected: shipping unverified shell code — the prompt forbids it.

## D2 — Single persistence authority

**Decision: the MissionContext document is the single authority.** It already has the
sidecar + project-XML dual write with QSaveFile atomicity, a fail-closed reader and a
content fingerprint. The timeline embeds into `MissionContext::metadata["mission_timeline"]`
as a self-describing sub-document (its own `kind` = `mission_timeline`, `schema_version`,
`revision`, `last_event_seq`). The 12.0 `<stem>.mission-timeline.json` sidecar becomes a
**read-only legacy import channel**: on load, it is consulted *only* when the authority
has no embedded timeline; the runtime never writes it. Alternative rejected: making the
timeline sidecar the authority — it has no XML channel, so a `.qgz` round-trip would lose
the task space, and two writable transports is precisely the drift the prompt forbids.

## D3 — Tamper precedence

A present-but-tampered legacy sidecar can never override the authority (O2). If the
authority itself is corrupt, the loader falls back to the last-known-good snapshot
(`<sidecar>.last-good`, rotated on every successful save) before failing closed; the
recovered state is never silently discarded and a later save never overwrites a good
snapshot with corrupt-derived state. Rationale: "corrupt file must not overwrite the last
known usable state" requires a durable previous-good copy; QSaveFile only protects a
single write, not read-then-rewrite cycles.

## D4 — Migration & fail-closed versioning

`mission_timeline_bridge` validates the embedded sub-document with the same strictness as
`MissionTimeline::fromJson` (unknown kind/version → refuse, machine code). A legacy
sidecar adopted into the authority records `metadata["mission_timeline_migrated_from"]`
= `"legacy-sidecar-12.0"` and one audit event, so a second open is idempotent. Unknown
*future* versions (`"2.0"`) fail closed — no best-effort decoding, mirroring the repo's
existing `data_project_serializer.cpp:352-374` refusal pattern.

## D5 — Mission tools are QGIS-free objects with a pluggable authority

The three `mission:*` tools take their state from a `MissionToolGateway` interface:
`mission:context` / `mission:timeline` read; `mission:advance` mutates through the
`MissionTimeline` state machine only (never around it). The default gateway is backed by
the runtime store keyed by the current project file name (works headless under `--mcp`);
the desktop shell installs a gateway backed by its live values. Run status is resolved
through a `MissionRunStatusResolver` interface whose desktop implementation queries
TaskCenter / WorkflowRunCoordinator / PipelineRunCoordinator (existing authorities, no
second scheduler); the headless default resolver reports "unknown" and the tools refuse
to claim `Running` for an unresolvable run — fail closed rather than optimistic.

## D6 — No fake Running after crash/reopen

On runtime load, `reconcileRunAuthority` moves `Running` tasks whose authority is gone to
`Stale` (`stale_run_reference`) and maps terminal authority states to `Succeeded`/`Failed`.
This is the only place a status is derived from an external authority, and it happens at
load time, not at display time, so every surface reports the same truth.

## D7 — Shell scope

No UI redesign. The mission surface is one dock panel (`MissionTimelinePanel`) over the
existing paged model, three commands with availability rules, one additive
SelectionContext field, and project read/write + layer delete/rename hooks that call the
already-tested reconciliation APIs. Command availability derives from the same snapshot
every other workbench command uses.

## D8 — Scale assertions are structural

Per O10 and the repo's observatory precedent: counters (rows touched per incremental
apply, resolver calls per reconciliation, resets) and generous ceilings only — no
fragile millisecond thresholds.

## D9 — Known limitation carried from #1121 (still true)

The in-repo CMake targets for `sicnu_geo_rs` / `sicnu_agent` cannot be built in this
environment; the two new workbench sources and the new test targets are registered in
`src/app/CMakeLists.txt` and `tests/CMakeLists.txt` (append-only) so the real build
compiles exactly the verified files, and the out-of-tree harness proves their behaviour.

## Review dispositions (independent review, first pass BLOCK)

Review of `origin/master...HEAD` by an independent read-only agent. Findings
F1–F16; all P0/P1 fixed and re-verified, P2 fixed, P3 fixed or documented.

- **F1 (P0)** `command_defs.cpp` used `ContextRules::missionTaskRetryable` /
  `missionTaskResumable` without a header declaration → the desktop target
  would not compile. FIXED (declared in `selection_context.h`), and a new
  compile-substitute gate (`test_mission_runtime_parity.cpp`: "shell hunks
  reference only declared ContextRules predicates") parses the real sources
  so this failure class cannot recur silently; re-injection verified red.
- **F2 (P1)** `onProjectWrite` persisted the window's stale timeline cache
  over an agent commit made between saves. FIXED: the save path reloads the
  authority first and takes only the timeline; the live context stays owned
  by the window; a mission-id mismatch is logged, never merged.
- **F3 (P1)** the poison guard was bypassed because a failed load discarded
  the poisoned state. FIXED: the failed-load state is propagated to
  `m_missionRuntime`, and `onProjectWrite` refuses the mission block while
  `authorityCorrupt` is set (the project itself still saves).
- **F4 (P2)** `refreshMissionRuntime()` overwrote the live mission context.
  FIXED: a refresh takes the timeline from the authority and leaves the live
  context alone (adopting the disk document only on first load or a mission
  switch).
- **F5 (P2)** out-of-range integers escaped as an untyped jsoncpp exception.
  FIXED: bounded readers (`optionalBoundedInt`, `optionalCursor`) answer
  `INVALID_PARAMETER`; gates added.
- **F6 (P2)** `error_message` / `note` were unbounded and persisted verbatim.
  FIXED: clamped at the door (2000 / 500 chars) and gate-added.
- **F7 (P2)** the timeline projection returned the whole event log.
  FIXED: events are capped by `max_items` and truncation is declared
  (`events_total` / `events_truncated`).
- **F8 (P2)** the reconcile-persist failure in `refreshMissionRuntime` was
  silent. FIXED: reported, and the display stays on the last durably
  persisted state.
- **F9 (P2)** terminal transitions were unverified assertions. DOCUMENTED as
  the mission-ledger contract in the tool description, the surface registry
  (kept identical by the parity gate) and ADR 0166.
- **F10/F12/F13 (P3, pre-existing 12.0 behaviour)** `tasks()` returns by
  value; `applyEvents` scans all rows; `bindRunReference` bumps the revision
  without an event. NOT changed (shared 12.0 code, out of this track's
  surgical scope); documented as known limitations. The scale gate bounds
  only what it measures.
- **F11 (P3)** the panel copied the task vector per selection change. FIXED:
  it now uses the model's `projectionAt` / `rowOfTask`.
- **F14 (P3)** a short/tampered `last_event_seq` could mint duplicate seqs.
  FIXED: the cursor is backfilled from the log (`qMax`); gate added.
- **F15 (P3)** header documented pre-commit rotation while the code rotates
  after commit. FIXED (doc).
- **F16 (P3)** dead code in `readRuntime`; `sicnu_agent` needed `Qt6::Xml`
  (QDomDocument in the authority interface); `nameChanged` was watched only
  for layers added after setup; the panel signal needed a metatype;
  `layersRemoved` could reconcile a previous project's sidecar during
  teardown; the Pi category gate was satisfiable by a comment; the
  fingerprint gate was near-vacuous; the panel's incremental path was dead;
  the Windows duplicate-symbol hazard (mission sources in both the exe and
  the DLL). ALL FIXED: Qt6::Xml linked; the exe no longer compiles the three
  Qt-Core mission sources (they come from `sicnu_agent`); existing layers are
  watched at setup; `Q_DECLARE_METATYPE` at global scope; a projectRef guard;
  the Pi gate now requires the exact category literal; the fingerprint gate
  gained discrimination assertions; the panel's `applyEvents` is driven by
  `refreshMissionRuntime`; the `mutateRuntime` re-entrancy constraint is
  documented.

Known limitations carried into the PR body: shell hunks are not compiled in
this environment (mitigated by the compile-substitute gate); the project-XML
channel is written by `onProjectWrite` (the sidecar stays the authority
between saves); the 12.0 legacy sidecar is never deleted; mission tool calls
perform blocking file I/O on the calling thread; the persisted event log is
unbounded (projections are bounded).
