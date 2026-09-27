# EVIDENCE — Track R4: Plugin Lifecycle & exprs Registry Rollback

All runs: fresh `build-dev/` + light `build-sdk/` in the worktree, Debug,
`ENABLE_TESTS=ON`, `ninja -j2`, `ctest -j1` policy (binaries run directly),
`QT_QPA_PLATFORM=offscreen`,
`LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib` (GDAL/PROJ/GEOS prefix).

## §1 P1-9 is NOT reproducible on Linux — the three historical failures are
       Windows-specific (WP-A pivot evidence)

Claim under test (prompt §三 / PR #1334): `test_exprs_plugin_loader` fails 3 of
34 cases — "hot reload swaps in a valid new manifest and refuses a broken one"
(:493), "installOrUpgrade installs a fresh package, then atomically upgrades it"
(:1138), "installOrUpgrade rolls back when the new version cannot load"
(:1226). PR #1334 measured `31/34` **on Windows** and left P1-9 open.

Measured on this Linux host, master `15e5c66b5` code, fresh build:

- pass 1: `All tests passed (281 assertions in 34 test cases)` — full binary
- pass 2 (double-run rule): `All tests passed (281 assertions in 34 test cases)`
- single-case selectors for :493 and :1226 additionally pass individually;
  :1138's selector needs comma-escaping (Catch2 treats `,` as a separator) and
  is covered by both full-suite passes.

Command (both passes):
`cd build-dev && QT_QPA_PLATFORM=offscreen LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib ./tests/test_exprs_plugin_loader`

Conclusion: P1-9's three failures are platform-specific to the Windows lane
(where #1334 measured them). Per the prompt's own pivot rule (§二.6: "按实测
剩余失败集重新认领"), WP-A on this Linux track is re-claimed as:

1. prove and document the non-reproducibility (this section), and
2. sweep ALL plugin/exprs targets on Linux for the actually-failing set
   (§2), fixing anything real that appears inside the whitelist.

Cross-platform hypotheses for the Windows-only failure (not pursued — no
Windows lane on this host, outside this track's scope): Windows path
separators/case-folding in fixture manifests, DLL dependency resolution in
`PluginLoader` (the POSIX RPATH `$ORIGIN` advantage), or
`qEnvironmentVariable`/qputenv semantics. Candidates already carry typed
platform seams (see plugin_loader.cpp:22 "Platform library-loading seam").

## §2 Linux sweep + WP-F gate: 20/20 suites green, twice consecutively

All 20 plugin/exprs test targets built in the fresh heavy `build-dev/` and run
directly (binaries, `-j1` semantics, offscreen). PASS 1 and PASS 2 both:
**20/20 "All tests passed"**, zero failures — logs: `gate_pass1.txt` /
`gate_pass2.txt` in this directory (1,873 assertions per pass, post-review
re-run). Highlights: loader 281/34, manifest 378/11, plugin_system 129/16,
host_process 336/30, capabilities 112/13, ipc 136/24, and the five r4 suites
(halffail 68/9, unload_order 34/4, boundary 35/8, parity 10/3, pollution
19/3).

Sweep finding (pre-existing, fixed in this track): the ONLY red on master
Linux was `test_exprs_ipc` "the recv cap refuses oversized peer frames (E6003)"
— root-caused and fixed (commit 6716a435b, ledger round 3; the case now runs
deterministically green 5×+ suite).

Gate-command note: the prompt's `ctest -R "plugin|exprs"` selects by Catch2
CASE NAME (catch_discover_tests registers bare case names; PRE_TEST discovery
does not even list them at `ctest -N`). Case names like "installOrUpgrade
installs a fresh package..." contain neither word, so the selector matches
almost nothing — the honest equivalent used here (and by every sibling track)
is running each of the 20 plugin/exprs BINARIES directly, twice.

## §3 New r4 suites — red/green provenance

- `test_plugin_lifecycle_halffail_r4` — 9 cases / 67 assertions, ALL GREEN
  (pass 1 and 2). Notes: initially 8/9; the failing assertion looked for the
  dependency-spec refusal in the REGISTRY diagnostics — the scan-time
  validation verdict lives on the RECORD's diagnostics (plugin_discovery.cpp
  inspectDirectory fills record.diagnostics). Test corrected to the real
  seam; no product change needed (the refusal is already typed).
- `test_plugin_lifecycle_unload_order_r4` — 4 cases / 34 assertions, ALL
  GREEN ×2. First run 0/4: the fixture forgot to write the manifest (the
  hello fixture dir ships none — same deal as ReloadFixture). Test corrected.
- `test_plugin_host_boundary_r4` — 8 cases / 35 assertions, ALL GREEN ×2.
  First run 7/8: the load-time containment recheck (#756) only fires for a
  record already Validated at scan time AND with a sink installed (the
  preflight refuses with RegistrationFailed otherwise, state untouched).
  Test corrected to plant the symlink AFTER the scan — exactly the TOCTOU
  window the recheck exists for. No product change needed.
- `test_plugin_channel_parity_r4` — 3 cases / 10 assertions, ALL GREEN ×2.
  Two corrections during bring-up: (a) both load paths refuse at the
  preflight without a contribution sink — the parity harness now installs
  one; (b) the id-mismatch sample must target the DECLARED manifest id (the
  record id), not the fixture directory id. Finding: the host-process
  channel folds the worker's typed InitializationFailed into
  LibraryLoadFailed — documented drift (DECISIONS.md D-6), pinned as the
  observed contract.

## §4 Master build break fixed en route (mirrors open PR #1335)

`libsicnu_agent.so` references `agent_loop::VerificationReport::aggregate`
without linking `sicnu_agent_loop` — on ELF every downstream consumer fails
to link (first hit: `tests/test_exprs_plugin_loader`). This is exactly the
break open PR #1335 ("restore master build/CI") repairs; to build the heavy
lane on this branch, the same minimal link-line fix is carried locally in
`src/agent/CMakeLists.txt` (attributed in-code; drops cleanly on rebase).

## §5 Environment facts

- cmake 4.1.2 `/home/kevin/tools/cmake-4.1.2-linux-x86_64/bin`, ninja
  `/home/kevin/tools/ninja-bin`, gcc 16.2.1, Qt 6.11.2 (system),
  GDAL 3.13.3 / PROJ 9.8.1 / GEOS 3.15.0 via `/home/kevin/pwb-sdks/root/usr`
  (recipe inherited from the planner-repair worktree's CMakeCache, 2026-09-23).
- `cmake --preset dev-default -G Ninja -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`.
- The shared `.goal-loop-ledger.md` is a TRACKED append-only file (D-4);
  open sibling PRs #1334/#1336/#1338 all modify it — expected textual
  conflict at merge, union resolves trivially.
