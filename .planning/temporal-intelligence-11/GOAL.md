# /goal — F04 · Temporal Phenology & Change Intelligence 11.0

> Verbatim record of the track prompt received 2026-09-15.

/goal  target-agent=zcode  model=GLM-5.3-flash  budget=500000000  subagents<=2  ci=none  autonomy=full  defaults=best

> **Mission:** 季节分量突变、模型选择、不确定性与区域时序能力升级
> **Target model:** GLM-5.3-flash（长跑大预算；以严格 Oracle/ledger 防止低质量漂移）
> **Branch:** `zcode/temporal-intelligence-11`
> **Worktree:** `../exp-rs-temporal-intelligence-11`
> **Terminal state:** 独立 PR 已创建；不 merge；不等待在线 CI。

## Mission definition

最终产品不是“写了一批代码”，而是：**季节分量突变、模型选择、不确定性与区域时序能力升级**，并且其 authority、failure semantics、resource bounds、provenance、tests、docs、agent/CLI/GUI surface（适用时）相互一致。先证明已有能力和真实缺口，再实现；对所有科学公式/坐标/单位/时间/NoData 语义采用独立真值，不允许测试复用被测实现来制造绿灯。

## Work packages

| ID | Package | Required deliverables |
|---|---|---|
| A | seasonal-component breaks | 检测趋势与季节幅相变化，明确与完整 BFAST/CCDC 的差异。 |
| B | 逐段模型选择 | harmonic order/trend family/penalty 的 bounded selection，AIC/BIC/CV 语义与退化处理。 |
| C | 不确定性与置信 | bootstrap/analytic CI 的可选实现，缺测/不规则采样/质量权重传播。 |
| D | 多季物候 2.0 | 多 cycle 自动候选、农作制度事件、跨年窗口、质量旗标，禁止低样本硬猜。 |
| E | 区域/地块时序 | 共享 point-in-polygon/ROI reducer、批量区域 table、可取消、内存有界。 |
| F | 独立区域时序 UI | 曲线/事件/质量/区域选择，不改 D18 owned mount；通过现有 command/plugin seam 暴露。 |
| G | 性能重构 | fit scratch 重用、tile batching、SIMD 仅在结果一致性验证后采用。 |
| H | 跨年/跨区 synthetic corpus | 已知断点/双季/缺测/异常/无变化 negative controls。 |

每个 package 都必须包含：现状证据 → 设计选择（至少两个候选时写 DECISIONS）→ 最小 vertical slice → known-answer/negative test → failure/cancel/resource handling → 文档/contract/surface 同步 → commit。不要先堆几万行再统一测试。

## Token budget / execution phases

总预算 **500,000,000 tokens**（规划包线，不是消耗 KPI）。

| Phase | 内容 | Budget |
|---:|---|---:|
| 0 | 最新态审计、ownership、architecture map、GOAL/PLAN 落盘 | 30M |
| 1 | 基础契约/数据模型/authority 层 | 80M |
| 2 | 核心算法/执行能力第一大块 | 90M |
| 3 | 核心算法/执行能力第二大块 | 75M |
| 4 | surface/integration/compatibility | 65M |
| 5 | 规模、故障、并发、性能硬化 | 55M |
| 6 | E2E、known-answer、drift/生成物验证 | 45M |
| 7 | 独立 adversarial review + 全部 P0/P1 remediation | 35M |
| 8 | 最终双验证、rebase、PR evidence/提交 | 25M |

## GOAL Loop Oracle（未满足不得结束）

1. 趋势 break 与季节 break 能在独立 synthetic truths 上区分
2. 模型选择结果/penalty 可解释且 deterministic
3. 低覆盖/大 gap 场景返回质量/refusal 而非伪物候
4. temporal suites 两次通过，性能不引入无界内存
5. `git diff --check origin/master...HEAD` clean；无冲突标记/secret；所有新增生成物/manifest drift gate clean。
6. Phase 8 完成后把关键 targeted validation **原样连续运行两遍**，两次都通过（或同一明确、与本 diff 无关的 pre-existing/host limitation 被对照证明）。
7. 独立 review 完成：P0=0、P1=0；所有 finding 有 disposition；PR 已创建且未 merge。

## Autonomy defaults（摘要）

1. 优先使用 master 已有 registry/schema/domain authority；禁止第二真值。
2. 失败项先根因分析；环境缺失才标 not-executed；单测失败不得删 test 换绿。
3. 沿用现有 namespace/operator/error/ADR 规则；冲突时最小 additive 命名。
4. build `-j2`→压力大降 `-j1`；test `-j1`；scale 测试 opt-in。
5. 允许 fetch/读 GitHub/push 自己分支/建自己 PR；禁止 merge/close/改他人 PR/issue。
6. 范围外发现写 EVIDENCE `OUT_OF_SCOPE`。
7. 默认不加新依赖。
8. 并发冲突优先 rebase + 重新审计；禁止复制实现避冲突。
9. `ISSUES.md`/CHANGELOG/历史 GOAL 只做线索，缺口必须对当前 code 重新验证。

## Operating envelope（摘要）

- 全自动，不向用户提问；歧义取最保守方案写 DECISIONS。
- master 只读；所有编辑只在独立 worktree/branch。
- 不等待在线 CI；capability claim 只来自本地可复现 evidence。
- 资源硬上限：`CMAKE_BUILD_PARALLEL_LEVEL=2`、`CTEST_PARALLEL_LEVEL=1`、`-j2`/`-j1`；`QT_QPA_PLATFORM=offscreen`。
- 不把 wall-clock benchmark 当 correctness gate。
- Subagents ≤2（#1 Phase 0 只读审计、#2 最终只读对抗 review）。
- 输出/缓存原子性、cancel、失败清理、Unicode path、read-only source、NoData/CRS/provenance 全考虑。

## Runbook（摘要）

1. Phase 0 只读审计 → worktree（已完成：见 BASELINE.md / PARALLEL_OWNERSHIP.md）。
2. 落盘 GOAL/PLAN/BASELINE/DECISIONS/EVIDENCE/REVIEW_LOG/PR_BODY/PARALLEL_OWNERSHIP/TEST_MATRIX。
3. 每 Phase ≥1 原子 commit；commit 后 rebase origin/master。
4. 共享注册文件推迟到独立 integration commit，append-only。
5. 主 agent 全 diff review → 独立 reviewer → P0/P1 全修 → 重跑 gate。
6. `git diff --check origin/master...HEAD`、冲突标记/secret 扫描、targeted tests 连续两遍。
7. push（禁 force）→ `gh pr create --base master` → 不 merge、不等 checks。

## Ledger 协议

`.goal-loop-ledger.md` 每轮：`Round N | 当前 Oracle 差距 | 单一聚焦改动 | 验证命令 | 实际结果/exit | PASS/FAIL | 下一步`。连续失败 3–4 轮列 3 个根因假设；5–7 轮换工具/层级；8+ 轮重查前提。
