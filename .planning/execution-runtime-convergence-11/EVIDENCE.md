# EVIDENCE — 本地验证记录（Local evidence only; no online CI dependency）

格式：每条 = 日期/Phase | 命令 | exit code | 关键输出摘要。所有 claim 均可本地复现。

## Phase 0（2026-09-15）

| 命令 | exit | 摘要 |
|---|---|---|
| `git fetch origin --prune` | 0 | master 前移 007e70cf→a5b11b7f；新 remote 分支 zcode/radiometric-spectral-workbench |
| `git rev-parse origin/master` | 0 | `a5b11b7f10fa010c1c060864fb427d777ba9a4aa` |
| `gh pr list --state open --limit 100` | 0 | 仅 #1008（radiometric-spectral-workbench，DIRTY） |
| `gh issue list --state open --limit 200` | 0 | #1001–#1007 共 7 条（R2 残留，均非 execution 域） |
| `gh pr view 1008 --json ...` | 0 | 41 files，与本 track scope 交集 0（BASELINE.md） |
| `git worktree add ../exp-rs-execution-runtime-convergence-11 -b zcode/execution-runtime-convergence-11 origin/master` | 0 | HEAD=a5b11b7f |
| 三路只读 Explore 审计（runtime / jobs+authority / operators seam） | 0 | BASELINE.md「代码结构审计结论」 |

## OUT_OF_SCOPE（范围外发现，不修）

- issue #1001 io:clip srcCrsOverride 误作 targetCrs（critical/P1）— src/operators/io，非本 track scope。
- issue #1002 workflow registry node executor fail-open（critical/P1）— src/workflow。
- issue #1003 dataset joinFeaturesBySampleId JSON-null（P1）— src/dataset。
- issue #1004 agent dataset:qa scan_capped uniqueness（P1）— src/agent。
- issue #1005 georef mapPickToLayerCrs（P1）— georeferencer。
- issue #1006 workflow PipelineRunCoordinator syntheticExecute 默认（P2）— src/workflow（注：代码中类名为 WorkflowRunCoordinator）。
- issue #1007 dataset:qa 不审计 CRS（P2）— src/dataset。
- 审计另见：ChunkedProcessor 注释声称 JobEngine clamp 2..4 线程与实际 cores−1 不符（预算注释过期，P3 记录不修——src/processing 域）。

## 资源监控说明

宿主 Windows + Git Bash：load average 不可直接测量（无 /proc/loadavg 语义）→ 按 GOAL 允许，记录一次 not-executed，build 一律 `-j2` 上限；RSS 用 `tasklist` 抽查记录于各 build 段落。
