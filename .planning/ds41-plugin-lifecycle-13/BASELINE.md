# BASELINE — ds41-plugin-lifecycle-13

## Live pre-read (this session, read-only master checkout)

- `git fetch origin --prune` run at session start.
- `origin/master` HEAD: `79adfe78a16b9419eef180cf9e6e5739658621a2`
  ("Merge pull request #1134 — Plugin SDK, Capability Negotiation & Lifecycle 12.0",
  commit time 2026-09-20 21:39:35 +0800).
- Recent history: `1c24274d` ledger record of the 12.0 fusion round;
  `5669a6c7` MSVC constexpr fix; everything before is 12.0 lineage.

## PRs

- Open: **#1135** (`agent/flash-temporal-phenology-12`, temporal phenology) —
  file overlap scan shows zero `src/sdk/`/`src/plugins/`/`tests/test_*plugin*`
  overlap. Not in conflict with this track.
- Merged **#1134** (`agent/flash-plugin-sdk-12`): Plugin SDK 12.0 — the direct
  baseline of this track. Its documented known limitations are exactly this
  track's four work packages:
  1. install-time upgrade still uses the old two-step path (no atomic upgrade);
  2. dev-mode `refreshLastGoodSnapshot()` copies the whole plugin dir
     synchronously after every successful load (GUI startup pays it per plugin);
  3. hot-reload snapshots in temp are only removed after a successful rollback —
     failed/abandoned dev trees leave `plugin-last-good-<id>` residue;
  4. one host-process stress flake: untyped `no such process: no such process`
     escaping the timeout+crash+cancel interleave test.

## Issues

- `gh issue list --state open --limit 200`: **no open issues**.

## Remote branches (agent/track/fix residue)

- `origin/agent/flash-plugin-sdk-12` — merged (PR #1134), residue only.
- `origin/agent/flash-temporal-phenology-12` — open PR #1135 head.
- `origin/agent/glm53-plugin-sdk-trust` and assorted older `agent/*`/`fix/*`
  branches carry no unmerged plugin-lifecycle work.
- Suggested branch `agent/ds41-plugin-lifecycle-13` is free.

## Parallel local worktrees

- `exp-rs-worktrees/ds41-raster-merge-types`, `ds41-http-fetch-strict`,
  `geo-maintenance-13` — raster/http/maintenance domains, no plugin
  ownership overlap. They hold configured `build-dev` trees; GDAL/PROJ/GEOS
  come from `/home/kevin/pwb-sdks/root/usr/lib/cmake/`.

## Worktree

- Path: `/home/kevin/project/exp-rs-worktrees/ds41-plugin-lifecycle-13`
- Branch: `agent/ds41-plugin-lifecycle-13`
- `git merge-base HEAD origin/master` = `79adfe78` ✓

## Toolchain (verified live)

- cmake 3.30.5 at `/tmp/local/bin/cmake` (repo requires ≥3.29); g++ 14,
  Qt 6.11.2, jsoncpp, GDAL/PROJ/GEOS via the pwb-sdks prefix above.
- Host: 16 cores / 62 GB shared; build cap `-j2`, tests `-j1`,
  `QT_QPA_PLATFORM=offscreen`.

## Throw-site identification (WP4, done during pre-read)

`IpcChannel::close()` (`src/sdk/exprs/ipc_channel.cpp:66-77`) runs
`mReader.joinable() && mReader.join()` with NO synchronization. Every
`killProcess` path in `PluginHostProcessSession` (timeout ladder, poisoned
drain, confirm-death, shutdown, destructor — `plugin_host_session.cpp:650-710,
754-785, 800-894, 936-1029`) funnels into `mChannel->close()`. Two concurrent
killers both pass `joinable()` → double `join()` → `std::system_error`
(`errc::no_such_process`) escapes `killProcess` → propagates out of
`request()`/`runOperator` untyped. Matches the recorded flake exactly.
Secondary same-class site: `ExecutionPool::drain()` join/detach loops in
`plugin_host_worker_main.cpp:303-319` (concurrent drainers double-join).
