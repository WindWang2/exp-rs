# `/goal` 模板 · 长跑 epic 指令规范

本文件是 `/goal` 提示词的唯一事实来源。新 track 从这里复制骨架，不从历史 `GOAL.md` 考古。
词汇边界与命令选择判据见 `docs/agents/command-vocabulary.md`；常驻循环见 `docs/agents/loop-template.md`。
每条规则的依据标注为 D-xxx，见 `review/PROMPT_DEFECTS.md`；逐 track 证据见 `review/GOAL_MATRIX.csv`。

---

## 何时用 `/goal`

判据只有一句：**这件事做完之后还需要再做一次吗？** 不需要 → `/goal`；需要 → `/loop`。
完整判据图（含 wayfinder、grilling 分支）见 `docs/agents/command-vocabulary.md`，此处不重复。

`/goal` 的终态是一个 PR。有效期到 PR 合入为止；合入后 track 结束。PR 收到 reviewer 修改
请求时，按本文 PR runbook 第 9 步在同一 track 内续跑，不新开 `/goal`。

---

## 参数头（每个 `/goal` 的第一段；格式逐字保留，数值参数按 track 实际值填写）

```
/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best
```

| 参数 | 含义 | 默认 |
| --- | --- | --- |
| `target-agent` | 执行代理 | `zcode` |
| `budget` | token 规划包线；计量方式与耗尽动作见"预算与阶段" | `300000000` |
| `subagents<=2` | 子代理数量硬上限；全部只读 | `2` |
| `ci` | 依赖的远端 CI | `none`（不触发、不等待、不引用） |
| `autonomy` | 是否允许停下来提问 | `full`（禁止提问） |
| `defaults` | 有选择时怎么办 | `best`（自选最优并记入 DECISIONS.md） |

按需追加的修饰参数（写在实际作用域行，不写进参数头）：

```
mode=review-only  write-access=<目录清单>  src-access=read-only
```

**与运行时准则的优先级**：本 GOAL 与 `.agents/AGENTS.md` / `CLAUDE.md` 冲突时，以本 GOAL
为准。AGENTS.md §1 的"呈现选项/澄清"义务在 `autonomy=full` 下由 Autonomy defaults 段
履行（写入 DECISIONS.md），不向用户提问。

---

## 模板骨架（复制以下整块，填占位符；段落一个不删）

```markdown
# GOAL — <ID> · <Track Name>（<一句话定位>）

/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Track branch:** `zcode/<slug>` (worktree `../exp-rs-<slug>`, off `origin/master` @ `<sha>`)
> **Mode:** unattended long-running epic. Local build/test evidence only — never block on,
> trigger, or cite online CI.
> **Write scope:** <本 track 独占可写的目录清单>
> **Read-only:** <明确排除的目录；每一项注明归属 track 或权威来源>

## Mission

<≤3 段。每段回答：现状是什么（每条断言附 `文件:行号` 或验证命令）→ 为什么这是真问题 →
做完之后什么变了。数字断言必须先跑验证命令再写进本文；验证命令与输出写进 EVIDENCE.md。>

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions, no option menus. Take the
  default in Autonomy defaults; record every taken decision in
  `.planning/<slug>/DECISIONS.md`.
- **Precedence**: this GOAL overrides `.agents/AGENTS.md` / `CLAUDE.md` on conflict.
  AGENTS.md §1's "surface options / clarify" duty is discharged by writing options and
  the taken default into DECISIONS.md.
- **Agent**: `zcode`. **Token budget: 300,000,000**（allocation below）。When a phase
  exceeds 1.5× its allocation: append a budget line (phase, clock time, commands run,
  files touched) to EVIDENCE.md and continue — never stall silently, never ask.
- **Subagents: at most 2**, both read-only（<子代理 A 的唯一职责>；<子代理 B 的唯一职责>；
  惯分配：#1 = Phase 0 基线/研究审计，#2 = 最终对抗审查）。
  Main agent owns 100% of implementation and judgment. Subagents spawn no further subagents.
  A subagent that fails or returns nothing usable: the main agent performs that review
  inline, records the failure in EVIDENCE.md, and does not spend an extra slot.
- **No CI**: never wait on, trigger, or cite GitHub Actions. Local evidence only →
  `.planning/<slug>/EVIDENCE.md`. Every capability claim maps to a local command + exit
  code, or is explicitly marked not-executed.
- **Branching**: `master` is read-only. Create worktree + branch before the first edit.
- **Build entry**: configure via `CMakePresets.json` presets (development preset
  `build-dev`); record the configure exit code once in EVIDENCE.md. `build.cmd` /
  `configure_*.cmd` at repo root carry stale absolute paths from past tracks — do not
  follow them (defect D-028).
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`;
  Ninja `-j2`, drop to `-j1` when RSS > 70% or load > 1.5× cores; `-j$(nproc)` is
  forbidden. Measure RSS with the host OS tool (`tasklist` on Windows, `ps` on Linux);
  load average via `uptime` (Linux). On a host where a trigger is not measurable
  (e.g. load average under Git Bash), record that fact once in EVIDENCE.md as
  not-executed and run `-j2` unconditionally. Log CPU/RSS every 60 s during builds.
- **Tests**: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` before any
  broad run.
- **Exit**: PR created, not merged; worktree retained until merge, then removed.

## Skills (load proactively)

| Skill | Use it for | Phase |
| --- | --- | --- |
| `.agents/skills/<name>/SKILL.md` | <一句话用途> | <阶段号> |

写入本表前逐行执行 `ls .agents/skills/<name>/`；不存在的技能不得出现在本表或正文。
引用格式一律带 `SKILL.md` 全路径。
冲突裁决：autonomy=full 下技能的"向用户提问"步骤由 Autonomy defaults 段的预设答案
替代；`resolving-merge-conflicts` 等技能自带的验证步骤按本 GOAL 的 Tests 行执行
（targeted `ctest -R <family> -j1`），不做全量套件。

## Autonomy defaults (do not ask — apply these)

以下 7 类为必答清单，每类至少一条；类内条目按 track 追加：

1. **格式/来源**：<数据格式、schema 来源、既有权威选谁>
2. **失败项处置**：<单个条目失败时跳过、重试还是标记>
3. **命名/编号**：<新文件、新符号、新编号的规则与冲突处理>
4. **资源与超时**：<单命令/单阶段超时上限；超时后的动作>
5. **对外动作**：<允许清单，默认只读；对 origin 的 `git fetch` 属只读、允许>
6. **范围外发现**：记入 `.planning/<slug>/EVIDENCE.md` 的 `OUT_OF_SCOPE` 节；
   P0 级另在 PR_BODY.md 顶部以 `P0 (out of scope)` 标注；不在本 track 修复。
7. **依赖新增**：<需要新依赖/新工具时的默认动作（默认：不用，找既有等价物）>

## Current state & evidence (verified)

| Fact | Evidence |
| --- | --- |
| <已核实事实> | `<文件:行号>` 或 `<验证命令> → <输出摘要>` |

## Work packages

| ID | Package | Key deliverables |
| --- | --- | --- |
| A | <包名> | <可核查交付物> |

## Execution order & token budget (300,000,000 total)

| Phase | Content | Budget (M tokens) |
| --- | --- | --- |
| 0 | <内容> | <数字> |
| … | … | … |

（总和必须等于 budget。分配范式与裁剪规则见本文件"预算与阶段"一节。）

## Required planning files

`.planning/<slug>/`（在 worktree 内创建）：`GOAL.md`（本文逐字存档）· `PLAN.md` ·
`BASELINE.md` · `DECISIONS.md` · `EVIDENCE.md` · `REVIEW_LOG.md` · `PR_BODY.md`。
按需追加：`MILESTONES.md`、`TEST_MATRIX.md`、`CAPABILITY_MATRIX.md`、`OWNERSHIP.md`。

## Completion gate

1. <每条附验证方式：`验证命令 → 期望输出`，与证据政策同构；无验证命令的条目删除。>
2. …

## PR runbook (execute verbatim)

1. `git worktree add ../exp-rs-<slug> -b zcode/<slug> origin/master`；此后全部工作
   （含本 GOAL.md 的落盘）只发生在 worktree 内。
2. 在 worktree 内向 `.gitignore` 追加本 track 白名单（`.planning/<slug>/` 目录 +
   `*.md` 再白名单，照 `.planning/lab-spec-data-driven/` 条目的三行模式），执行
   `git check-ignore -v .planning/<slug>/GOAL.md`，确认无输出（exit 1）后，把
   `.gitignore` 与 `.planning/<slug>/GOAL.md` 一并作为首次 commit。
3. 每个 Phase 完成后 commit 一次；把 `git status --porcelain` 的完整输出粘进
   EVIDENCE.md 对应 Phase 小节。
4. 每个 Phase commit 后执行 `git fetch origin && git rebase origin/master`；冲突时
   加载 `.agents/skills/resolving-merge-conflicts/SKILL.md`，其验证步骤按本 GOAL 的
   Tests 行执行。
5. 本地验证按 Build resources 硬约束执行；输出进 EVIDENCE.md。
6. 最后提交前执行存在性断言（见"存在性断言"一节），结果粘进 EVIDENCE.md。
7. `git push -u origin zcode/<slug>`。失败时：stderr 原文记入 EVIDENCE.md；若错误含
   hook/BLOCKED 字样，原样重试一次；仍失败则跳到第 10 步写收尾报告并停止
   （PR 未建成即 track 的如实终态）。`git-guardrails-claude-code` 技能仅描述 hook
   机制——禁止在本 track 内执行它的任何安装步骤。force push 在任何情况下禁止。
8. `gh pr create --base master --head zcode/<slug> --title "<type>(<scope>): <summary>"
   --body-file .planning/<slug>/PR_BODY.md`。Do not merge. Do not wait for checks.
   Report the PR URL and stop.
9. 收到 reviewer 修改请求：视为同一 track 的续跑——逐条回复、修改、commit、push，
   重跑第 6 步断言；仍 do not merge。预算从原 track 余量扣除；超 1.5× 按预算行上报。
10. 任何一步无法完成（push 被拦且重试无效、rebase 无法收敛、gh 不可用）：在
    EVIDENCE.md 写收尾报告（已完成工作包、失败步骤原文、退出码），向用户报告后停止。
```

---

## 填写规则（逐段，违反任何一条即打回）

### 命名（D-008）

- `<slug>` 与 `.planning/<slug>/` 目录名逐字符一致。
- 分支 = `zcode/<slug>`；worktree = `../exp-rs-<slug>`。
- 三处名字（目录、分支、worktree）互为派生，禁止第三种拼法。
- 时代注记：平台 6.0–9.0 代历史 track 用 `feat/` 前缀；3/4/5 代与全部最新 D/R 系列用
  `zcode/`。新 track 一律 `zcode/`，不继承 `feat/`。

### Mission 与证据（D-004/D-025）

- 每条现状断言附 `文件:行号` 或验证命令；无证据的断言删除或标 `UNDETERMINED` 并列入待澄清。
- 验收判据全部自带全文；`§N of the goal`、`from the goal brief`、`see the goal definition` 这类外引禁止出现——brief 全文写进本文或 `.planning/<slug>/`。
- 若 brief 历史上以本地文件存在（如 `prompts/NN_*.md`），把其全文并入本 GOAL.md，不引用路径。

### 预算与阶段（D-009）

- 九阶段默认分配（总和 = `budget`）：

| Phase | 内容定位 | 占比 | 默认 M（300M 时） |
| --- | --- | ---: | ---: |
| 0 | 基线审计 + 能力矩阵 + GOAL 落盘 | 6% | 18 |
| 1 | 第一工作包（最基础的契约/接线） | 16% | 48 |
| 2 | 第二工作包（体量最大的一块） | 18% | 54 |
| 3 | 第三工作包 | 15% | 46 |
| 4 | 第四工作包（收口） | 13% | 38 |
| 5 | 测试 + 文档 + 认证 | 11% | 34 |
| 6 | 交叉复核（≤2 只读子代理） | 8% | 24 |
| 7 | 复核发现修复 | 7% | 20 |
| 8 | Rebase + docs 同步 + PR | 6% | 18 |

- 裁剪规则：工作包不足 4 个时合并 Phase 1–4 重算，Phase 0/6/7/8 的占比不变；改完重算使总和 = `budget`。
- 计量方式：会话内无 token 计数接口；以可测量代理指标计——每 Phase 结束时把
  `时间戳 / 工具调用次数 / 触及文件数` 记入 EVIDENCE.md 预算节，对照阶段包线判断超支。
- 阶段超 1.5× 包线：按 envelope 预算行上报后继续。
- **总预算耗尽而 Completion gate 未达成**：停止开新工作，按已完成工作包走 runbook
  第 8 步出 PR，PR_BODY.md 顶部声明未完成项清单；不带半成品冒充完成。

### 证据政策（D-024）

最终报告里每一条能力断言映射到一条本地命令 + 退出码，或显式标注 `not-executed`。两者之外的表述方式（"验证过""没问题"）不出现。

### 技能引用（D-001/D-019）

- 只引用 `ls .agents/skills/<name>/` 验证存在的技能，引用格式带全路径 `SKILL.md`。
- 以下技能在当前仓库不存在，任何文档不得引用：`karpathy-guidelines`、`planningwithfiles`、`gstack`（后两者仅存在于用户级运行时，见 `review/SKILL_INVENTORY.md`）。

### 措辞（D-015）

禁用词定义为下方检测命令枚举的**封闭清单**——清单即规则的全部，不外推到其他修饰词；
清单外的措辞问题归人工评审，不进自动断言。每条指令写成"动词 + 对象 + 可判定完成条件"。

```bash
grep -E "尽量|适当|必要时|合理|充分|酌情" <被检文本> | grep -vc "grep -E"   # 期望 0
```

### 存在性断言（runbook 第 6 步的实现）

```bash
for f in <本 GOAL.md> docs/agents/goal-template.md docs/agents/loop-template.md docs/agents/command-vocabulary.md .agents/AGENTS.md CLAUDE.md; do
  grep -ohE '\.agents/skills/[a-z-]+/SKILL\.md' "$f"
done | tr -d '\r' | sort -u | while read p; do test -e "$p" || echo "MISSING: $p"; done

grep -ohE '`docs/agents/[a-z-]+\.md`|`review/[A-Z_]+\.(md|csv)`|`\.planning/[a-z0-9-]+/[A-Z_]+\.md`' \
  <本 GOAL.md> docs/agents/*.md .agents/AGENTS.md CLAUDE.md \
  | tr -d '\r`' | sort -u | while read p; do test -e "$p" || echo "MISSING: $p"; done
```

两条命令输出为空才算通过（`tr -d '\r'` 必须保留——Windows 工作副本是 CRLF）；
结果（含空输出声明）粘进 EVIDENCE.md。

---

## 硬性自查清单（提交 `/goal` 前逐条执行）

- [ ] 第一段是参数头，格式逐字符一致；数值参数已按实际值填写。
- [ ] `<slug>` 三处（目录/分支/worktree）逐字符一致。
- [ ] Write scope 与 Read-only 都填了；排除项注明归属。
- [ ] Mission 每条断言有 `文件:行号` 或验证命令；数字断言的验证命令已进 EVIDENCE.md。
- [ ] 子代理 ≤2、只读、各自唯一职责、禁止再派生——四要素齐全。
- [ ] Build resources 三件套齐全：`-j2` + RSS/load 降级规则 + 禁 `-j$(nproc)`；构建入口指向 CMakePresets。
- [ ] Skills 表每行都 `ls` 验证过；表外正文零技能引用。
- [ ] Autonomy defaults 七类必答齐全。
- [ ] 九阶段预算总和 == `budget`；预算计量方式与总耗尽动作已写入。
- [ ] Completion gate 每条附"验证命令 → 期望输出"。
- [ ] PR runbook 含白名单自检、护栏拦截处理、PR 被拒续跑、失败收尾四件。
- [ ] 全文不可判定词命中数 = 0（排除检查命令自身行后；命令见"措辞"一节）。

---

## 与相邻命令的关系

| 场景 | 用哪个 |
| --- | --- |
| 一次性、有终点、终态是 PR | `/goal`（本文件） |
| 周期性、无终点、每轮产出同样的东西 | `/loop` → `docs/agents/loop-template.md` |
| 路还没找到，需要先映射决策票 | `wayfinder`（`.agents/skills/wayfinder/SKILL.md`） |
| 想法要磨清楚（树式追问） | `grilling`（`.agents/skills/grilling/SKILL.md`） |

判据图与三者边界见 `docs/agents/command-vocabulary.md`。
