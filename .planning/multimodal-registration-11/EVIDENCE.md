# EVIDENCE — multimodal-registration-11

## Phase 0（2026-09-15）

- `git fetch origin --prune`：首次 TLS EOF（网络抖动），重试成功。origin/master=`a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- `gh pr list`（GraphQL）连续 3 次 EOF → 改用 REST `gh api repos/.../pulls?state=open` 成功：唯一 open PR #1008（spectral）。
- `gh issue list` 成功：#1001–#1007（dedupe 见 BASELINE.md；#1005 in-scope）。
- `git branch -r --sort=-committerdate`：除 itk-upstream 外只有 `origin/zcode/radiometric-spectral-workbench`（= PR #1008）。
- PR #1008 changed files 通过 REST 拉取成功（53 个文件清单见 PARALLEL_OWNERSHIP.md；count 查询再次 EOF，清单本身完整）。
- ISSUES.md / CHANGELOG.md / docs/agents/goal-template.md 已读；ISSUES.md 全条目 dedupe 结论见 BASELINE.md（无本 track 工作）。
- Subagent #1（Explore，只读）完成 geometric 域深度审计：14 个主题 + A–H gap table（结论内嵌 BASELINE.md / CURRENT_ARCHITECTURE.md）。
- 资源基线：16 核 / 64 GiB / load 3.23（启动时）。build `-j2` 上限执行。
- worktree 创建：`git worktree add ../exp-rs-multimodal-registration-11 -b zcode/multimodal-registration-11 origin/master` → exit 0 @ a5b11b7f10。

## Build 基线（Phase 1 前配置/构建记录）

（待填：configure preset exit code、build 时长、并行级别、CPU/RSS 采样、基线 targeted tests 结果）

## OUT_OF_SCOPE

- #1001 io:clip CRS override（P1，io 域）
- #1002 workflow registry artifact 校验（P1，workflow 域）
- #1003 dataset join null 列（P1，dataset 域）
- #1004 dataset:qa scan_capped identity（P1，dataset/agent 域）
- #1006 PipelineRunCoordinator syntheticExecute 默认（P2，workflow 域）
- #1007 dataset:qa CRS 审计缺失（P2，dataset 域）
以上问题在 PR_BODY 顶部转述（P1 级），本 track 不修复。
