# 02 — Test Ledger: repair-planner-recovery-integration-r3

全部行号基于分支工作区（基线 9ea5a2fd + 本 PR 修改）。构建目录 `build-lab`（Ninja, Debug, ENABLE_TESTS=ON，窄目标构建，未做全量 build）。

## Oracle 证据（RED → GREEN）

| # | Oracle（目标映射） | 测试 | RED 证据（基线行为） | GREEN |
|---|---|---|---|---|
| 1 | 相同 finding+authority 计划 fingerprint 稳定（oracle 清单 1） | `recovery repair projection is provider-backed planning with real facts` | FAIL：基线伪造投影，proposals 为空时直接 `no_proposals`，无 findings_digest 概念 | PASS，两次 decide() 字节一致 |
| 2 | 候选来自 provider 条目（cost=light→rank2、operator、risk），不伪造 | 同上 | FAIL：伪造 `rank=2/shape_preserving` 与条目无关 | PASS |
| 3 | 无 capability knowledge 时计划零候选（fail-closed；目标 1） | `without capability knowledge the recovery bridge plans nothing` | FAIL：`"repair" == "ask"`——无 provider 仍走 auto repair | PASS `no_provider` typed cause |
| 4 | unsupported finding 保持 typed，不得猜相似 repair（非目标清单） | `unsupported finding codes stay typed through the recovery projection` | FAIL：基线无 unresolved 概念 | PASS `unsupported_finding` + `resolves_all_blockers=false` |
| 5 | science-changing 不得走 preparation-only auto path，**与 autonomy 等级无关**（oracle 清单 3） | `the plan's own risk class drives the science-changing gate, not the caller claim` | FAIL：`"repair" == "ask"`——caller claim `shape_preserving` 压过真实 radiometric 合同 | PASS：gate 读计划 selected[0].risk_class；批准后仍 radiometric 且 `requires_reverification=true` |
| 6 | capability 删除/variant 变化后旧候选不能复活（oracle 清单 2） | `a removed capability cannot revive through the recovery projection`（agent_ops）+ `live repair capability source routes the real knowledge documents`（capability，真文档） | FAIL：基线投影完全无视 provider（两 provider 得同一伪造计划） | PASS：shrunken provider 改选 rs:align；全空 provider → `no_candidate`；live 文档删除 rs:resample 后路由不再产出 |
| 7 | approval 绑定 plan/coordinator/时钟，replay/过期/跨 coordinator/tamper 拒绝（oracle 清单 4） | `repair approval tokens bind plan, coordinator and clock window` + `coordinator repair approval: arm, consume once, replay refused` + `the surface approve_repair action mints and arms a bound token` | 编译 RED（API 不存在）；行为 RED 由 RED-2 轮 ctest 输出佐证（裸 bool 任意 approve 均成功） | PASS：kExpired/kWrongPlan/kWrongCoordinator/kTampered/kMalformed 全 typed；armed→launch 消费一次；同 token 再 arm → `APPROVAL_REPLAYED`；launch 时过期 → `out.approvalError=APPROVAL_EXPIRED` 且 gate 关闭 |
| 8 | repair 后必须 re-preflight/re-verify 才能宣称修复（oracle 清单 5） | `requires_reverification` 断言（case #1 与 #5） | FAIL：字段不存在 | PASS：仅 kRepair 决定携带 `requires_reverification=true` |
| 9 | planning state 压力：负序拒绝 / 重启后 eviction 连续 + 审计计数连续 / digest link 负例（目标 4） | `planning state rejects negative sequences...`、`planning state eviction continues after a restart...`、`result digest linking refuses...` | 第一例 FAIL（基线接受 -1）；后两例为回归守卫（基线已绿） | PASS |
| 10 | teaching view 不泄漏执行参数 / agent view 保留 planning contract（目标 6） | 基线 #1278 既有 suites（`teaching view withholds...`、`agent view keeps...`）全绿，本 PR 未触碰视图层 | —（已覆盖） | PASS（回归） |

RED 采集方式：先提交仅含 `RecoveryBridge(&provider)` 构造注入（行为不变）的增量，使新 oracle 可编译；ctest 7/7 失败（`"repair"=="ask"`、`"no_safe_repair"=="planned"` 等 expansion 见 ctest --output-on-failure 记录）。Token/live-adapter API 的编译期 RED 按 missing-API 记录。

## 命令（可复现）

```bash
cmake --build build-lab --parallel 2 --target \
  test_agent_ops_core test_repair_planner_completion \
  test_capability_knowledge test_build_wiring_drift test_repair_planner_schema
ctest --test-dir build-lab -j2 -R '<各 case 正则，见上表>'
```
