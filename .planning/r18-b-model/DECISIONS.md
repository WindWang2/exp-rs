# R18-B Decisions (autonomy=full defaults; per AGENTS.md unattended-mode rule)

- **D1 Skill fallback:** `/goal-loop` not installed on host → native ZCode loop + task-book Oracle protocol (disclosed in BASELINE; no fake slash-command success).
- **D2 Build:** fresh `cmake -S . -B build-r18-b` inside the worktree (avoid preset hard-coded paths shared with other tracks), `ENABLE_TESTS=ON`, `-j2` hard cap under the global `flock` five-track lock; `-j1` if RAM pressure observed. Never edit resource-guard scripts to raise parallelism.
- **D3 Scope:** deepen existing modules only. No new ML frameworks, no new provider abstraction, no rewrite of R18-A math kernels.
- **D4 Increments only:** every candidate defect is first checked against r14-B (#1508) / r15-C (#1515) fix lists; already-fixed items get regression verification at most, never a duplicate patch.
- **D5 Reproduction discipline:** each fix needs a minimal RED (new test case failing on base) then GREEN ×2; if a candidate cannot be reproduced, mark FALSE POSITIVE and move on — no patch-for-patch's-sake.
- **D6 Provider lanes without deps (CUDA/TRT/OpenVINO/ORT if absent):** compile-only verification + typed-failure tests via mocks where the project already has mock seams; every non-run lane is listed NOT_EXECUTED with reason.
- **D7 Perf claims:** only measured deltas with same input/config, ≥3 runs, hot/cold split; no unmeasured speedup ratios.
- **D8 Shared test registration:** new tests appended as independent blocks in the model-runtime section of `tests/CMakeLists.txt` only if the project offers no alternative; conflict re-scan before commit; flagged in PR.
- **D9 Language:** code/comments in English (match repo); planning docs may be bilingual like predecessors.
- **D10 Review independence:** R1 reviewer = fresh subagent that authored nothing; R2 = different fresh context. Fix rounds between reviews.
