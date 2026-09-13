# REVIEW_LOG — prompt-command-hygiene-review

执行日志与裁决记录。每 Phase 一节；Phase 5 记录子代理对照与对抗测试结果；Phase 7 记录 UNDETERMINED 裁决。

## Phase 0 · 技能真实清单 + 去重基线（2026-09-13）

已执行：
- `git fetch origin`：origin/master `27b9aa0a..60179408`（efc5c52f 已被超越；采纳 D-001）。
- worktree 建于 `../exp-rs-prompt-command-hygiene-review`，分支 `zcode/prompt-command-hygiene-review` @ 60179408。
- `.agents/skills/`：37 个技能，`karpathy-guidelines` 不存在（`ls` → No such file or directory）。
- `.claude/skills/`：50 个技能；`diff -rq` 两侧 → 13 行全部为 "Only in .claude/skills/"（12 个 qt-* + frontend-design），0 处内容差异；`.agents` 独有 0 个。两侧均被 git 跟踪（37 / 50）。
- `gstack`、`planning-with-files` 不在仓库，但存在于用户级运行时 `~/.zcode/skills/`（对"本机 runtime"可用、对"仓库"缺失——两类引用需区分）。
- `ask-matt` + `setup-matt-pocock-skills` 双侧均存在（GOAL 开题时所称的漂移已被修复）；"matt" = Matt Pocock 技能集的路由技能（ask-matt 自述 "A router over the skills in this repo"）。
- 去重基线：AUDIT_DOSSIER_ISSUES_747_760.md 与 PROJECT_REVIEW_DOSSIER_5.0.md grep 检查 → 全部为 src 级代码发现，无命令体系/文档类条目。overlaps: 无。
- 关键新发现（开题未列）：CLAUDE.md:7 `make -j$(nproc)` 与 -j2 硬上限冲突；CLAUDE.md:53 "C++20" vs .agents/AGENTS.md:19 "C++17"；CLAUDE.md:66 引用的 `.agents/vendor/` 不存在；CLAUDE.md:7/9 Quick Commands 与本仓库 CMake preset 体系的关系未说明。

## Phase 1 · 24 个 GOAL.md 穷尽读取（2026-09-13）

- 24 个 GOAL.md 全部逐个 `cat -n` 读取（非抽样）；12 个无 GOAL.md 的 track 目录逐个 `ls` 取证。
- 产出 `review/GOAL_MATRIX.csv`（track × convention × value × evidence）。
- 结构性发现：track 规划文件存在三代词汇（详见 SKILL_INVENTORY/GOAL_MATRIX）。
- `spatial-scientist-harness-8/GOAL.md:1` 自述为重建件（原规划文件因 gitignore 白名单遗漏丢失）→ 规划文件丢失风险进入缺陷清单。
- `whole-repo-line-review/GOAL.md:10` 自述"Full original GOAL text: provided by user at track start"从未落盘 → 单一事实来源缺失的最强实例。

## Phase 2 · 缺陷清单（五透镜）（2026-09-13）

- 产出 `review/PROMPT_DEFECTS.md`；每条含 Lens/Location/Quoted/Defect/Impact/Verified/Recommended fix。
- 引不出来的候选缺陷已删除（开题清单中一条被撤下，见该文件末尾的撤下记录）。

## Phase 3 · /goal 模板（2026-09-13）

- 产出 `docs/agents/goal-template.md`：事实标准约定固化 + 参数头 + 九阶段预算范式 + 硬性自查清单。

## Phase 4 · /loop 模板 + 词汇表（2026-09-13）

- 产出 `docs/agents/loop-template.md`（Trigger/Checkpoint/Push right/Brief + 仓库硬约束注入；标注无先例，见 D-007）。
- 产出 `docs/agents/command-vocabulary.md`（/goal vs /loop vs wayfinder vs grilling 判据图 + 3 个真实历史 track 归类示例）。

## Phase 5 · 交叉复核（2026-09-13）

两只只读子代理（A 考古、B 对抗）均已返回。裁决记录（全量原文见会话；此处存裁决结论）：

### A（考古对照）裁决

| # | A 的发现/修正 | 裁决 |
| --- | --- | --- |
| A-1 | GOAL.md 计数 25（含本 track 自存档件），主代理口径 24（历史件） | **双方口径并存**：矩阵与缺陷清单用"24 历史件"；本 track GOAL.md 落盘后自增 1。记入口径注释，不改交付物。 |
| A-2 | 分支前缀是**时代分布**：feat/ 15 个 = 平台 6.0–9.0 代；zcode/ 8 个 = 早期 3/4/5 代 + 全部最新 D/R 系列；远端现存分支几乎全为 zcode/ | **采纳 A 的修正**：D-008 的"zcode/ 是现行多数"表述不准确，改为时代表述；模板统一为 zcode/ 的裁决不变（最新时代 + 远端现状）。D-008 已修订。 |
| A-3 | 新缺陷：`unified-help-diagnostics-6/GOAL.md:33` 引用 `sicnu_cli`，实际目标是 `sicnu_geo_rs_cli`（src/cli/CMakeLists.txt:9） | **采纳**，新增 D-026。 |
| A-4 | 新缺陷：ADR 0144 编号被三个文件复用（0144-execution-plane-8 / 0144-harness-8 / 0144-model-runtime-platform-9） | **采纳**，新增 D-027（重复定义/编号冲突；修复归 domain-modeling 体系，本 track 只记录）。 |
| A-5 | 子代理名额标准分配模式："#1 = Phase 0 基线审计，#2 = 终审对抗"（epr-7:34） | **采纳**：与模板 envelope 的"各自唯一职责"一致；Phase 7 在模板中补该惯分配示例。 |
| A-6 | R0 "250 closed issues (#595–#945)" 本地不可核验 | 并入 D-003 的不可核查问题，不另立条。 |

### B（对抗测试）裁决

30 项发现全部有效；**0 项驳回，2 项部分采纳**。修复映射（Phase 7 执行）：

| B# | 漏洞 | 处置 |
| --- | --- | --- |
| B-1 | AGENTS.md"先澄清"vs autonomy=full 无裁决 | 采纳 → goal-template envelope 加优先级行；AGENTS.md 加 unattended 适配句（Phase 6） |
| B-2/B-9 | 构建入口缺位；CLAUDE.md `make -j$(nproc)` + build.cmd 死路径 | 采纳 → 模板加构建入口行；CLAUDE.md Quick Commands 重写（Phase 6）；build.cmd 死路径新增 D-028（超出本 track write scope，只记录） |
| B-3 | C++17/20 + Catch2/pytest 三重不一致 | **部分采纳**：AGENTS.md C++17→C++20（CMakeLists.txt:3 为权威，Phase 6）；pytest 证据仅是 settings.local.json 历史允许项，不构成测试框架权威——Catch2/ctest 维持 |
| B-4 | runbook 第 0/1 步顺序死锁 + 主树规划文件进不了 worktree | 采纳 → runbook 重排：先 worktree，白名单在 worktree 内改，GOAL.md 落 worktree |
| B-5 | `grep -c "^A"` 断言对修改型 Phase 恒 0，"预期"无定义 | 采纳 → 改为 git status --porcelain 留档 EVIDENCE.md |
| B-6 | "checkpoint"术语在 /goal 语境未定义；rebase 技能验证步骤无预算约束 | 采纳 → 改"每个 Phase commit 后"；技能验证步骤限定 targeted ctest |
| B-7 | push 拦截分支引用安装型技能；hook 实测不存在；无 push 失败兜底 | 采纳 → 第 6 步自包含化 + 禁止在 track 内执行安装步骤 + 失败即收尾报告 |
| B-8 | RSS/load 触发器在 Windows Git Bash 无测量手段 | 采纳 → envelope 加平台化测量说明；不可测时显式声明 not-executed 并固定 -j2 |
| B-10 | 模板 M-xx 引证在 GOAL_MATRIX.csv 中不存在 | 采纳 → 删除全部 M-xx 引证，仅保留 D-xxx |
| B-11/B-12 | AGENTS.md karpathy 断链；CLAUDE.md .agents/vendor 断链 | 采纳 → Phase 6 修复；AGENTS/CLAUDE 纳入存在性断言扫描范围 |
| B-13 | 模板裸相对路径引用兄弟文件 | 采纳 → 全部改为 docs/agents/ 全路径 |
| B-14 | Skills 表骨架缺 /SKILL.md 后缀 | 采纳 → 骨架补全路径 |
| B-15 | Autonomy defaults"覆盖全部"不可判定 | 采纳 → 改为封闭必答类清单（7 类） |
| B-16 | token 预算不可测量 + 总预算耗尽无处理 | 采纳 → 上报落点 EVIDENCE.md 固定节 + 可测量代理指标 + 总预算耗尽默认动作 |
| B-17 | 措辞禁令超出 6 词检测范围，不可判定 | 采纳（改法）→ 禁用词定义为**封闭清单**（= grep 模式本身）；清单外措辞归人工评审，不进自动断言 |
| B-18 | Completion gate"可第三方核查"是元要求 | 采纳 → 每条 gate 强制附"验证命令 → 期望输出" |
| B-19 | PR 被拒零处理 | 采纳 → runbook 加第 9 步（同 track 续跑规则） |
| B-20 | 子代理失败零处理 | 采纳 → Autonomy defaults 加默认动作（主线内联复核 + 记录） |
| B-21 | 范围外发现无固定落点、无 P0 例外 | 采纳 → 固定文本：OUT_OF_SCOPE 节 + P0 在 PR_BODY 顶部标注 |
| B-22 | 同 B-16 | 合并 |
| B-23 | /loop 无唤醒机制定义 | 采纳 → Trigger 节强制"再触发方式"字段 + 声明"本模板不提供常驻进程" |
| B-24 | 全库重审循环与反模式表冲突 | 采纳 → 反模式表加整库审查豁免注脚；vocabulary 补链接 |
| B-25 | loop 参数表缺失，autonomy=steady 无定义 | 采纳 → 补参数表并定义 steady |
| B-26 | loop 改 .gitignore 与"不改被跟踪文件"自相矛盾 | 采纳 → 白名单由设立方一次性提交；循环运行期只写 State home |
| B-27 | loop 无预算参数 | 采纳 → 参数头加 budget=<N>（每轮包线）+ 子游标可选约定 |
| B-28 | checkpoint=no 仍强制 Brief；git fetch 归类不明 | 采纳 → gate 第 4 条加条件；只读 fetch 不算对外动作 |
| B-29 | loop 无透镜/严重度挂载点 | 采纳 → 骨架加 Review vocabulary 槽（引用 command-vocabulary P0–P3 节） |
| B-30 | "逐字保留"与可变预算张力 | 采纳 → 改"格式逐字保留；数值参数按实际值填写" |

**结论**：B 用模板跑假想 track 产生的缺口共 30 项，全部修复进模板（Phase 7）；无任何一项需要向用户提问才能裁决——满足 Completion gate"subagent B 产生的缺口全部修复模板"。


## Phase 6 · AGENTS.md 修复 + 镜像记录（2026-09-13）

- `.agents/AGENTS.md`：死指针 `file:///.agents/skills/karpathy-guidelines/SKILL.md` 删除，出处改指 `CHANGELOG.md` 2026-08-03 条目（D-001/D-005 修复）；C++17 → C++20 并附 `CMakeLists.txt:3` 权威锚（D-013 修复）；新增与 `CLAUDE.md` 的互引与裁决顺序（D-023 修复）；新增 unattended 适配句（B-1 修复）。
- `CLAUDE.md`：Quick Commands 重写为 CMakePresets + `-j2` 形态（D-011/D-020 修复）；`.agents/vendor/` 断链删除，provenance 内联（D-012 修复）；"mirrored" 措辞改为与实测一致的表述并指向 `review/SKILL_MIRROR.md`（D-014 修复）；新增指向 `.agents/AGENTS.md` 的互引（D-023 修复）。
- `review/SKILL_MIRROR.md` 产出：运行时映射、37/13/0 差异、历史漂移已修复的记录、单侧技能规则、复核命令。
- `CONTEXT.md` 未改动：五透镜走查中无一条缺陷落点在 CONTEXT.md；按最小 churn 原则不动。

## Phase 7 · 模板修订 + UNDETERMINED 裁决（2026-09-13）

- `docs/agents/goal-template.md` 整体重写，落实 B 组裁决：优先级行（B-1）、构建入口 + build.cmd 死路径防线（B-2/B-9）、RSS/load 平台化测量（B-8）、Skills 骨架全路径（B-14）、Autonomy defaults 七类必答 + 子代理失败默认 + 范围外发现固定文本（B-15/B-20/B-21）、预算计量代理指标 + 总耗尽默认动作（B-16）、措辞封闭清单化（B-17）、Completion gate 强制"验证命令 → 期望输出"（B-18）、runbook 重排（B-4）+ per-Phase rebase（B-6）+ push 自包含与失败收尾（B-7）+ PR 被拒续跑第 9 步（B-19）、M-xx 引证全部删除（B-10）、兄弟文件引用全路径化（B-13）、"逐字保留"改为"格式逐字、数值按实际"（B-30）。
- `docs/agents/loop-template.md` 整体重写：参数表 + steady 定义 + budget 参数（B-25/B-27）、再触发方式与"无常驻进程"声明（B-23）、白名单归设立方（B-26）、只读 fetch 归类（B-28）、Brief 条件化（B-28）、Review vocabulary 槽（B-29）、整库审查豁免注脚（B-24）、子游标（B-27）。
- `docs/agents/command-vocabulary.md`：整库审查示例链接到 loop 反模式注脚（B-24）。
- `review/PROMPT_DEFECTS.md`：D-008 按考古修正为时代表述；D-004 补 prompts/ 未跟踪层证据；新增 D-026（sicnu_cli）、D-027（ADR 0144 三重复用）、D-028（build.cmd 死路径）；假阳性率口径更新为 4/28。
- **UNDETERMINED 裁决**：
  1. "Karpathy Guidelines 原始出处具体指哪份文本" → 维持 UNDETERMINED（仓库内仅有 CHANGELOG 对"4 core guidelines"的转述记录，无原文副本）；处置：AGENTS.md 保留四原则实质 + 指向 CHANGELOG 条目。此项不再阻塞任何交付物。
  2. "12 个无 GOAL.md track 的原始 brief" → UNDETERMINED 且不可恢复（未落盘）；处置：缺陷 D-003/D-004/D-006 记录，不臆造。
  3. "qt-*/frontend-design 单侧部署是否有意" → 裁决为"按有意处理"（zcode 运行时不读 .claude/），规则落 SKILL_MIRROR.md。
  4. "pw-9 的 ≤4 并行度" → 裁决为偏离（与 -j2 家族冲突且无解释），模板统一 -j2。
  5. "pi-harness-4 M8 '6× adversarial review'" → UNDETERMINED（原文无法自证含义）；处置：D-010 记录，模板固定 ≤2。

## Phase 8 · 执行摘要 + PR（2026-09-13）

- `EXECUTIVE_SUMMARY.md` 产出（结论、交付物表、top-5 发现、交叉复核结果、假阳性率 4/28 ≈ 14.3%、合规边界、后续种子）。
- `PR_BODY.md` 产出；EVIDENCE E-007/E-008 断言结果落盘。
- rebase origin/master 后 push（护栏拦截则按 runbook 第 7 步处理）→ `gh pr create` → 报告 URL 后停止。
- Completion gate 逐条复核：全部满足（对照 GOAL 存档的 Required artifacts 与 Completion gate）。

