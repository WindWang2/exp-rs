# GOAL — F10 · Teaching Lab, Grading & Reproducibility Platform 11.0

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 实验数据、批改、复现报告、离线课堂和教师工作流完善
> **Branch:** `zcode/teaching-lab-platform-11`
> **Worktree:** `../exp-rs-teaching-lab-platform-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

（本文件为 `/goal` 启动提示词的存档原文；执行以 Phase 0 重新审计的
master/PR/issue/code 事实为准。原始提示词全文见 track 会话记录；
下方保留与执行有关的核心约束摘要，内容与原文一致。）

## Prompt-generation snapshot（只用于启动审计，不是固定基线）

本 Prompt 生成时（2026-09-15）观测到：
- `origin/master` = `ebcafb4d02ec3522eaaa4b3b62b1082c36280ffb`。
- open PR #991 `grok/unified-mission-workbench-d18`、#992 `grok/dataset-foundry-benchmark-d19`。
- 当时无独立 open issues；`ISSUES.md` 是旧 D3 backlog，禁止直接当实时 backlog 实施。

**启动时必须完全刷新这些事实（已完成，见 BASELINE.md）。**

## Why this track now

把"数据包→实验执行→判分→报告→班级批处理→复现"收敛成可靠教学平台，
避免重新修改已合并的 D18 LabSpec/IR2 UI 与 D19 foundry/benchmark。

## Ownership / parallelism

**Primary write scope（启动审计后可收窄，不可无理由扩大）：**
- `data/labs/**`
- `docs/labs/**`
- `src/agent/*lab*`
- `src/experiment/bridge/*lab*`
- `src/cli/*lab*`
- `scripts/*lab*`
- `tests/*lab*`

**默认 read-only / 避免并发冲突：**
- D18 LabSpec workflow UI files（#991 已合并 → 以新 master 为事实源）
- D19 experiment benchmark files（#992 已合并 → 以新 master 为事实源）

## Operating envelope（不可协商）

- 全自动：不向用户提澄清问题；歧义取最保守方案并写入 DECISIONS.md。
- `master` 永远只读；所有编辑只发生在本 track worktree/branch。
- 不 merge 其他分支，不改别人的 worktree，不 force-push。
- 不等待/不引用线上 CI；capability claim 只来自本地可复现 evidence。
- 构建资源：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、ninja `-j2`/`-j1`；
  RSS>70% 或高负载降 `-j1`；测试 `QT_QPA_PLATFORM=offscreen`、`-j1`。
- 编译期间每 60 秒记录 CPU/RSS/负载（无法测量时 EVIDENCE 记录一次并保持 -j2 上限）。
- 不新增重量级依赖；优先 Qt/GDAL/QGIS/PROJ/GEOS/OpenCV/现有 runtime。
- 输出/缓存/sidecar/DB 写入必须 atomicity、cancel、失败清理、Unicode path、
  read-only source、NoData/CRS/provenance 安全。
- 不把 wall-clock benchmark 当 correctness gate；规模证据用内存上限/操作数/逻辑规模。

## Model / subagent policy

最多 2 个 subagent（建议 #1 只读 Phase 0 审计、#2 只读最终 review）；
subagent 不得再 spawn；主 agent 完成全部写代码与裁决。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | Lab dataset pack contract | 每门实验的数据需求、fixture/generator、checksum、license、版本、offline size、sensor truth。 |
| B | grader 2.0 | 更多空间/时序/分类/制图 assertion kernels，统一 evidence、容差来源、不可验证状态。 |
| C | batch classroom | 目录/名单批量批改、隔离失败、CSV/JSON/HTML 汇总、资源上限、重复提交 identity。 |
| D | replay/report | run lineage、operator/model/data fingerprints、grade evidence、replay blockers、隐私/secret redaction。 |
| E | copilot safety/evals | 学生/教师权限、提示不泄露答案、diagnostic evidence、中文术语、prompt injection 回归。 |
| F | offline lab runner | 无网络启动、环境自检、包完整性、Windows/Linux 一键运行与可诊断失败。 |
| G | 实验内容 refresh | 对当前 10.0/11.0 能力更新实验 8–11 和可新增实验，operator_id/live schema 机器校验。 |
| H | classroom scale | 100–1000 submissions synthetic scale、并发/磁盘/取消、确定性分数。 |

每个 package：现状证据 → 设计选择（≥2 候选写 DECISIONS）→ 最小 vertical slice →
known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。

## GOAL Loop Oracle（未满足不得结束）

1. 参考答案 100 分与错误 corpus 扣分来源独立可解释
2. 学生路径无法调用 artifact-producing reference/teacher surface
3. offline 强制模式零网络且实验/批改可跑
4. lab suites 两次通过且 report 无 secret 泄漏
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；生成物/manifest drift gate clean
6. Phase 8 关键 targeted validation 原样连续运行两遍，两次都通过
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge

`.goal-loop-ledger.md` 每轮格式：

```text
Round N | 当前 Oracle 差距 | 单一聚焦改动 | 验证命令 | 实际结果/exit | PASS/FAIL | 下一步
```

## Required planning/evidence artifacts

`CURRENT_ARCHITECTURE.md`、`CAPABILITY_MATRIX.md`、`PARALLEL_OWNERSHIP.md`、
`TEST_MATRIX.md`、`PERFORMANCE.md`、`REVIEW_LOG.md`，必要时 `MIGRATION.md`/`SCHEMA.md`/`FAILURE_MATRIX.md`。

## Worktree / commit / rebase / PR runbook

见原始 GOAL（runbook 1–10 步全文在 track 会话记录中逐条执行；关键点：
Phase ≥1 commit；每 Phase 后 rebase origin/master；共享注册文件 append-only
独立 integration commit；先主 agent 全 diff review 再独立 reviewer；
最终 `git diff --check` + 冲突/secret 扫描 + targeted tests 连续两遍；
push 后 `gh pr create --base master`，不 merge、不等 checks；
PR_BODY 含 baseline SHA、dedupe/ownership、架构决定、实际交付、兼容性、
local tests、资源证据、review findings、known limitations、follow-ups、
`Local evidence only; no online CI dependency`）。
