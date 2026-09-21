# Progress — RS14-14-agent-benchmark

| Round | Change | Verification | Status | Next |
|---|---|---|---|---|
| 1 | recon/plan/slices + dynamic dedup (no open PRs; master at 4f6632e1f6; no benchmark-harness overlap in issues) | docs written; worktree `../exp-rs-wt-rs14-agent-benchmark` on `agent/rs14-agent-benchmark` @ 4f6632e1f6 | PASS | Slice 0 skeleton |
| 2 | Slice 0 skeleton + smoke (json_writer, lane wiring) | ctest ^test_agentbench_core:: 5/5 green (0.05s) | PASS | Slice A |
| 3 | Slice A case schema v1 (parse/validate/version/digest; RED→GREEN; fixed cross-ref order + test helper double-serialize bug) | ctest ^test_agentbench_case:: 9/9 green | PASS | Slice B trace/replay |
| 4 | Slice B trace schema v1 + replay validation (allowed tools, scope roots, budget/retry accounting) + workspace_roots case validation | ctest ^test_agentbench* 24/24 green (0.13s) | PASS | Slice C fake agent |
| 5 | Slice C deterministic fake agent (script/v1: scripted policy, fault application, on_failure abort/retry_once/skip, honest claims; byte-identical reruns) | ctest ^test_agentbench* 33/33 green | PASS | Slice D invariants (F folded into E: fault mechanics done here; recovery METRIC lands with metrics) |
| 6 | Slice D invariant oracle — 14 closed kinds, pass+fail probes each, invalid-params degradation, order preservation | ctest ^test_agentbench* 47/47 green | PASS | Slice E metrics+taxonomy |
| 7 | Slice E evaluator — 8 metrics (null-with-reason), closed failure taxonomy w/ deterministic priority, recovery quality from observable fault markers, double-eval reproducibility oracle, versioned evaluation doc (folds Slice F fault metric) | ctest ^test_agentbench_evaluator:: 11/11; suite 58/58 | PASS | Slice G report |
| 8 | Slice G report — deterministic Markdown + pretty JSON (versioned evaluation doc), typed write failures, no partial files | ctest ^test_agentbench_report:: 3/3; suite 61/61 | PASS | Slice H suite+corpus |
| 9 | Slice H — suite runner (loader-injected, pack digest over sorted pins, suite_report/v1) + 24-case/4-trace starter pack via deterministic generator + corpus validation (families, sizes, pinned summary 20P/4F, FNV digest pins, mutation moves packDigest) + docs (corpus README, benchmark-harness.md, integration.md) | ctest ^test_agentbench* 71/71 green | PASS | review gate 1 |
