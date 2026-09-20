# GOAL — ds41-dev-worktree-tooling · Track D5 · Worktree/PR Hygiene & Agent Developer UX

> Verbatim archive of the track brief (per `docs/agents/goal-template.md` "Required planning files").
> **Track branch:** `agent/ds41-dev-worktree-tooling` (worktree `../exp-rs-worktrees/ds41-dev-worktree-tooling`, off `origin/master` @ `adf8f98952422fe9c386c56d64d5fb6a4a6642f1`)

# OpenCode / DeepSeek v4.1 Flash Track D5 — Worktree/PR Hygiene, Local Review Automation & Agent Developer UX

**运行器/模型：** OpenCode / DeepSeek v4.1 Flash
**工程预算量级：** 大规模长任务，可充分使用 token（这是可投入的探索/实现规模上限，不是要求无意义消耗 token；以 Oracle 完成为停止条件）
**track-id：** `ds41-dev-worktree-tooling`
**执行方式：** 全自动、无人值守、独立 worktree、独立 review、独立 PR。不要询问用户做常规技术选择；基于证据自行决策。

▎ [GOAL Loop] 已启动
▎ 目标：把当前高并发 agent 开发中"预读、去重、worktree、资源锁、review、PR证据、历史分支识别"做成安全的本地开发工具链，降低下一轮 10+ agents 互相踩踏。
▎ 完成条件：本文件的全部 Oracle 有可复现证据，关键 gate 连续通过两次，独立 review 的 P0/P1 清零，并已创建独立 PR。
▎ 迭代上限：允许长循环；不要因默认 15 轮提前停止。连续失败时按 goal-loop 的"换假设→换层级→质疑前提"升级规则推进。

## 固定项目上下文（只作启动快照，运行时必须重新刷新）

- 仓库：`https://github.com/WindWang2/exp-rs`
- 2026-09-20 制作本 Prompt 时，`master` 快照为 `2761a6857f2a879b29ec0fa70d7439c38c964deb`，最新合入记录为 `#1115 fix: deep-review wave-2 investigation targets (#1097)`。
- 制作快照时 GitHub **open PR = 0、open issue = 0**；但这一状态在执行 Prompt 时可能已变化，**禁止把本快照当实时事实**。
- 近期 master 已完成一轮大规模 fail-closed / 并发与生命周期 / HTTP+fabric / workflow resume / plugin trust / data transaction / CI portability 修复。远端仍残留若干 `agent/*`、`fix/*` 历史分支；抽样分支相对当前 master 已 `diverged` 且落后约 78 个提交，因此它们只作为历史证据读取，不得直接作为新开发基线。
- 项目是基于 QGIS 的纯 C++ 遥感分析平台，已有 Processing Registry、TaskCenter、Workflow、MCP/Pi Agent、模型运行时、时序、分类、光谱、OBIA、离线实验包和 1800+ Catch2 测试。**不重新造已有系统。**
- 可参考 `bravesd/goal-loop`：把目标转为客观 Oracle，每轮只做一个聚焦改动并亲自验证，记录 `.goal-loop-ledger.md`，关键验证连续通过两次后才能结束。

## Phase 0 — 强制实时预读与去重（任何写代码之前必须完成）

1. 在主仓库只读执行：
   ```bash
   git fetch origin --prune
   git status --short
   git log --oneline --decorate -30 origin/master
   git branch -r
   git worktree list
   gh pr list --state open --limit 100
   gh pr list --state merged --limit 80
   gh issue list --state open --limit 200
   gh issue list --state closed --limit 100
   ```
2. 读取**最新 master、所有 open PR、与本 Track 文件范围有关的最近 merged PR、review comments、open/最近 closed issues**。对可能重叠的 PR 必须执行 `gh pr view <N> --comments` 并查看 reviews / files；不能只看标题。
3. 对仍存在的远端 `agent/*` / `fix/*` 分支，用 `git log origin/master..origin/<branch>`、`git diff --stat origin/master...origin/<branch>` 判断它是：已合入历史残留、被后续 PR supersede、还是确有未合入增量。**不得 cherry-pick 旧分支整包。**
4. 对本 Track 计划涉及的目录执行源码级 census：入口、公共 API、测试、ADR、能力清单、注册表、调用者、线程/所有权模型、失败/取消路径、性能热点。先确认"已有能力是什么、真实缺口是什么"。
5. 在 `.planning/<track-id>/` 生成并维护：
   - `BASELINE.md`：实时 master SHA、PR/issues/review 摘要；
   - `OWNERSHIP.md`：本 Track 可写文件、只读文件、共享 append-only 文件；
   - `DEDUP.md`：与现有 PR/branch/issue 的重叠判定；
   - `ORACLES.md`：客观完成条件与验证命令；
   - `DECISIONS.md`：关键设计取舍及替代方案。
6. 若发现新 open PR 已覆盖本 Track 的某个子任务：**自动缩小/换成相邻未覆盖缺口**，继续完成 Track 总目标，不要停下来让用户选择，也不要复制实现。

## 独立 worktree

预读完成后，从**刚 fetch 的最新 `origin/master`**创建唯一 worktree；绝不在 master checkout 写代码：

```bash
TRACK=<本文件指定的 track-id>
BRANCH=agent/${TRACK}
WT=../exp-rs-worktrees/${TRACK}
git worktree add "$WT" -b "$BRANCH" origin/master
cd "$WT"
git status --short
git rev-parse HEAD
```

若分支名已存在，自动采用带日期/短 SHA 的新名字，不覆盖旧分支。进入 worktree 后再次核对 `origin/master`、PR、issues 是否有漂移。

## Track 边界与文件所有权

- 主 owner：`.agents/**`、新 `scripts/dev/**`、`docs/development/**`、tooling tests。
- 默认不改 product source。
- 工具不得自动删除远端分支/关闭 PR/merge PR；危险操作必须 fail-safe。

**并行开发原则：** 只在上述 owner 范围内做主实现；共享注册表/CMake/能力清单尽量 append-only。发现并行 PR 已修改共享文件时，先 rebase 后做最小集成，不把别人的功能复制进本 Track。

## Subagent 规则

本 Track 对 subagents **不设数量上限**，但必须按文件所有权分工并由主 agent 汇总；任何时刻只能由主 agent 协调编译，禁止多个 subagent 并发重编。

## 必做工作包

1. **Repo Preflight** — 一条命令输出 master SHA、open PR/issues、recent merged、worktrees、远端历史分支与 ahead/behind。
2. **Overlap Scanner** — 给定计划路径，列出最近 PR/branch 触及文件与潜在冲突；只作为 evidence，不自动判定代码等价。
3. **Worktree Creator** — 从 fresh origin/master 建唯一 branch/worktree，防重名、防在 master 写入。
4. **Resource Guard** — 本地 build wrapper/锁，保证单个 build -j1/-j2，并避免同一 build dir 被多个 agent 同时写。
5. **Review Pack** — 自动收集 diff stat、changed APIs、tests、git diff --check、baseline、known conflicts，生成 PR body 草稿。
6. **Stale Branch Report** — 识别已 merged/superseded/严重落后分支，**只报告建议**，绝不自动删除。
7. **Agent Skill Docs** — 把 goal-loop、code-review、planning/worktree 约定写成短而明确的 agent 开发指南。

## 明确非目标

- 不自动 merge/close/delete GitHub 资源。
- 不绕过 git safety 或 force-push master。
- 不引入线上 CI/CD。

## 客观 Oracle（不能靠主观"完成"判断）

- [ ] 在本仓库 fixture/真实只读状态下 preflight 能正确输出当前 master/open PR/issues/worktree/branch 状态。
- [ ] 尝试从脏 master、重复 branch、同一路径 worktree 创建时工具 fail closed。
- [ ] resource guard 能证明并发第二个写同 build dir 的进程被拒绝/序列化。
- [ ] review pack 对一个测试分支生成完整、可复现的 PR evidence。

## 预期冲突/集成热点

- Build Portability Track 负责 CMake/依赖，本 Track 只负责 agent/dev orchestration wrapper。
- 不修改业务 Track 的 `.planning` 内容；仅提供模板/工具。

## 资源与并发硬约束

- **无需等待线上 CI/CD**；不要为了 CI 状态空等。所有 merge-candidate 证据以本地 targeted build/test/review 为准。
- 编译上限：`-j1` 优先，必要时最多 `-j2`；严禁 `-j3+`。
  ```bash
  export CMAKE_BUILD_PARALLEL_LEVEL=1
  export CTEST_PARALLEL_LEVEL=1
  cmake --build <build-dir> --parallel 1   # 必要时最多 2
  ctest --test-dir <build-dir> -j1 --output-on-failure
  # Ninja 同理：ninja -j1，必要时最多 -j2
  ```
- 优先编译受影响 target，不要反复全量重编 QGIS/OTB/ITK。只有 Oracle 明确需要才做全量 gate。
- subagent **禁止同时启动大编译**；编译由主 agent 串行协调。subagent 以源码审计、设计、测试设计、review 为主。
- 遇到内存压力立即降到 `-j1`、缩小 target、复用合法缓存；禁止用并发硬顶导致宿主崩溃。
- 测试默认 `QT_QPA_PLATFORM=offscreen`；涉及外部依赖时优先使用仓库已有 fixture / hermetic seam，不把联网服务作为完成条件。

## GOAL Loop 执行纪律

- 每轮先读取本 Track ledger，列出距离 Oracle 的**一个**真实缺口；每轮尽量只推进一个可归因改动。
- 每轮必须亲自运行相应静态检查/测试/微基准，不得"看代码觉得没问题"。
- 若发现新 defect 且属于本 Track owner 范围，建立最小复现后直接修；若明显属于其他并行 Track，记录证据并开 issue/在 PR body 标注，不跨界大改。
- 对性能目标同时记录正确性基线；任何优化都必须 byte/semantic equivalent 或明确记录科学语义变化。
- 不通过删测试、放宽断言、吞异常、关闭 feature、降低精度来制造绿色。
- 不以 token 数作为完成判定；预算大用于充分预读、设计、反例、测试、review 和迭代。

## 独立 Review、收口与 PR（必须执行）

1. 功能 Oracle 首次满足后，**重新 `git fetch origin --prune` 并重读新出现的 PR/review/issues**。若 master 漂移，rebase/合并最新 `origin/master`，解决冲突后重新验证。
2. 启动一个与实现角色分离的独立 reviewer，对 `origin/master...HEAD` 做 read-only 深审，至少覆盖：
   - 正确性与边界条件；
   - 线程安全、Qt/QGIS 对象生命周期与所有权；
   - 取消/失败/异常/回滚/部分输出；
   - 数值与科学语义；
   - API/ABI/序列化兼容性；
   - Windows/Linux/macOS 路径与构建可移植性；
   - 性能、内存与大数据规模；
   - 安全/路径/外部输入边界；
   - 测试是否真的能在修复前失败、修复后通过；
   - 与并行 PR 的文件冲突和语义重复。
3. P0/P1 必须修复；P2 能修则修，确需延期必须写进 PR 的 `Known limitations` 并给出可复现证据。review 后重新跑受影响 Oracle。
4. **关键验证连续跑两遍**，两遍都通过才可宣告完成；不得用"编译成功"代替行为测试。
5. 最终清理：
   ```bash
   git diff --check
   git status --short
   ```
   删除调试输出、临时二进制、无关格式化；`.goal-loop-ledger.md` 默认只留在 worktree，不与其他 Track 争抢共享账本，除非最新仓库规范明确要求提交。
6. 逻辑分段 commit，commit message 清晰；push 独立 branch，并创建**本 Track 独立 PR**。PR body 至少包含：baseline SHA、实时去重结果、范围/非范围、设计、issue/需求映射、测试命令与两次结果、资源限制、review 处置、已知限制、与其他 PR 的冲突热点。
7. 不自行 merge PR，不等待线上 CI。创建 PR 后输出 PR URL 和本地验证证据即结束。
