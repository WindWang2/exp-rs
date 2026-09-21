# ORACLES — executable acceptance conditions (Mission Runtime 13.0)

Every oracle below is a machine-checked assertion in the gate suite (or a documented
command). Gate suite = the out-of-tree Catch2 harness `mission-runtime-gate/` (real repo
sources, Qt6::Core + Qt6::Xml + jsoncpp only) plus, where a target is registered, the
in-repo ctest targets.

## Domain oracles (this Track)

- **O1 — single persistence authority.** Saving a mission writes exactly one authority
  document (the MissionContext sidecar/XML pair, timeline embedded in
  `metadata["mission_timeline"]`). Assert: after `saveMissionRuntime`, the context sidecar
  contains the embedded timeline; the legacy `<stem>.mission-timeline.json` sidecar is
  never created by the runtime save path.
- **O2 — tamper cannot silently overwrite.** With a valid authority document present,
  a tampered/forged legacy sidecar (different mission id, extra tasks, bumped revision)
  is ignored on load: the loaded timeline equals the embedded one, byte-for-byte
  projection equality, and a subsequent save does not adopt the tampered content.
- **O3 — legacy 12.0 migration round-trip.** A 12.0-era project (context sidecar without
  an embedded timeline + a legacy timeline sidecar written by 12.0's store) opens: the
  timeline is adopted, `metadata["mission_timeline_migrated_from"]` records the source,
  and a save re-persists it as the new single authority. A second open is idempotent
  (no second migration event).
- **O4 — fail closed on unknown future version.** An embedded timeline (or legacy
  sidecar) with `schema_version` "2.0" / unknown kind is refused with a machine code and
  no partial state; a corrupt authority sidecar does not destroy the last known usable
  state: load falls back to the last-good snapshot, and the recovered state saves cleanly.
- **O5 — reopen / restart / recovery E2E.** Save → fresh load in a new process (new
  harness binary run) reproduces the same projection bytes; app-restart equivalence is
  modelled by a cold reload of the runtime store from disk only.
- **O6 — layer delete/rename recovery.** Deleting a referenced layer marks dependent
  tasks `Stale` (via reconcile + apply, ids resolved once); renaming preserves status,
  attempts and run binding; both are observable through the same projection on GUI,
  MCP and Pi paths.
- **O7 — real `mission:*` tools in a fresh process.** The three tool objects are
  constructed through the real `SpatialToolRegistry`, invoked with the real
  `validateAgainstRequired` gate, and: `mission:context` / `mission:timeline` return the
  canonical projection; `mission:advance` performs a legal transition, refuses an
  illegal one without mutation (revision unchanged), refuses `Pending→Running` without a
  bound run authority, and rebinds/retries with lineage.
- **O8 — no fake Running after crash/reopen.** A task left `Running` whose run authority
  no longer exists is reconciled to `Stale` (code `stale_run_reference`) on reopen; a
  `Running` task whose authority reports a terminal success is reconciled to `Succeeded`,
  failure to `Failed`. The runtime never reports a task as Running with no live run.
- **O9 — surface parity (GUI == MCP == Pi).** For the same timeline, the desktop model
  row, the `mission:timeline` tool payload and the timeline projection are byte-equal;
  the surface registry has no duplicates/phantoms; deleting one tool wiring (MCP dispatch
  branch, provider prefix, registry entry, Pi category) turns the gate red — proven by
  source-text gates that parse the real `mcp_server.cpp`, `spatial_tool_provider.cpp`,
  `spatial_tool.cpp` and `pi/exp-rs-spatial.ts`.
- **O10 — scale.** ≥ 300 layers, ≥ 3000 tasks, ≥ 10000 events: incremental apply touches
  only the rows the batch's events carry (structural counters, no fragile ms thresholds);
  persistence save/load round-trips at that size with O(tasks+events) work; no per-event
  full-model reset.
- **O11 — potency.** Injected defects each turn at least one gate red:
  (a) drop one surface wiring; (b) mark a task Running with no run authority
  (fake Running); (c) a stale layer pointer treated as alive. Each injection is reverted
  after the red is observed.

## Track-general oracles (prompt §8)

- [ ] worktree created from the fetched `origin/master` (79adfe78a);
- [ ] PRs/issues/reviews/branches read + deduped (BASELINE/DEDUP);
- [ ] no duplicate implementation of an open PR;
- [ ] changes confined to OWNERSHIP;
- [ ] targeted build of the gate harness succeeds;
- [ ] the full gate suite passes **twice consecutively** on the final commit;
- [ ] at least one new test proven to catch an injected/regression error (O11);
- [ ] independent review P0=0, P1=0, second pass after fixes;
- [ ] `git diff --check` clean;
- [ ] pre-PR fetch + overlap scan, master drift handled;
- [ ] worktree clean, all intended changes committed;
- [ ] PR created, base `master`, no self-merge, no CI wait.

## Gate commands (reproducible)

```bash
export PATH=/tmp/cmake-3.30.5-linux-x86_64/bin:$PATH
cmake -S mission-runtime-gate -B mission-runtime-gate/build -G Ninja -DCMAKE_BUILD_TYPE=Release \
      -DFETCHCONTENT_SOURCE_DIR_CATCH2=<local catch2 src>
cmake --build mission-runtime-gate/build -j2
QT_QPA_PLATFORM=offscreen ctest --test-dir mission-runtime-gate/build -j1 --output-on-failure
```
