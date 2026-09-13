# GOAL — R2 · Post-D-Landing Deep Review（合并潮后全面深审 + issue 提交）

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/r2-deep-review` (worktree `../exp-rs-r2-deep-review`, off `origin/master` @ `7d78059d`)
> **Mode:** unattended long-running epic. Static deep review + local evidence; build toolchain
> unavailable in session (no cmake/ninja/MSVC env in PATH — probed 2026-09-13) → build/test
> claims marked not-executed per evidence policy.
> **Write scope:** `review/`、`.planning/r2-deep-review/`、`.gitignore`（本 track 白名单）
> **Read-only:** `src/`、`tests/`（发现只记录不修）；`docs/` 只读（改动走后续 fix track）

## Mission

2026-09-13 一天合并了 8 个 PR（#951–#958），其中 6 个是功能 PR，`src/app` 一次被 92 个文件
的机械扫描扫过（#953 i18n）。R0（#957）只覆盖 `src/operators/**` + `pi/**`，且其 GOAL 禁止
建 issue——7 条已去重发现（F-OPS-1..5、F-PI-1..2）从未提交到 GitHub。现状证据：

| Fact | Evidence |
| --- | --- |
| R0 Tier A 覆盖 = src/operators + pi（reviewed_at 2026-09-13） | `review/COVERAGE_LEDGER.csv:1-8` |
| R0 禁止建 issue，7 条发现只落 dossier | `.planning/whole-repo-line-review/GOAL.md:6`；`review/DEDUPE.md:18-24` |
| #953 触及 src/app 92 文件 | `gh pr view 953 --json files`（本 GOAL 起草时实测） |
| #954 自述"green baseline with offline degradation"（测试可信度红旗） | PR #954 标题 |
| issue 现状 0 open / 761 closed；`needs-triage` 等标签未创建 | `gh api search/issues` 实测；`gh label list` 实测 |

做完之后什么变了：新代码（#951–#956）经过六透镜审查；R0 未提交的发现经 HEAD 复验后成为
GitHub issue；全部发现带 文件:行号 + 逐字引用 + 严重度（P0–P3，`docs/agents/command-vocabulary.md`）。

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended; defaults recorded in `.planning/r2-deep-review/DECISIONS.md`.
- **Precedence**: 本 GOAL 与 `.agents/AGENTS.md`/`CLAUDE.md` 冲突时以本 GOAL 为准。
- **Agent**: `zcode`. **Token budget: 300,000,000**；超 1.5× 按 envelope 预算行上报。
- **Subagents: at most 2**, both read-only（A = 机械扫描区 #953/#951 审查；B = 领域数据区
  #956/#955/#952 审查）。Main agent owns 100% of verification and judgment；#954、R0 复验、
  横切检查、issue 提交全部由主代理执行。子代理失败 → 主线内联完成并记录。
- **No CI**: 不触发/不等待/不引用 GitHub Actions；本地证据 → `EVIDENCE.md`；构建标 not-executed。
- **Branching**: `master` 只读；worktree 先行。
- **Build entry**: 不可用（探测记录见 EVIDENCE.md E-001）；不做编译，`CTEST_PARALLEL_LEVEL=1`
  纪律仅在可执行时适用。
- **External actions**: `gh issue create` / `gh label create`（needs-triage 族，与
  `docs/agents/triage-labels.md` 对齐）已被用户明示授权（"提交问题"）；其余对外动作禁止
  （不 push 非本 track 分支、不关 issue、不合并 PR）。
- **Exit**: dossier PR created, not merged; issues filed; worktree retained until merge.

## Skills (load proactively)

| Skill | Use it for | Phase |
| --- | --- | --- |
| `.agents/skills/code-review/SKILL.md` | 审查方法学基准 | 2-3 |
| `.agents/skills/resolving-merge-conflicts/SKILL.md` | rebase 冲突时 | 6 |

## Autonomy defaults (do not ask — apply these)

1. **格式/来源**：issue 正文 = 结构化模板（Severity/Lens/Location/Quoted/Impact/Repro/建议修复）；
   严重度按 `docs/agents/command-vocabulary.md` P0–P3。
2. **失败项处置**：单条候选发现复验失败（HEAD 已改/引用失效）→ 撤下并记入 dossier 撤下表。
3. **命名/编号**：issue 标题 `<type>(<scope>): <一句话>`；发现编号 F2-<seq>。
4. **资源与超时**：单条 gh 命令超时 60 s；gh 连续 3 次失败 → 暂停提交、报告、停止。
5. **对外动作**：允许清单 = `gh issue create`、`gh label create`、本 track 的 `git push`、
   `gh pr create`；对 origin 的只读 fetch 允许。
6. **范围外发现**：记入 EVIDENCE.md `OUT_OF_SCOPE` 节；P0 级在 dossier 顶部标注。
7. **依赖新增**：不引入任何工具依赖。

## Current state & evidence (verified)

见上文 Mission 表。补充基线：去重材料 = `review/DEDUPE.md`（R0 已对照 250 条闭环 issue）+
`review/findings/{operators,pi}.md` + 250 条闭环 issue 文本。

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | 去重基线 + R0 发现 HEAD 复验 | F-OPS/F-PI 逐条复验结论 |
| B | #954 绿基线深审（主代理） | 测试可信度发现 |
| C | #953/#951 机械扫描区（子代理 A） | 候选发现 |
| D | #956/#955/#952 领域数据区（子代理 B） | 候选发现 |
| E | 横切漂移（ADR/白名单/CHANGELOG/docs） | 漂移发现 |
| F | 复验 + 去重 + issue 提交 | GitHub issues |
| G | dossier + PR | review/DEEP_REVIEW_R2.md + PR |

## Execution order & token budget (300,000,000 total)

| Phase | Content | Budget (M tokens) |
| --- | --- | ---: |
| 0 | Track + 去重基线（A 前半） | 18 |
| 1 | #954 深审 + R0 复验（A 后半、B） | 54 |
| 2 | 子代理 A/B 区域审查（C/D） | 54 |
| 3 | 横切漂移（E） | 30 |
| 4 | 全量复验 + 闭环 issue 搜索去重（F 前半） | 48 |
| 5 | issue 提交（F 后半） | 36 |
| 6 | dossier + 存在性断言 + PR（G） | 30 |
| 7 | rebase + PR 收尾（含 review 意见续跑） | 18 |
| 8 | 机动（复核争议条目） | 12 |
| 合计 | | 300 |

## Required planning files

`.planning/r2-deep-review/`：`GOAL.md` · `DECISIONS.md` · `EVIDENCE.md` · `REVIEW_LOG.md` ·
`PR_BODY.md`。dossier 主件落 `review/DEEP_REVIEW_R2.md`。

## Completion gate

1. 每条提交的 issue 附 `验证命令 → 期望输出`：`gh issue view <n> --json body` 含
   Location 行且 `test -e <被引文件>` 成立 → `grep -c "Location" body` ≥ 1。
2. R0 的 7 条发现每条有复验结论（仍成立 / 已修复 / 撤下），零跳过：
   `grep -c "F-" review/DEEP_REVIEW_R2.md` ≥ 7。
3. 全部 issue 带严重度（P0–P3）与 lens；`needs-triage` 或对应 severity 标签已应用。
4. 与 761 条闭环 issue 的去重：每条 issue body 含 `Dedup:` 行列出搜索过的关键词。
5. `src/`/`tests/` 零改动：`git diff --name-only origin/master...HEAD | grep -cE "^(src\|tests)/"` = 0。
6. dossier 含撤下表与假阳性率。

## PR runbook (execute verbatim)

1. `git worktree add ../exp-rs-r2-deep-review -b zcode/r2-deep-review origin/master`（已完成）
2. 在 worktree 内向 `.gitignore` 追加 `.planning/r2-deep-review/` 白名单三行；
   `git check-ignore -v .planning/r2-deep-review/GOAL.md` 确认无输出后与 GOAL.md 一并首次 commit。
3. 每 Phase 完成后 commit；`git status --porcelain` 输出粘进 EVIDENCE.md。
4. 每 Phase commit 后 `git fetch origin && git rebase origin/master`。
5. 最后提交前执行存在性断言（被引文件 test -e + SKILL.md 路径），结果粘进 EVIDENCE.md。
6. `git push -u origin zcode/r2-deep-review`；失败时 stderr 入 EVIDENCE.md，hook/BLOCKED 字样
   重试一次，仍失败写收尾报告并停止；force push 禁止。
7. `gh pr create --base master --head zcode/r2-deep-review --title "review: R2 post-D-landing deep review dossier"
   --body-file .planning/r2-deep-review/PR_BODY.md`。Do not merge.
8. 收到 review 意见 → 同 track 续跑；任何步骤失败 → EVIDENCE.md 收尾报告后停止。
