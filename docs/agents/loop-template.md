# `/loop` 模板 · 常驻循环指令规范

本文件是 `/loop` 提示词的唯一事实来源。骨架取自 `.agents/skills/loop-me/SKILL.md` 的
Trigger / Checkpoint / Push right / Brief 词汇，注入本仓库的硬约束（资源上限、无 CI、
技能加载、中断恢复）。

**证据等级声明**：`/goal` 在本仓库有 24 个实战样本（`review/GOAL_MATRIX.csv`）；`/loop`
没有——`.planning/`、`.agents/`、`docs/` 中零引用，loop-me 的产物目录 `workflows/` 也不存在
（`review/PROMPT_DEFECTS.md` D-016）。本模板是推导出的新约定：词汇来自 loop-me（已落盘
技能），硬约束来自 /goal 体系（已验证约定）。首个实战 track 跑完前，不把它当"事实标准"引用。

选择判据（/loop vs /goal vs wayfinder vs grilling）见 `docs/agents/command-vocabulary.md`，此处不重复。

---

## `/loop` 的定义

一条常驻循环替人执行一个可委派的复现模式。它的成功标准是**每一轮稳定产出同样的交付物**，
而不是"做完就结束"。循环没有终态；**循环不写 PR 收尾流程**（PR 是 `/goal` 的收尾动作）。

**本模板不提供常驻进程。** 一轮 = 一次会话；轮与轮之间由 Trigger 一节写明的再触发方式
（用户再次发起，或外部调度）衔接。agent 不自我复启。

与 `/goal` 的区别速览（完整对照表见 `docs/agents/command-vocabulary.md`）：

| 维度 | `/goal` | `/loop` |
| --- | --- | --- |
| 生命周期 | 一次，PR 合入即结束 | 常驻，反复运行 |
| 触发 | 人发起 | Trigger：事件优先于排程 |
| 人工介入 | 无（autonomy=full） | Checkpoint，推到最后一站（push right） |
| 状态 | 无状态 | 有状态：跨轮游标 |
| 版本控制 | 分支 + PR | 默认不开分支、不提交（见第 5 节） |

---

## 参数头（格式逐字保留，数值参数按循环实际值填写）

```
/loop  target-agent=zcode  trigger=<event|schedule>  checkpoint=<yes|no>  budget=<N>  ci=none  autonomy=steady
```

| 参数 | 含义 | 默认 |
| --- | --- | --- |
| `target-agent` | 执行代理 | `zcode` |
| `trigger` | 触发方式：**事件优先于排程** | `event` |
| `checkpoint` | 是否设人工闸口 | `yes`（确无人工决策点时 `no`） |
| `budget` | **单轮** token 包线（语义同 /goal 的 budget：按代理指标计量，超 1.5× 如实上报） | 按循环填 |
| `ci` | 依赖的远端 CI | `none` |
| `autonomy` | 循环内自主度 | `steady` = 允许唯一 Checkpoint 处提问，其余分叉按 Autonomy defaults；无 Checkpoint 时等同 full |

**与运行时准则的优先级**：本 LOOP 与 `.agents/AGENTS.md` / `CLAUDE.md` 冲突时，以本 LOOP
为准；AGENTS.md §1 的"澄清"义务由 Autonomy defaults 履行。

---

## 模板骨架（复制以下整块，填占位符）

```markdown
# LOOP — <ID> · <Loop Name>（<一句话定位>）

/loop  target-agent=zcode  trigger=<event|schedule>  checkpoint=<yes|no>  budget=<N>  ci=none  autonomy=steady

> **State home:** `.planning/loops/<slug>/`（`STATE.md` · `RUN_LOG.md`）
> **Trigger:** <事件描述；或排程表达式 + 选排程的理由>
> **Re-fire:** <每轮结束后由谁再次触发：用户手动 /loop <slug> / 外部调度命令名>
> **Checkpoint:** <有/无；有则写明位于流程哪一站>
> **Working tree:** <默认"主工作树直读，零提交"；写权限默认仅限 State home>

## Mission

<这条循环维护什么？它替人省掉哪个重复动作？每轮的交付物是什么？>

## Loop lens

1. **为什么是循环不是一次性任务？** <回答：产出会不会过期/漂移/新增；写明维持判断的事实依据。>
2. **内部还套着哪些更小的循环？** <可选。嵌套循环常能拆出逐件处理的实现。>

## Review vocabulary

<透镜清单（固定还是每轮可调，写明）+ 严重度分级引用
`docs/agents/command-vocabulary.md` 的 P0–P3 节。每轮发现一律带级别。>

## Trigger

- **类型**：event / schedule
- **触发条件**：<如"master 新合入 ≥1 个 merge commit" / "每周一 09:00 本地时间">
- **选型理由**：<事件触发默认更省资源；选 schedule 必须写出事件不可观测的具体原因>
- **空转出口**：<无新输入时本轮的直接结束动作；空轮不产出报告>
- **再触发方式**：<见头部 Re-fire 行；此处写细节>
- **感知手段**：<如何检测触发条件；对 origin 的只读 `git fetch` 允许>

## Checkpoint & push right

**原则：把人工介入推到流程最后一站；一次问清；带全套材料去问。**
checkpoint=no 时本节只保留失败停止条件，不设闸口。

| 环节 | 要人吗 | 说明 |
| --- | --- | --- |
| 采集 | 否 | <…> |
| 加工 | 否 | <…> |
| **闸口** | **是** | <唯一人工点；位置 = 流程最后一站> |
| 交付 | 否 | <…> |

**Brief 格式**（闸口呈现给人看的决策材料，非原始产出；checkpoint=no 时不产 Brief）：

    ## <循环名> · <轮次> · <日期>
    **结论**：<一句话：本轮发现了什么>
    **需要你决定**：<可执行选项列表，附推荐项>
    **依据**：<指向本轮产物的路径>
    **上一轮遗留**：<未闭环项>

原始输出、全量 diff、未消化的清单不进 Brief。

## State（跨轮记忆）

`.planning/loops/<slug>/STATE.md`，每轮结束前更新：

| 字段 | 说明 |
| --- | --- |
| `last_run` | 上轮时间 |
| `last_cursor` | 上轮处理到哪（commit SHA / 日期 / 文件游标）；本轮从这里续，禁止全量重扫（整库审查循环的豁免见反模式表注脚） |
| `open_items` | 未闭环项 |
| `suppressed` | 已知且决定不再报的项；第二次报告必须来自真实变化 |

## Autonomy defaults (do not ask — apply these)

1. **无新输入**：执行空转出口，本轮结束，零报告。
2. **重复发现**：已在 `suppressed` 的不再报；连续 2 轮出现的同一新项升级为 `open_items` 并进下轮 Brief。
3. **资源上限**：编译 `CMAKE_BUILD_PARALLEL_LEVEL=2`、Ninja `-j2`、RSS > 70% 或 load > 1.5× cores 时 `-j1`、`-j$(nproc)` 禁止；测试 `QT_QPA_PLATFORM=offscreen`、targeted `ctest -R <family> -j1`。
4. **写权限**：默认只读，写权限仅限 State home。要改任何被跟踪文件：停下，起一次性 `/goal` track 完成。
5. **对外动作**：不 push、不建 issue、不发邮件，除非本骨架显式授权具体动作；对 origin 的只读 `git fetch` 不属于对外动作。
6. **单轮超时**：<写明确分钟数；到点如实报告当前进度、写本轮子游标进 STATE.md，结束本轮>。
7. **单轮预算**：超 `budget` 的 1.5× 时在 RUN_LOG.md 记一行（时间戳、命令数、触及文件数）后继续；连续 2 轮超线 = 该循环需要拆分，记入 open_items 上报。

## Per-run gate（每轮都要过）

- [ ] `STATE.md` 已更新（`last_cursor` 前进，或写明为何未前进）
- [ ] 空转轮没有产出报告
- [ ] 所有结论带 `文件:行号` 或命令 + 退出码
- [ ] Brief 按模板生成，零原始产出（checkpoint=yes 时；checkpoint=no 时本条跳过）
- [ ] `suppressed` 项未被重复报告
- [ ] 未执行任何未授权的对外动作（只读 fetch 除外）

## Failure handling

某一轮失败（命令报错、资源超限、超时）：把失败原文与退出码写入 `RUN_LOG.md`，在
`STATE.md` 标记失败点并写子游标（长任务在轮内分段推进时，子游标记到分段边界），
下一轮从游标续跑。静默跳过失败步骤禁止——静默跳过会让循环看起来在跑，实际已经瞎了。
```

---

## 填写规则

### 状态目录与 gitignore

`.planning/loops/<slug>/` 需要被跟踪时，白名单条目由**设立该循环的一方**（一次性
`/goal` track 或人工提交）按 `docs/agents/goal-template.md` runbook 第 0 步模式添加。
循环运行期只写 State home 内的文件，不改任何被跟踪文件——包括 `.gitignore` 本身。
仅本地留存的循环不需要白名单，STATE.md 写明 "local-only" 即可。

### 分支与提交（与 /goal 的边界）

循环默认**不开分支、不产生 commit**。循环需要修改被跟踪文件时，改用 `/goal` 起一个
一次性 track 完成修改；循环本体的写权限仅限其 State home。这个约束保证"常驻自动化"
永远不直接污染 master 的工作树状态。

### 技能引用

规则同 `docs/agents/goal-template.md`：引用前 `ls .agents/skills/<name>/` 验证存在；
引用格式带全路径。循环常用：`.agents/skills/grilling/SKILL.md`（循环规格未磨清时，先跑
一轮 grilling）、`.agents/skills/retro/SKILL.md`（循环满 N 轮后的复盘）。

---

## 硬性自查清单（提交 `/loop` 前逐条执行）

- [ ] 第一段是参数头；`budget` 已填数字。
- [ ] Re-fire 行已写明再触发方式；正文声明"本模板不提供常驻进程"。
- [ ] Loop lens 第 1 问有事实依据的回答。
- [ ] Trigger 写明空转出口与感知手段。
- [ ] Checkpoint 唯一，且位于流程最后一站（checkpoint=no 时写明失败停止条件）。
- [ ] Brief 规则与 checkpoint 参数一致。
- [ ] STATE.md 字段四件套齐全；本轮从 last_cursor 续跑。
- [ ] Review vocabulary 槽已填（透镜 + P0–P3 引用）。
- [ ] Autonomy defaults ≥7 条且每条可判定。
- [ ] 零 PR 收尾流程、零 merge 术语。
- [ ] 单轮超时是明确数字。
- [ ] 全文不可判定词命中数 = 0（排除检查命令自身行后；命令见 goal-template.md"措辞"一节）。

---

## 反模式（出现即打回）

| 反模式 | 为什么错 |
| --- | --- |
| 每轮全量重扫历史 | 有 last_cursor 却不用，预算随历史线性膨胀。**注脚**：审查对象本身为整库的循环（如全库线审查）不受此条约束，但必须在 Trigger 节写明整库重审的事实依据，且 cursor 仍用于记录"上轮审到哪个基线" |
| 同一问题每轮重复提醒 | 进 `suppressed`；第二次报告必须来自真实变化 |
| Checkpoint 放在流程开头 | 违背 push right：人在信息量最低的时刻被打断 |
| Brief 里贴原始输出 | 人读的是决策材料；贴原始产出等于没有 Brief |
| 给循环写 PR / merge 流程 | 循环无终态；PR 是 `/goal` 的收尾动作 |
| 空转轮也产出报告 | 空报告累积成噪音，最终没人读 |
| 失败步骤静默跳过 | 循环看起来在跑，实际已经瞎了 |
| 循环内直接改被跟踪文件 | 写权限越界；应起一次性 `/goal` track |
| 循环内自己改 `.gitignore`/加白名单 | 白名单属设立方的一次性动作，不是循环运行期行为 |
