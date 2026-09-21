# ADR 0166: Mission Runtime 13.0 — One Persisted Mission, One Tool Surface

Status: accepted · Branch `agent/glm53-mission-runtime-13` · Baseline `origin/master@79adfe78a16b`

## Context

Workbench 12.0 (#1121) delivered the mission **task space** as a value model:
`MissionStage`, a fail-closed `MissionTimeline` (append-only event log,
revision, run-authority binding, retry lineage, reference reconciliation),
one shared projection for GUI/MCP/Pi, and a sidecar store for the timeline.
It deliberately left two gaps, and both were structural rather than cosmetic:

1. **Two writable transports, no precedence rule.** The timeline lived in
   `<stem>.mission-timeline.json` next to the D18 MissionContext sidecar and
   the `sicnuMissionContext` project-XML block. Nothing embedded the timeline
   in the context, so a `.qgz` round-trip lost the task space, and two files
   could both claim to be the task space.
2. **The `mission:*` surface was an allow-list entry, not a tool.** The ids
   `mission:context` / `mission:timeline` / `mission:advance` were declared in
   the surface registry and allowed by `surface_registry.cpp`, but no tool
   object existed: an allowed id fell through the MCP dispatch chain to
   "Algorithm not registered". There was no desktop surface either — no dock,
   no commands, no project read/write hook for the task space.

The MissionRuntime 13.0 track closes both without rebuilding TaskCenter, the
workflow durable IR, or the D18 MissionContext value model.

## Decisions

1. **The MissionContext document is the single persistence authority.** The
   timeline embeds verbatim under `MissionContext::metadata["mission_timeline"]`
   as a self-describing sub-document (`kind = "mission_timeline"`,
   `schema_version`), so the existing D18 dual write — sidecar +
   `sicnuMissionContext` XML, both `QSaveFile`-atomic — becomes the one write
   path, and extraction reuses `MissionTimeline::fromJson`'s fail-closed
   decoder. `missionContentFingerprint` excludes `metadata`, so embedding does
   not perturb scientific identity.
2. **The 12.0 timeline sidecar is an import-only channel.** It is read only
   when the authority carries no embedded timeline (migration, audited via
   `metadata["mission_timeline_migrated_from"]`, idempotent on re-open) and is
   never written. A tampered legacy copy can never override a present
   authority document.
3. **A last-known-good snapshot guards the artifact.**
   `<stem>.mission.json.last-good` is snapshotted from the authority sidecar
   after every successful commit. A corrupt authority recovers from it; a
   corrupt authority with no recovery is refused (poisoned) and the poisoned
   state can never be published over the artifact.
4. **`mission:*` tools are real objects behind an interface.** The three
   SpatialTools read and mutate through `MissionToolHost`, whose authority is
   a `MissionAuthority` strategy implemented in the app layer
   (`MissionStoreAuthority`). The shared agent library therefore compiles only
   the QGIS-free value model (`mission_stage`, `mission_projection`,
   `mission_run_authority`) plus the tools — no app sources, no duplicate
   definitions across the executable and the DLL. The MCP dispatch branch, the
   catalog provider prefix, and the built-in registration are wired, and a
   source-text parity gate makes a deleted wiring fail the build.
5. **One mutation path for all three surfaces.** `applyMissionAction()`
   (load → state-machine mutation → single-authority commit) is shared by the
   `mission:advance` tool and the desktop `mission.task.retry` /
   `mission.task.resume` commands, so the GUI and the agent surface cannot
   diverge. Every mutation goes through `MissionTimeline`'s fail-closed state
   machine; a rejection persists nothing.
6. **No fake Running, ever.** `start` requires a bound run authority verified
   against the *existing* execution authorities (TaskCenter,
   WorkflowRunCoordinator, PipelineRunCoordinator) through
   `resolveMissionRunStatus`. On project open, `reconcileRunAuthority` maps
   tasks left `Running` by a crashed session onto their terminal execution
   state, or to `Stale` when the run cannot be resolved. Terminal
   transitions (`succeed`/`fail`/`cancel`) are caller assertions on the
   mission ledger — documented as such in the tool contract.
7. **The desktop surface is a thin client.** One dock
   (`MissionTimelinePanel`) over the existing paged model, three commands
   whose availability derives from the same `SelectionContextSnapshot` every
   workbench command uses, and project read/write plus layer delete/rename
   hooks that call the already-tested reconciliation APIs. Project save
   reloads the authority first, so an agent commit between saves is never
   reverted by a stale in-memory cache.

## Consequences

- One persisted truth: the sidecar (preferred) and the project XML always
  carry the same document, and the task space survives a `.qgz` round-trip.
- 12.0 projects migrate on first open; the legacy sidecar remains as inert
  residue (never deleted, never read again once the authority carries a
  timeline).
- A corrupt or future-version authority fails closed: the project still
  opens, the mission block refuses to persist, and the artifact survives for
  recovery.
- Gates (`test_mission_runtime_persistence`, `test_mission_tools`,
  `test_mission_runtime_parity`, `test_mission_runtime_scale`) compile the
  real sources; the parity gate also substitutes for the compiler on the
  shell hunks that cannot be built in the verification environment, by
  checking that every `ContextRules::` predicate the shell references is
  declared and defined.
- The full event log is embedded in the authority, so a very long mission
  makes a large project file; the tool projections are bounded (task rows and
  events by `max_items`, truncation declared), the artifact is not.
