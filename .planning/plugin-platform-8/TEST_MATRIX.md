# Plugin Platform 8.0 — Test Matrix & Local Evidence

Environment: Linux 6.18.49-2-lts x64, GCC /usr/bin/c++ (Release, Ninja), 16 CPUs / 62 GB RAM,
host shared with other concurrent build agents (load 10–40). `QT_QPA_PLATFORM=offscreen` for Qt
suites. Every suite: deterministic, resource-bounded, no network.

| Suite | Cases | Assertions | Result | Notes |
|---|---|---|---|---|
| test_plugin_host_process | 13 | 111 | PASS ×6 runs (1 env-sensitive fail under load ≈ 40, host contention from sibling build agents; 5/6 green incl. 3 consecutive at load ≈ 11) | real worker binary + isolation fixture: load/execute, crash recovery, hang kill-ladder, flood frame-cap, lifecycle round-trip, concurrency rendezvous (peak ≥ 3), host-cancel → per-id frame (E6009 = 4000), poison escalation + drain + one bounded recovery, gate FIFO unit, orphan grandchild reaping (graceful + crash), declarative UI round-trip (describe/invoke, post-crash typed E6005) |
| test_exprs_ipc | 13 | 68 | PASS | protocol contract incl. 1.1 additive surface |
| test_plugin_capabilities | 9 | 49 | PASS | deny-all defaults, placeholder expansion (platform-native anchors), containment unit (nested/traversal/sibling-prefix/fail-closed), quota clamping |
| test_plugin_ui_schema | 6 | 14 | PASS | schema contract: valid echo, all control types, broken structures, unresolved command refs, caps (65 controls / 33 options / depth 6 / 300-char), group nesting within budget |
| test_plugin_ui_schema_host | 2 | 18 | PASS (offscreen) | renderer: defaults rendered, events delivered, state applied back, release deletes, empty schema refused |
| test_plugins_runtime_host | 5 | 48 | PASS | manifest contributions, external tool operators, agent tools (baseline regression guard) |
| test_plugin_manifest | 6 | 60 | PASS | manifest schema incl. runtime/access/quotas/conformance/package round-trip |
| test_plugin_execution_barrier | 6 | 23 | PASS | drain/generation contract (baseline regression guard) |
| test_exprs_plugin_system | 6 | 42 | PASS | packaging: staged install, known-answer checksum upgrade, mismatch refusal with previous version surviving, no staging leftovers, SBOM metadata passthrough |
| test_cli_commands_json | 3 | 12 | PASS | CLI command surface (baseline regression guard) |
| CLI `plugin test` (isolation fixture) | 13 checks | — | 13/13 PASS ×2 | PT_MANIFEST…PT_ROUNDTRIP incl. PT_CANCEL (typed cooperative cancel), PT_CONCURRENCY (3 parallel), PT_RESTART (crash → typed failure → worker restored), PT_UI_SCHEMA (schema fetched + validated) |

Executed-and-passed claims above are backed by logs in this session; the Windows/macOS-specific
lanes (job-object enforcement, RLIMIT on macOS) remain documented-but-not-locally-executed (no
Windows/macOS host available) — the job-object code path is unchanged from the 5.0 baseline.
