# 命令词汇表 · `/goal` · `/loop` · `wayfinder` · `grilling`

本文件是这四个（一组）命令**边界与选择判据**的唯一事实来源。
内部结构各自定义于：`goal-template.md`（/goal）、`loop-template.md`（/loop）、
`.agents/skills/wayfinder/SKILL.md`（wayfinder）、`.agents/skills/grilling/SKILL.md`（grilling）。
本文件只回答"什么时候用哪个"，不重复定义任何一者的内部结构。

---

## 一张判据图

```
这件事做完之后，还需要再做一次吗？
│
├─ 需要 ──► 多久做一次 / 做什么，已经能说清吗？
│           ├─ 能   ──►  /loop        （loop-template.md）
│           └─ 不能 ──►  grilling     （磨清楚后回到本题）
│
└─ 不需要 ─► 通往终点的路，现在看得见吗？
             ├─ 看得见 ──►  /goal        （goal-template.md）
             └─ 看不见 ──►  wayfinder    （把路映射成决策票后，逐票回到本题）
```

判据图的根节点只有一句：**这件事做完之后，还需要再做一次吗？**

---

## 四者对照

| 维度 | `/goal` | `/loop` | `wayfinder` | `grilling` |
| --- | --- | --- | --- | --- |
| 目的 | 抵达终点 | 维持循环 | 找到路 | 磨清想法 |
| 生命周期 | 一次性 | 常驻 | 一次性（产出地图） | 会话内 |
| 终态 | 一个 PR | 每轮同样的产出 | 一张决策票地图 | 共识达成 |
| 触发 | 人发起 | Trigger（事件优先） | 人发起 | 人发起 |
| 人工介入 | 无（autonomy=full） | Checkpoint（push right） | 逐票决策 | **全程问答** |
| 版本控制 | 分支 `zcode/<slug>` + PR | 默认零提交 | 通常不开分支 | 不适用 |
| 状态 | 无状态 | 有状态（跨轮游标） | 有状态（票进度） | 会话内 |
| 产出落点 | 代码/文档 + PR | State home + 每轮 Brief | issue tracker 票 | 规格 / ADR |

### autonomy 冲突裁决（/goal × grilling）

grilling 的纪律是向用户提问；`/goal` 的 `autonomy=full` 禁止提问。裁决：
**/goal 内不得运行 grilling 式问答。** /goal 用 Autonomy defaults 段把"会引发提问的
决策点"逐条预答（`goal-template.md`）；预答覆盖不到的分叉记入 DECISIONS.md 按默认
继续。grilling 保留给 /goal 启动前（磨 brief）与 /loop 规格化前（loop-me 用法）。

---

## 边界裁决（实例 → 归类 → 判据）

### 真实历史 track（归类自其已落盘的 GOAL.md 与终态）

| 实例 | 归类 | 判据依据 |
| --- | --- | --- |
| whole-repo-line-review（全库逐行审查，终态 PR #957 已合入） | `/goal` | 一次性穷尽；做完即止（`.planning/whole-repo-line-review/GOAL.md:1-10`） |
| algorithm-foundation-5（算法基座升 5.0，里程碑 A–L → PR） | `/goal` | 一次性平台升级（`.planning/algorithm-foundation-5/GOAL.md:39-62`） |
| unified-help-diagnostics-6（建统一 help 知识层） | `/goal` | 建成即止，无常驻职责（`.planning/unified-help-diagnostics-6/GOAL.md:1-7`） |
| R 系列_issue_triage（每个 open issue 在最新 master 重新定位一遍，如 `.planning/geospatial-data-fabric-9/ISSUE_TRIAGE.md:2-4`） | `/goal`（单次执行时） | 单次 triage 有终点 |

### 衍生实例（同一素材固化为循环后的正确归类；非历史，标注 derived）

| 实例 | 归类 | 判据依据 |
| --- | --- | --- |
| "master 每前进 N 个 merge 就重跑一遍全库线审查" | `/loop`（derived） | 同一件事反复发生；R0 是其中一轮 |
| "每有新算子注册进 RSOperatorRegistry 就补 capability 元数据" | `/loop`（derived） | 增量反复发生；单次补齐版（/goal）做完后新算子会再产生缺口 |
| "每周把上轮审查发现转成 issue 草稿" | `/loop`（derived） | 配套循环；审查若循环化，转票同步循环化 |
| "还不知道下一个 track 该做什么" | `wayfinder` | 路未见，先映射决策票 |
| "知道要做某平台 10.0，但工作包和预算没定" | `/goal` + 完整 Autonomy defaults | 终点清楚、路径待细化；确实定不下的关键分叉先 grilling |

### 易混淆裁决

**"每天跑一次全库审查"这类周期性大活**：是 `/loop`，不是 `/goal`——即使单轮工作量
像 epic。判据句：单次执行 = `/goal`；固化重跑 = `/loop`。
反例警惕：把周期任务写成 `/goal` 会得到一条"每轮从头做一遍、以 PR 收尾"的昂贵伪循环。

---

## 组合用法

### A. 探路 → 长跑

```
wayfinder ──► 路清楚后，逐票产出自包含的 /goal
```

适用：一次要推进十几个 track 的大工程。wayfinder 把决策票摊开到 issue tracker，
每张票解决后就能写出一个自包含 `/goal`（其 GOAL.md 满足 goal-template 全部自查项）。

### B. 磨清 → 常驻

```
grilling（经 loop-me） ──► /loop
```

loop-me 的用法：以 grilling 纪律把复现模式磨成 workflow 规格，规格完成标准 =
"实现者无需提问即可执行"（`.agents/skills/loop-me/SKILL.md:27`），然后按
loop-template 落成常驻循环。

### C. 长跑 → 常驻（最常见的演化）

`/goal` 完成后，若其 Completion gate 里出现 ≥3 条"每次都要重新核查"的项，这些项
已在描述一条循环——为它们另起 `/loop`，原 `/goal` 保持一次性终态不变。

---

## 共享术语

| 术语 | 本仓库含义 | 首次落盘 |
| --- | --- | --- |
| Track | 一个 `/goal` 实例；一分支一 worktree 一 `.planning/<slug>/` | `.planning/*` 既有用法 |
| Slug | track 短横线标识；目录/分支/worktree 三处同名 | goal-template.md（D-008 裁决） |
| Trigger | 循环起火点；事件优先于排程 | `.agents/skills/loop-me/SKILL.md:20` |
| Checkpoint | 循环中唯一人工闸口 | loop-me:21 |
| Push right | 把 Checkpoint 推到最后一站，一次问清 | loop-me:22 |
| Brief | Checkpoint 呈现的决策材料，非原始产出 | loop-me:23 |
| Cursor | 循环跨轮进度游标（`last_cursor`） | loop-template.md |
| Suppressed | 已知且决定不再重复报告的项 | loop-template.md |
| Completion gate | 可第三方核查的完成判据集 | `.planning/*` 既有用法 |
| Autonomy defaults | 预答所有"会引发提问"的决策点 | lab-spec-data-driven/GOAL.md:23 |
| Evidence policy | 每条断言 → 本地命令 + 退出码，或标 not-executed | verification-platform-8/GOAL.md:24-25 |

### 严重度词汇 P0–P3（源自 `.planning/whole-repo-line-review/GOAL.md:9`，此处收录为共享词表）

| 级别 | 定义 |
| --- | --- |
| P0 | 崩溃 / 内存破坏 / 科学结论错误 / 数据静默损坏 / 凭据泄漏 |
| P1 | 未处理边界 / 竞态 / 泄漏 / 契约违背 |
| P2 | 性能 / 非确定性 / 错误类型不一致 / 缺失防御 |
| P3 | 代码债 / 文档脱节 |

用法约定：`/goal` 的 Completion gate 写 `P0/P1 = 0`；对抗审查处置写
`P2/P3 fixed or justified`（先例 `.planning/cartography-platform-8/GOAL.md:50`）。
P0–P3 定义若需修订，改本文件这一节，不改历史 GOAL.md。

---

## 禁止事项

1. 不给 `/loop` 写 PR 收尾——循环没有终态。
2. 不给 `/goal` 写 Trigger——它由人发起。
3. 不用 `/goal` 表达周期性任务——那会得到一条每轮重头做、以 PR 收尾的昂贵伪循环。
4. 不在 `/loop` 里全量重扫——必须用 `last_cursor` 增量。
5. 不在命令模板里引用不存在的技能或文件——断链引用是本仓库已发生的真实缺陷
   （`review/PROMPT_DEFECTS.md` D-001/D-002/D-012）。
6. 不在 `/goal` 内运行 grilling 问答——见上文 autonomy 冲突裁决。
