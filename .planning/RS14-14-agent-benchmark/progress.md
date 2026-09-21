# Progress — RS14-14-agent-benchmark

| Round | Change | Verification | Status | Next |
|---|---|---|---|---|
| 1 | recon/plan/slices + dynamic dedup (no open PRs; master at 4f6632e1f6; no benchmark-harness overlap in issues) | docs written; worktree `../exp-rs-wt-rs14-agent-benchmark` on `agent/rs14-agent-benchmark` @ 4f6632e1f6 | PASS | Slice 0 skeleton |
| 2 | Slice 0 skeleton + smoke (json_writer, lane wiring) | ctest ^test_agentbench_core:: 5/5 green (0.05s) | PASS | Slice A |
| 3 | Slice A case schema v1 (parse/validate/version/digest; RED→GREEN; fixed cross-ref order + test helper double-serialize bug) | ctest ^test_agentbench_case:: 9/9 green | PASS | Slice B trace/replay |
| 4 | Slice B trace schema v1 + replay validation (allowed tools, scope roots, budget/retry accounting) + workspace_roots case validation | ctest ^test_agentbench* 24/24 green (0.13s) | PASS | Slice C fake agent |
| 5 | Slice C deterministic fake agent (script/v1: scripted policy, fault application, on_failure abort/retry_once/skip, honest claims; byte-identical reruns) | ctest ^test_agentbench* 33/33 green | PASS | Slice D invariants (F folded into E: fault mechanics done here; recovery METRIC lands with metrics) |
| 6 | Slice D invariant oracle — 14 closed kinds, pass+fail probes each, invalid-params degradation, order preservation | ctest ^test_agentbench* 47/47 green | PASS | Slice E metrics+taxonomy |
