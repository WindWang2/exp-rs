# DECISIONS — Track 4: Plugin Lifecycle & exprs Registry Rollback (R4)

Append-only. One entry per decision, each with evidence and scope consequence.

## D-1: Dual build lanes (heavy + light SDK), each capped -j2

The `test_exprs_plugin_loader` target (WP-A) links the full QGIS closure (~3,588
ninja steps; hours at -j2). The four NEW r4 test targets use the repo's
`sicnu_add_sdk_test` light lane (Catch2 + sicnu_sdk, the same lane
`test_plugin_host_process` uses). To iterate on the new tests while the heavy
closure builds, a second build dir (`build-sdk`) compiles only the light target
set. Precedent: the ds41 tracks' "second build dir (build-fast, sicnu_data only)"
rounds. Resource accounting: each lane `ninja -j2` (4 compiler jobs total from
this track), RSS checked per round, forced to -j1 per the standing rule when
RSS > 70%. CTEST stays -j1 globally — test RUNS are strictly serialized.

## D-2: WP-D narrowed to on-master seams (prompt premise drift)

The prompt places #1334's legacy-PluginHost gates (module whitelist,
world-writable-dir rejection, Qt-metadata IID pre-check) inside `src/plugins/`
and calls them "既有门" (existing gates). Measured reality (Phase 0):

- The legacy PluginHost lives in `src/core/plugin_host.{h,cpp}` — OUTSIDE this
  track's file whitelist.
- #1334 (which ADDS those gates) is an OPEN, unmerged PR — master
  `15e5c66b5` does not contain the whitelist at all.

Consequence: boundary tests pin the containment/trust surfaces that DO exist on
master inside the whitelist (`src/sdk/exprs/path_policy.*`, manifest
validation/containment, registry trust gates, loader load-time re-check). The
#1334 gates are documented here and in the test-file header; asserting them
today would mean red tests for unbuilt code (or out-of-whitelist edits) — both
forbidden. After #1334 merges, the natural home for those boundary tests is
#1334's own `tests/test_plugin_host_allowlist.cpp`.

## D-3: WP-B failure class 3 narrowed ("dependency missing" is not injectable)

The prompt's half-init matrix class 3 assumes a load-time dependency check
("manifest 声明缺失依赖 → 加载中止于依赖检查"). Measured: the validator checks
dependency SPEC FORMAT only (plugin_validator.cpp:560, ManifestInvalidField on
malformed specs); there is NO load-time dependency-resolution gate on master.
Adding resolution = new functionality — forbidden by the track's iron law.
Consequence: class 3 is pinned at the seam that exists (invalid dependency spec
→ typed validation refusal, zero writes) and the gap is documented in the test
header; a resolution gate is a feature request, not a hardening fix.

## D-4: `.goal-loop-ledger.md` is a tracked, append-only shared file

The prompt says the ledger is gitignored per-worktree. Measured: the file is
TRACKED (`.gitignore` lists it, but it was force-added long ago) and every
prior track APPENDED its section — open PRs #1334/#1336/#1338 all carry
`.goal-loop-ledger.md` modifications. Overwriting it destroys sibling tracks'
process history (a P1 finding in the flash-data-scale-13 review: "F0 ledger
overwrite"). Consequence: this track appends; the file will conflict textually
with sibling PRs at merge time — the append-only union resolves trivially.

## D-5: Token-budget numbers are estimates, not measurements

The prompt's §4.3 token budget table (280M total) assumes a harness that
reports per-turn token consumption. This harness exposes none. Fabricating
per-round token counts would poison the audit trail. Consequence: the ledger
records rounds, verification evidence, and honest order-of-magnitude effort
notes; the 4.2 DELIVERABLE floors (fixtures/variants/commits/files counts) —
which ARE objectively checkable — are the binding gate, not token arithmetic.

## D-6: Host-process channel folds worker-typed load refusals (documented drift)

Measured in WP-E parity probes: when the worker refuses a payload whose
`pluginId()` disagrees with the manifest, the WORKER produces the typed
`InitializationFailed` (E4003), but the session re-reports only
`LibraryLoadFailed` (E4002, "worker plugin.load failed: worker failed to load
the plugin binary"). The in-process channel reports E4003 directly.

Consequence: the parity table pins the OBSERVED codes (in-process E4003,
host-process E4002 for the id-mismatch sample) and the drift is documented
here and in ROLLBACK_CONTRACT.md appendix A. Preserving the worker's typed
code through the session report is a protocol/forwarding change in
`src/plugins/host/plugin_host_session.cpp` — a behavior improvement beyond
this track's fix-and-contract mandate; filed in the PR's known-issues section
rather than silently absorbed or widened in the test.
