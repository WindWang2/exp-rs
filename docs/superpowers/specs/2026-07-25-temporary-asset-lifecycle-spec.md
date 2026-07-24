# Spec: Temporary Asset Lifecycle and Reaping

**Parent:** `docs/superpowers/specs/2026-07-24-data-manager-architecture-spec.md` — Persistence and Storage (lines 131-152), and the Phase 1 plan's Deferred Follow-Up Order item 2.
**Status:** Proposed.
**Supersedes / refines:** none (implements an already-specified but unimplemented behavior).

## Problem Statement

When an algorithm produces an output, the transactional committer (#39) registers it as a `SessionTemporary` Data Asset and publishes a real file on disk at a stable path. The architecture spec already says these assets "are not serialized and are cleaned when the project session closes" (SessionTemporary) and that `TaskTemporary` artifacts "are cleaned when their task scope ends and no lease remains."

None of that cleaning exists today. A temporary asset is registered, its file lives forever on disk, and it sits in the in-memory catalog until the process exits (the catalog is then dropped without unloading or deleting anything). Reopening a project re-resolves the persistent assets, but the previous session's temporary outputs have already leaked onto disk with no record of them. There is also no way to **promote** a useful temporary output into a persistent project asset — the spec promises promotion ("they can be ... promoted") but no promote capability exists.

The user-facing problem: scratch outputs from NDVI runs, classification trials, and intermediate pipeline stages accumulate on disk and in the Data Manager panel across a session, and a good result the user wants to keep has no path from "session scratch" to "saved with my project."

## Solution

Give the Data Manager a **host-driven reaping** capability and a **promote** capability, so that:

- On project session close, the host asks the Data Manager to reap every `SessionTemporary` asset: unload it from the catalog **and** delete its on-disk published file (when the asset declares it owns a deletable source). Nothing persistent is touched.
- When a task scope ends, the host reaps that task's `TaskTemporary` assets once they have no remaining lease.
- A user can promote a `SessionTemporary` (or `TaskTemporary`) asset to `ProjectPersistent` so it is serialized into the `.qgz` and survives the session — its file is kept, and its persistence policy flips.

Reaping is a **distinct operation from unload**, because the parent spec is explicit: "Unloading never deletes source data. Physical deletion is a separate capability-limited command." Reaping is that separate capability-limited command, scoped to temporary assets that the Data Manager itself published. It is never offered for assets the user imported from outside.

## User Stories

1. As an analyst, I want scratch NDVI / classification outputs to disappear from the Data Manager and from disk when I close the project, so that my workspace does not fill up with throwaway results.
2. As an analyst, I want intermediate pipeline artifacts to be cleaned up as soon as the task that produced them is done and nothing is looking at them, so that the catalog only holds things I might still use.
3. As an analyst, when a scratch output turns out to be the one I want, I want a single "promote to project" action that saves it into my `.qgz` so it survives the session, so that I do not have to re-run the algorithm.
4. As an analyst, I want temporary outputs to remain fully usable (displayable, leasable, processable) for the whole session, so that I can inspect a scratch result before deciding to promote or discard it.
5. As an analyst, I want reaping to never touch my imported rasters or my persistent project layers, so that closing a session cannot destroy data I brought in.
6. As an analyst, if I am still viewing or processing a temporary output when the session closes, I want a clear warning rather than a silent deletion that crashes my viewer, so that I am not surprised.
7. As a developer, I want one explicit, testable reap entry point on the Data Manager rather than implicit deletion hidden inside unload, so that the "unload never deletes" contract stays honest and auditable.
8. As a developer, I want reaping to be driven by the host (ProjectContext on close, the task host on task end) rather than auto-triggered on lease release, so that deletion timing is predictable and never surprises a caller mid-operation.
9. As an analyst, I want a promoted asset to keep its identity, revision, provenance, and display configuration, so that promoting does not look like a delete-plus-reopen.
10. As an analyst, I want the Data Manager panel to show which assets are temporary vs persistent, so that I can tell what will be cleaned on close and what I should promote.
11. As an analyst, I want reaping of an asset that a Display Layer still shows to remove or mark that layer too, so that the display does not point at a deleted file.
12. As an analyst, I want a failed reap (file locked, permission denied) to leave a clear diagnostic rather than partially corrupting the catalog, so that the catalog and disk stay consistent.

## Implementation Decisions

- **Reap is a new Data Manager operation, distinct from unload.** It combines an unload (catalog removal, lease revocation, signal emission) with a capability-limited physical deletion of the published source file. This preserves the parent spec's "unload never deletes source data" contract: deletion lives only behind the reap operation.
- **Reap is scoped to temporary assets.** Only assets whose `PersistencePolicy` is `SessionTemporary` or `TaskTemporary` may be reaped. Reaping a `ProjectPersistent` asset is rejected. This makes the "never touch imported data" story structural, not advisory.
- **Deletability is gated by the `DeletableSource` capability.** A temporary asset whose source the Data Manager published (via the OutputCommitter) declares `DeletableSource`. Reap deletes the on-disk file only when `DeletableSource` is set; otherwise it unloads from the catalog but leaves the file (mirroring the "ENVI/Shapefile as resource sets, complex products unload-only" rule). The OutputCommitter stamps `DeletableSource` on the assets it registers, since it owns the published stable path.
- **Host-driven triggers, not auto-GC.**
  - `SessionTemporary` reaping is triggered by the host on session close. `ProjectContext` gains a `reapSessionTemporaries()` step invoked from its close/clear path, which calls the Data Manager reap for every `SessionTemporary` asset.
  - `TaskTemporary` reaping is triggered by the task host when a task scope ends. The host calls a Data Manager reap scoped to a task's assets, but only once those assets have no remaining lease (the lease is the "still in use" signal).
  - Reaping is **not** auto-triggered on lease release. The plan's "do not widen the interface / no surprising auto-behavior" rule rules out a last-lease-released signal driving deletion; the host decides timing.
- **Lease safety.** Reap refuses an asset that still holds an active lease, returning a clear diagnostic, rather than revoking leases out from under a viewer/processor. The host reaps only idle temporaries. (This is the contract that makes host-driven reaping safe.)
- **Promote is a new Data Manager operation.** `promote(AssetId)` flips a temporary asset's `PersistencePolicy` to `ProjectPersistent`. It keeps the AssetId, revision, source, structure, capabilities, and provenance unchanged — only the policy mutates, and one `assetChanged` is emitted so the panel refreshes. A promoted asset is then serialized by the existing project serializer (which already writes `ProjectPersistent`). Promoting a persistent asset is a no-op success.
- **Task-to-asset binding for TaskTemporary.** To reap a task's artifacts, the Data Manager must know which assets belong to a task. Rather than a new bidirectional link, the reaping of `TaskTemporary` is driven by a `persistence == TaskTemporary` query at task-scope end: when a task ends, the host reaps all currently-`TaskTemporary` assets that have no lease. This avoids a task-id column on every asset while still scoping cleanup. (If a future caller needs true per-task ownership, that is a later wave — do not add it now.)
- **Display coordination.** Reap emits `assetAboutToUnload` (the existing signal) before deletion, so the Display Manager's existing listener (`relocateLayer`/removal path) removes or marks the Display Layer. No new display signal is needed.
- **Partial-failure semantics.** If the file deletion fails (locked, permission), the reap returns a warning diagnostic, the catalog entry is still unloaded (the asset is gone from the catalog), and the orphaned file is reported — the catalog is never left pointing at a deleted file, and disk orphans are surfaced rather than hidden. This mirrors the OutputCommitter's publish-rollback posture.

### Decision-rich shape (from the design, not a working demo)

The reap request and result:

```text
ReapRequest { AssetId id; }
ReapResult  { bool unloaded; bool sourceDeleted; QVector<Diagnostic> diagnostics; }
  // unloaded       — the asset was removed from the catalog
  // sourceDeleted  — the on-disk file was deleted (false if not DeletableSource, or deletion failed)
```

The two host entry points on the Data Manager:

```text
Result<void> reapSessionTemporaries();   // reaps every SessionTemporary asset that is idle
ReapResult   reap( const ReapRequest& ); // single-asset reap (refuses persistent, refuses leased)
Result<void> promote( AssetId id );      // temporary -> ProjectPersistent, keeps identity+provenance
```

## Testing Decisions

- **The seam is the Data Manager.** All reaping and promotion behavior is tested through the `DataManager` interface, exactly as the existing unload tests in `tests/test_data_manager.cpp` do (`planUnload(...).confirmedCascade()` → `unload(plan)` → assert `assetAboutToUnload` count and `asset(id).has_value()`). The new tests mirror that pattern.
- **A good test asserts external behavior only**: that a reaped temporary is gone from the catalog, that its file is deleted from disk, that a persistent asset is refused, that a leased temporary is refused, that promote changes the policy and preserves identity/revision/provenance, and that the session-temporary sweep reaps exactly the SessionTemporary assets and leaves everything else.
- **No disk orphans are left silent:** a test stages a real published file (via the real OutputCommitter against a fixture, or a simpler direct register with `DeletableSource`), reaps it, and asserts `QFile::exists(stablePath)` is false.
- **Host integration (ProjectContext reap-on-close) is tested** by asserting the catalog is empty of SessionTemporary assets after the close path runs — but the Data Manager reap methods themselves are the primary unit, since the host is a thin caller.
- **Prior art:** `test_data_manager.cpp` (unload, lease, signals), `test_output_committer.cpp` (publish + registration round trip against real fixtures), `test_data_project_roundtrip.cpp` (serialization filtering of non-persistent assets — promotes must round-trip into the `.qgz`).

## Out of Scope

- **Auto-GC on lease release.** Explicitly rejected above; the host drives timing.
- **A last-lease-released signal.** Not added; would widen the interface against the plan rule.
- **True per-task asset ownership (a task-id column).** TaskTemporary reaping uses the policy query at task-scope end, not per-task binding.
- **Cross-session temporary recovery.** Temporary files orphaned by a crash are not recovered or cleaned by this wave (the catalog is gone with the session); a future "scratch directory sweep on startup" is separate.
- **Prompting the user on close.** Whether the host warns the user before reaping leased session-temporaries on close is a host/UI decision, not a Data Manager behavior; the Data Manager just refuses leased assets and reports them.
- **Promoting across a copy/move.** Promote is a policy flip on the same file, not a copy to a project-managed location. Renaming/moving the file into a project data folder is a later wave (the "materialize" action mentioned in the parent spec).

## Further Notes

- This wave makes the parent spec's lines 148-152 ("cleaned when the project session closes" / "cleaned when their task scope ends and no lease remains") actually true, and closes the contradiction with line 420 ("unload never deletes") by making deletion a distinct, capability-limited, temporary-only operation.
- The `DeletableSource` capability already exists in the enum (parent spec line 107) but is not yet stamped by any provider. The OutputCommitter change (stamping `DeletableSource` on published outputs) is the first real use of that capability — exactly the "expose a capability only when a real caller requires it" discipline the plan asks for.
- Ordering within this wave: (1) `DeletableSource` stamping on committer outputs + reap refusal rules, (2) the reap + promote Data Manager operations with tests, (3) host wiring in `ProjectContext` (session close) and the task host (task-scope end), (4) panel indication of temporary vs persistent. Each is a small commit.
