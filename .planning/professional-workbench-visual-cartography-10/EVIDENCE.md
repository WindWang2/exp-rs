# EVIDENCE

## Phase 0

- `git fetch --all --prune` → ok；`git rev-parse origin/master` → `7d78059d1a6d316d606656759a506d17bc5e3b55`
- `git worktree add ../exp-rs-professional-workbench-visual-cartography-10 -b zcode/professional-workbench-visual-cartography-10 origin/master` → ok
- `git check-ignore -v .planning/professional-workbench-visual-cartography-10/GOAL.md` → 匹配 `!` 否定规则（negation），`git add -n` 确认可跟踪
- `cmake --preset dev-default`（CMAKE_BUILD_PARALLEL_LEVEL=2）→ **exit 0**，"Build files have been written to: .../build-dev"（2026-09-14）
- 去重分析：见 BASELINE.md（30+ PR 逐条、4 个并行 10.0 OPEN PR、DEDUPE 对照）
- 措辞自查：`grep -E "尽量|适当|必要时|合理|充分|酌情" GOAL.md | grep -vc "grep -E"` → 见下

### 存在性断言（Phase 0 版，最终 PR 前重跑）
