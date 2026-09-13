# `/loop` 模板 · 常驻循环指令规范

本文件是 `/loop` 提示词的唯一事实来源。骨架取自 `.agents/skills/loop-me/SKILL.md` 的
Trigger / Checkpoint / Push right / Brief 词汇，注入本仓库的硬约束（资源上限、无 CI、
技能加载、中断恢复）。

**证据等级声明**：`/goal` 在本仓库有 24 个实战样本（`review/GOAL_MATRIX.csv`）；`/loop`
没有——`.planning/`、`.agents/`、`docs/` 中零引用，loop-me 的产物目录 `workflows/` 也不存在
（`review/PROMPT_DEFECTS.md` D-016）。本模板是推导出的新约定：词汇来自 loop-me（已落盘
技能），硬约束来自 /goal 体系（已验证约定）。首个实战 track 跑完前，不把它当"事实标准"引用。

选择判据（/loop vs /goal vs wayfinder vs grilling）见 `command-vocabulary.md`，此处不重复。

---

## `/loop` 的定义

一条常驻循环替人执行一个可委派的复现模式。它的成功标准是**每一轮稳定产出同样的交付物**，
而不是"做完就结束"。循环没有终态；**循环不写 PR 收尾流程**（PR 是 `/goal` 的收尾动作）。

与 `/goal` 的区别速览（完整对照表见 `command-vocabulary.md`）：

| 维度 | `/goal` | `/loop` |
| --- | --- | --- |
| 生命周期 | 一次，PR 合入即结束 | 常驻，反复运行 |
| 触发 | 人发起 | Trigger：事件优先于排程 |
| 人工介入 | 无（autonomy=full） | Checkpoint，推到最后一站（push right） |
| 状态 | 无状态 | 有状态：跨轮游标 |
| 版本控制 | 分支 + PR | 默认不开分支、不提交（见第 5 节） |

---

## 模板骨架（复制以下整块，填占位符）

```markdown
# LOOP — <ID> · <Loop Name>（<一句话定位>）

/loop  target-agent=zcode  trigger=<event|schedule>  checkpoint=<yes|no>  ci=none  autonomy=steady

> **State home:** `.planning/loops/<slug>/`（`STATE.md` · `RUN_LOG.md`）
> **Trigger:** <事件描述；或排程表达式 + 选排程的理由>
> **Checkpoint:** <有/无；有则写明位于流程哪一站>
> **Working tree:** <默认"主工作树直读，零提交"；若声明写权限，写 `loop/<slug>` 分支规则>

## Mission

<这条循环维护什么？它替人省掉哪个重复动作？每轮的交付物是什么？>

## Loop lens

1. **为什么是循环不是一次性任务？** <回答：产出会不会过期/漂移/新增；写明维持判断的事实依据。>
2. **内部还套着哪些更小的循环？** <可选。嵌套循环常能拆出逐件处理的实现。>

## Trigger

- **类型**：event / schedule
- **触发条件**：<如"master 新合入 ≥1 个 merge commit" / "每周一 09:00 本地时间">
- **选型理由**：<事件触发默认更省资源；选 schedule 必须写出事件不可观测的具体原因>
- **空转出口**：<无新输入时本轮的直接结束动作；空轮不产出报告>

## Checkpoint & push right

**原则：把人工介入推到流程最后一站；一次问清；带全套材料去问。**

| 环节 | 要人吗 | 说明 |
| --- | --- | --- |
| 采集 | 否 | <…> |
| 加工 | 否 | <…> |
| **闸口** | **是** | <唯一人工点；位置 = 流程最后一站> |
| 交付 | 否 | <…> |

**Brief 格式**（闸口呈现给人看的决策材料，非原始产出）：

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
| `last_cursor` | 上轮处理到哪（commit SHA / 日期 / 文件游标）；本轮从这里续，禁止全量重扫 |
| `open_items` | 未闭环项 |
| `suppressed` | 已知且决定不再报的项；第二次报告必须来自真实变化 |

## Autonomy defaults (do not ask — apply these)

1. **无新输入**：执行空转出口，本轮结束，零报告。
2. **重复发现**：已在 `suppressed` 的不再报；连续 2 轮出现的同一新项升级为 `open_items` 并进下轮 Brief。
3. **资源上限**：编译 `CMAKE_BUILD_PARALLEL_LEVEL=2`、Ninja `-j2`、RSS > 70% 或 load > 1.5× cores 时 `-j1`、`-j$(nproc)` 禁止；测试 `QT_QPA_PLATFORM=offscreen`、targeted `ctest -R <family> -j1`。
4. **写权限**：默认只读。要写必须在本骨架 Working tree 行显式声明范围与分支规则。
5. **对外动作**：禁止——不 push、不建 issue、不发邮件，除非本骨架显式授权具体动作。
6. **单轮超时**：<写明确分钟数；到点如实报告当前进度并结束本轮，不带病续跑>。

## Per-run gate（每轮都要过）

- [ ] `STATE.md` 已更新（`last_cursor` 前进，或写明为何未前进）
- [ ] 空转轮没有产出报告
- [ ] 所有结论带 `文件:行号` 或命令 + 退出码
- [ ] Brief 按模板生成，零原始产出
- [ ] `suppressed` 项未被重复报告
- [ ] 未执行任何未授权的对外动作

## Failure handling

某一轮失败（命令报错、资源超限、超时）：把失败原文与退出码写入 `RUN_LOG.md`，在
`STATE.md` 标记失败点，下一轮从 `last_cursor` 续跑。静默跳过失败步骤禁止——
静默跳过会让循环看起来在跑，实际已经瞎了。
```

---

## 填写规则

### 状态目录与 gitignore

`.planning/loops/<slug>/` 落盘前先按 `docs/agents/goal-template.md` runbook 第 0 步添加
白名单条目并 `git check-ignore` 自证。仅状态与日志属于循环；循环的批量产物默认落在
本地目录（写明路径），不进 git。

### 分支与提交（与 /goal 的边界）

循环默认**不开分支、不产生 commit**。循环需要修改被跟踪文件时，改用 `/goal` 起一个
一次性 track 完成修改；循环本体的写权限仅限其 State home。这个约束保证"常驻自动化"
永远不直接污染 master 的工作树状态。

### 技能引用

规则同 `goal-template.md`：引用前 `ls .agents/skills/<name>/` 验证存在；引用格式带全路径。
循环常用：`.agents/skills/grilling/SKILL.md`（循环规格未磨清时，先跑一轮 grilling）、
`.agents/skills/retro/SKILL.md`（循环满 N 轮后的复盘）。

### 资源上限

循环复用仓库硬约束（见骨架 Autonomy defaults 第 3 条），不因"只是跑一圈"放宽。

---

## 硬性自查清单（提交 `/loop` 前逐条执行）

- [ ] 第一段是参数头。
- [ ] Loop lens 第 1 问有事实依据的回答。
- [ ] Trigger 写明空转出口。
- [ ] Checkpoint 唯一，且位于流程最后一站（checkpoint=no 时写明"全自动"及失败时的停止条件）。
- [ ] Brief 模板存在且声明零原始产出。
- [ ] STATE.md 字段四件套齐全；本轮从 last_cursor 续跑。
- [ ] Autonomy defaults ≥6 条且每条可判定。
- [ ] 零 PR 收尾流程、零 merge 术语。
- [ ] 单轮超时是明确数字。
- [ ] `grep -cE "尽量|适当|必要时|合理|充分|酌情" <loop 文本>` 输出 0。

---

## 反模式（出现即打回）

| 反模式 | 为什么错 |
| --- | --- |
| 每轮全量重扫历史 | 有 last_cursor 却不用，预算随历史线性膨胀 |
| 同一问题每轮重复提醒 | 进 `suppressed`；第二次报告必须来自真实变化 |
| Checkpoint 放在流程开头 | 违背 push right：人在信息量最低的时刻被打断 |
| Brief 里贴原始输出 | 人读的是决策材料；贴原始产出等于没有 Brief |
| 给循环写 PR / merge 流程 | 循环无终态；PR 是 `/goal` 的收尾动作 |
| 空转轮也产出报告 | 空报告累积成噪音，最终没人读 |
| 失败步骤静默跳过 | 循环看起来在跑，实际已经瞎了 |
| 循环内直接改被跟踪文件 | 写权限越界；应起一次性 `/goal` track |
