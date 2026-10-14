# R13-I Decisions

- D1 (2026-10-14): Host has no `/teamwork-preview` or `/goal-loop` slash command (Kimi Code CLI; skill dirs checked). Equivalent used: Agent subagents with implementation/review role separation (max 2 concurrent), and a prompt-driven Oracle→RED→fix→GREEN×2→ledger loop. Not claimed as slash commands.
- D2: Do NOT build on dormant `fix/all-open-issues` branch even though it touches #1490/#1491/#1494/#1495: it rewrites ~35 src/** production files (forbidden for this track), its #1495 fix (drop continue-on-error without scope proof) risks legacy flooding, and it is an abandoned all-issues megabranch. Its per-commit claims were used only as pointers to verify independently.
- D3: #1468 is verified-already-fixed on master → record verified-no-op, run fixture tests as GREEN evidence, do not manufacture diffs.
- D4: test_io_vector_contract:352 failure (#1491's failing test) is geospatial-io domain → cross-domain defect note in FINAL_REPORT, not fixed here (write scope + dormant-branch ownership both forbid).
- D5: CI concurrency fix shape: `group: ${{ github.workflow }}-${{ github.event_name == 'push' && github.sha || github.ref }}` — per-SHA groups on push (each master push gets its own uninterruptible run), per-ref groups on PR (new commits still cancel the old PR run via cancel-in-progress expression). Matches issue #1490's own suggestion.
- D6: Build policy: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`, `QT_QPA_PLATFORM=offscreen`, global flock `/tmp/exp-rs-agent-locks/global-build.lock` for build/link/large-test phases.
