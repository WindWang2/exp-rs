# `/goal` 模板 · 长跑 epic 指令规范

本文件是 `/goal` 提示词的唯一事实来源。新 track 从这里复制骨架，不从历史 `GOAL.md` 考古。
词汇边界与命令选择判据见 `command-vocabulary.md`；常驻循环见 `loop-template.md`。
每条规则的依据标注为 D-xxx（缺陷）或 M-xx（矩阵约定），见 `review/PROMPT_DEFECTS.md` 与 `review/GOAL_MATRIX.csv`。

---

## 何时用 `/goal`

判据只有一句：**这件事做完之后还需要再做一次吗？** 不需要 → `/goal`；需要 → `/loop`。
完整判据图（含 wayfinder、grilling 分支）见 `command-vocabulary.md`，此处不重复。

`/goal` 的终态是一个 PR。有效期到 PR 合入为止；合入后 track 结束，不再有下一轮。

---

## 参数头（每个 `/goal` 的第一段，逐字保留）

```
/goal  target-agent=zcode  budget=300000000  subagents<=2  ci=none  autonomy=full  defaults=best
```

| 参数 | 含义 | 默认 |
| --- | --- | --- |
| `target-agent` | 执行代理 | `zcode` |
| `budget` | token 规划包线，超 1.5× 必须上报，不是静默硬顶 | `300000000` |
| `subagents<=2` | 子代理数量硬上限；全部只读 | `2` |
| `ci` | 依赖的远端 CI | `none`（不触发、不等待、不引用） |
| `autonomy` | 是否允许停下来提问 | `full`（禁止提问） |
| `defaults` | 有选择时怎么办 | `best`（自选最优并记入 DECISIONS.md） |

按需追加的修饰参数（写在实际作用域行，不写进参数头）：

```
mode=review-only  write-access=<目录清单>  src-access=read-only
```

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
做完之后什么变了。数字断言必须先跑验证命令再写进本文；验证命令写进 EVIDENCE.md。>

## Operating envelope (non-negotiable)

- **Autonomy**: fully unattended. No clarifying questions, no option menus. Take the
  default in Autonomy defaults; record every taken decision in
  `.planning/<slug>/DECISIONS.md`.
- **Agent**: `zcode`. **Token budget: 300,000,000**（allocation below）。Report — never
  silently stall — if a phase exceeds 1.5× its budget.
- **Subagents: at most 2**, both read-only（<子代理 A 的唯一职责>；<子代理 B 的唯一职责>）。
  Main agent owns 100% of implementation and judgment. Subagents spawn no further subagents.
- **No CI**: never wait on, trigger, or cite GitHub Actions. Local evidence only →
  `.planning/<slug>/EVIDENCE.md`. Every capability claim maps to a local command + exit
  code, or is explicitly marked not-executed.
- **Branching**: `master` is read-only. Create worktree + branch before the first edit.
- **Build resources (hard)**: `CMAKE_BUILD_PARALLEL_LEVEL=2`, `CTEST_PARALLEL_LEVEL=1`;
  Ninja `-j2`, drop to `-j1` when RSS > 70% or load > 1.5× cores; `-j$(nproc)` is
  forbidden. Log CPU/RSS every 60 s during builds.
- **Tests**: `QT_QPA_PLATFORM=offscreen`; targeted `ctest -R <family> -j1` before any
  broad run.
- **Exit**: PR created, not merged; worktree retained until merge, then removed.

## Skills (load proactively)

| Skill | Use it for | Phase |
| --- | --- | --- |
| `.agents/skills/<name>` | <一句话用途> | <阶段号> |

写入本表前逐行执行 `ls .agents/skills/<name>/`；不存在的技能不得出现在本表或正文。
冲突裁决：本表与技能自身的触发条件冲突时，autonomy=full 下技能的"向用户提问"步骤
由 Autonomy defaults 段的预设答案替代；无预设答案的决策记入 DECISIONS.md 后按默认继续。

## Autonomy defaults (do not ask — apply these)

1. <决策点 1 → 预设答案>
2. <决策点 2 → 预设答案>
3. <资源与超时 → 上限值与超时后的动作>
4. <对外动作（push/issue/邮件）→ 允许清单>
5. <范围外发现 → 只记录不处理，落点文件>

（每条可判定真假；这里列出所有"不预设就会停下来问用户"的分叉。）

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

（总和必须等于 budget。分配范式见本文件"九阶段预算范式"。）

## Required planning files

`.planning/<slug>/`：`GOAL.md`（本文逐字存档）· `PLAN.md` · `BASELINE.md` ·
`DECISIONS.md` · `EVIDENCE.md` · `REVIEW_LOG.md` · `PR_BODY.md`。
按需追加：`MILESTONES.md`、`TEST_MATRIX.md`、`CAPABILITY_MATRIX.md`、`OWNERSHIP.md`。

## Completion gate

1. <每条可被第三方核查：动词 + 对象 + 可执行的验证方式。>
2. <验收门内禁止出现质量形容词与程度副词。>

## PR runbook (execute verbatim)

0. 向 `.gitignore` 追加本 track 白名单（`.planning/<slug>/` 目录 + `*.md` 再白名单，
   照 `.planning/lab-spec-data-driven/` 条目的三行模式），然后执行
   `git check-ignore -v .planning/<slug>/GOAL.md` 确认输出为空；非空则停止并修复。
1. `git worktree add ../exp-rs-<slug> -b zcode/<slug> origin/master`
2. 每个 Phase 完成后 commit 一次；`git add -A` 后执行
   `git status --porcelain | grep -c "^A"` 核对新增文件数与预期一致。
3. 每个 checkpoint 执行 `git fetch origin && git rebase origin/master`；冲突时加载
   `.agents/skills/resolving-merge-conflicts/SKILL.md`。
4. 本地验证按 Build resources 硬约束执行；输出进 EVIDENCE.md。
5. 最后提交前执行存在性断言（见 goal-template.md"存在性断言"一节），结果粘进 EVIDENCE.md。
6. `git push -u origin zcode/<slug>` — 若 guardrail hook 拦截（见
   `.agents/skills/git-guardrails-claude-code/SKILL.md`）：原样重试一次，把拦截记录写进
   PR_BODY.md 后继续；force push 在任何情况下禁止。
7. `gh pr create --base master --head zcode/<slug> --title "<type>(<scope>): <summary>"
   --body-file .planning/<slug>/PR_BODY.md`
8. Do not merge. Do not wait for checks. Report the PR URL and stop.
```

---

## 填写规则（逐段，违反任何一条即打回）

### 命名（M-10，D-008）

- `<slug>` 与 `.planning/<slug>/` 目录名逐字符一致。
- 分支 = `zcode/<slug>`；worktree = `../exp-rs-<slug>`。
- 三处名字（目录、分支、worktree）互为派生，禁止第三种拼法。

### Mission 与证据（M-8，D-004/D-025）

- 每条现状断言附 `文件:行号` 或验证命令；无证据的断言删除或标 `UNDETERMINED` 并列入待澄清。
- 验收判据全部自带全文；`§N of the goal`、`from the goal brief`、`see the goal definition` 这类外引禁止出现——brief 全文写进本文或 `.planning/<slug>/`。

### 预算与阶段（M-11，D-009）

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
- 阶段超 1.5× 预算：停下来写报告（已花了什么、为什么、剩余量），然后继续——不静默、不提问。

### 证据政策（M-15，D-024）

最终报告里每一条能力断言映射到一条本地命令 + 退出码，或显式标注 `not-executed`。两者之外的表述方式（"验证过""没问题"）不出现。

### 技能引用（M-4，D-001/D-019）

- 只引用 `ls .agents/skills/<name>/` 验证存在的技能，引用格式带全路径 `SKILL.md`。
- 以下技能在当前仓库不存在，任何文档不得引用：`karpathy-guidelines`、`planningwithfiles`、`gstack`（后两者仅存在于用户级运行时，见 review/SKILL_INVENTORY.md）。

### 措辞（D-015）

禁用词类：程度副词、条件副词与质量形容词——凡真值依赖读者主观判断的修饰词。
每条指令写成"动词 + 对象 + 可判定完成条件"。检测命令（允许命中检查命令自身所在行，
其余行命中数必须为 0）：

```bash
grep -E "尽量|适当|必要时|合理|充分|酌情" <被检文本> | grep -vc "grep -E"   # 期望 0
```

### 存在性断言（runbook 第 5 条的实现）

```bash
grep -oE '\.agents/skills/[a-z-]+/SKILL\.md' <三份模板与本 GOAL.md> | sort -u | while read p; do test -e "$p" || echo "MISSING: $p"; done
grep -oE '`\.planning/[a-z0-9-]+/[A-Z_]+\.md`|`docs/agents/[a-z-]+\.md`' <本 GOAL.md> | tr -d '\`' | sort -u | while read p; do test -e "$p" || echo "MISSING: $p"; done
```

两条命令输出为空才算通过；结果（含空输出声明）粘进 EVIDENCE.md。

---

## 硬性自查清单（提交 `/goal` 前逐条执行）

- [ ] 第一段是参数头，与上方格式逐字符一致。
- [ ] `<slug>` 三处（目录/分支/worktree）逐字符一致。
- [ ] Write scope 与 Read-only 都填了；排除项注明归属。
- [ ] Mission 每条断言有 `文件:行号` 或验证命令；数字断言的验证命令已进 EVIDENCE.md。
- [ ] 子代理 ≤2、只读、各自唯一职责、禁止再派生——四要素齐全。
- [ ] Build resources 三件套齐全：`-j2` + RSS/load 降级规则 + 禁 `-j$(nproc)`。
- [ ] Skills 表每行都 `ls` 验证过；表外正文零技能引用。
- [ ] Autonomy defaults 覆盖全部"不预设就会问"的分叉。
- [ ] 九阶段预算总和 == `budget`。
- [ ] Completion gate 每条可第三方核查；零质量形容词。
- [ ] PR runbook 含第 0 步白名单自检、护栏拦截处理、"do not merge / do not wait"。
- [ ] 全文不可判定词命中数 = 0（排除检查命令自身行后；命令见"措辞"一节）。

---

## 与相邻命令的关系

| 场景 | 用哪个 |
| --- | --- |
| 一次性、有终点、终态是 PR | `/goal`（本文件） |
| 周期性、无终点、每轮产出同样的东西 | `/loop` → `loop-template.md` |
| 路还没找到，需要先映射决策票 | `wayfinder`（`.agents/skills/wayfinder/SKILL.md`） |
| 想法要磨清楚（树式追问） | `grilling`（`.agents/skills/grilling/SKILL.md`） |

判据图与三者边界见 `command-vocabulary.md`。
