# Plugin Platform 8.0 — Final Report

Branch: `feat/plugin-platform-8` (worktree from `origin/master` @ `2d4f0daedd`)
Status: implementation complete, adversarially reviewed and remediated, all target suites green.

## Baseline (verified against latest master, PR #830 = isolation runtime 5.0)
Baseline audit + capability matrix: BASELINE.md / CAPABILITY_MATRIX.md. No open PRs/issues; all
5/6/7.0 branches merged residue; one concurrent local 8.0 worktree (dataset/experiment) shares no
files with this track.

## Delivered (by work package)
- **A/B. Host protocol 1.1 + real concurrency**: worker execution pool (≤ 8, quota-negotiated),
  per-id cooperative cancel (incl. host-side context cancel reaching the plugin — new), FIFO-fair
  exact `maxRequestConcurrency` gate with typed E6007 overload refusal, poison-on-timeout with
  drain-kill, per-request progress coalescing, bounded host event queue, downward frame-cap
  negotiation. v1 peers remain compatible (documented semantics).
- **C. Cross-platform isolation**: POSIX setpgid + process-group kill on ladder/crash/shutdown,
  FD_CLOEXEC hygiene, allocation-free fork child, parent-side RLIMIT sanity + pre-exec RLIMIT_AS,
  Windows job-object semantics preserved; orphan-grandchild tests cover graceful + crash paths.
- **D. Capability 2.0**: opt-in worker-side workDir containment vs declared write roots (typed
  E5005), `pathIsWithinRoot` contract, honest error-code mapping (no more atoi→Success), manifest
  round-trip fixes (`runtime`/`access`/`quotas` silently dropped by toJson — three baseline bugs).
- **E. Declarative out-of-process UI**: validated schema model (hard caps), `ui.describe` /
  `ui.invoke`, worker probe via optional entry point, host renderer through the existing
  reverse-ownership `UiShellSink`, bounded serialized event delivery, generation-free release;
  shell placement documented as the workbench track's seam.
- **F/H. Lifecycle + conformance kit**: reload-adapter restoration fix (unload→reload left the
  atomic catalog empty — found by the kit), PT_CANCEL / PT_CONCURRENCY / PT_RESTART / PT_UI_SCHEMA
  with strictly typed verdicts and honest skipped-counting.
- **G. Packaging**: staged install with atomic swap and rollback (v1 deleted the previous install
  first), self-contained SHA-256 pinned to independent known-answer vectors, `package` metadata
  (checksums verified; SBOM/signature carried, integrity-not-authenticity).
- **I. Docs/DX**: capabilities.md written (was a dangling reference), host-process.md protocol
  1.1 sections, declarative-ui.md, versioning.md fourth axis, conformance failure details.

## Verification (local evidence; no online CI waited on)
9 test suites green post-remediation: 111/68/49/14/48/60/23/54/18 assertions
(test_plugin_host_process, test_exprs_ipc, test_plugin_capabilities, test_plugin_ui_schema,
test_plugins_runtime_host, test_plugin_manifest, test_plugin_execution_barrier,
test_exprs_plugin_system, test_plugin_ui_schema_host) + test_cli_commands_json 12 assertions.
CLI `plugin test` on the misbehaving fixture: 13/13 checks with all declared 8.0 targets active.
Full details: TEST_MATRIX.md. Performance/bounds: PERFORMANCE.md.

## Adversarial review
Two read-only subagents, full diff; 0 P0, 5 P1, 12 P2, 27 P3 combined. All P0/P1 fixed, all
reasonable P2 fixed, P3s fixed or explicitly accepted with rationale — per-finding dispositions
in REVIEW_LOG.md. Post-fix sweep green.

## Known limitations / follow-ups
- Windows/macOS lanes not locally executable: job-object enforcement (unchanged baseline code)
  and macOS behavior are documented, not locally re-verified.
- Frame-cap negotiation is enforced for plugin→host frames; a per-direction split for host
  requests is a noted follow-up (A-P2-3).
- Declarative UI shell placement (menus/docks/preferences wiring in `src/app`) is the workbench
  track's seam; the framework surface is complete and hardened.
- test_exprs_ipc showed one transient failure under host load ≈ 40 (4 subsequent green runs);
  timing-sensitive assertions documented in TEST_MATRIX.md.
- Cross-lane baseline fixes included where they blocked verification: help-projections parser
  swallowed `--json` for every CLI subcommand; `plugin test` future-abandon semantics.

## Compatibility & migration
No manifest changes required; `runtime`, `access`, `quotas`, `conformance`, `package` are
additive and now survive round-trips. v1.0 protocol peers: a 1.0 worker serializes 1.1 host
traffic; a 1.0 host refuses a 1.1 worker with E6001 (same-SDK shipping makes this a
mismatched-deployment guard). Plugin ABI unchanged: `EXPRS_createUiSchemaProviderV1` is an
optional extra entry point, not an interface change.
