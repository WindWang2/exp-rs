# R33-T05 ACCEPTANCE — 首批工作包验收状态

状态图例：DONE（oracle 满足且有动态证据）/ PARTIAL / BLOCKED / NOT_EXECUTED / ALREADY_DONE_WITH_EVIDENCE

## 首批工作包

| WP | 状态 | 证据 |
|---|---|---|
| WP02 分页后新增的 model 通知 | **PARTIAL→待 CTest 补 sidecar 无关项即 DONE** | 独立运行 `test_r33_t05_mission_paging`：**All tests passed (2616 assertions in 5 test cases)**；含 RowContractWatcher 逐信号契约（insert 范围==rowCount 增量、无 remove、dataChanged 合法域）、QPersistentIndex 稳定、尾行 fetchMore 可达、隐藏行无通知不变可见、setPageSize 提升带通知。既有 `test_mission_scale_benchmark` 更新后全过（6 cases / 33054 assertions，独立运行） |
| WP05 journal seq 和构造边界 | **DONE（动态）** | `test_r33_t05_journal_bounds`：51 assertions / 6 cases 全过（next_seq 采用不回退、INT64 顶/next_seq≤last/非整数拒绝、append 溢出拒绝不回绕、legacy 无 next_seq 兼容） |
| WP06 journal compaction 保留终态语义 | **DONE（动态）** | 同上：compactProjection 保留 terminal string stopReason；磁盘 save(>1MiB payload)→load→replay stopReason 保留；既有 `test_agent_loop_core` compaction 套件不回归（17 cases / 216 assertions 独立运行全过） |
| WP08 Mission sidecar 有界读入 | **PARTIAL**（三读路径已改共享有界读；`readMissionSidecarBytes` 直测+authority 超限毒化保持 last-good 的断言在 `test_r33_t05_sidecar_recovery`，待 CTest 构建；legacy 超限同） | 生产代码语法级验证 + 设计评审；CTest 运行待主机锁 |
| WP09 last-good 的同一字节证据 | **PARTIAL**（同上：rotateLastGood 单次读取绑定验证字节；last-good==sidecar 字节相等断言待 CTest） | 同上 |
| WP11 trace 脱敏与 fault marker 最小化 | **DONE（动态）** | `test_r33_t05_redaction_delivery`：40 assertions / 3 cases 全过（fault.api_key/嵌套 authorization 被脱敏、id/type/code/数值证据保留、长描述预算、secret 形状 fault string 拒绝、合法 string marker 精确保留、科学 digest 不受影响） |
| WP13 claim 与 verifier report 的可信度 | **DONE（动态）** | 同上：未知 schema report → FAIL+confidence 0.0+reason、空/null report 保持 legacy 0.9、合法 pass 保持 0.9、外部 claims 标 source:external（自带 source 不覆写）、平台 claim 标 source:platform、capsule 导出保持区分 |

## 受影响既有套件（独立运行，全部通过）

- `test_mission_scale_benchmark` 6/33054 ✓（append-heavy 断言按新分页语义更新）
- `test_r30_t05_journal_read_bound` 3/13 ✓（fixture next_seq 修正为合法形状）
- `test_agent_loop_core` 17/216 ✓
- `test_agent_loop_journal_durability` 4/71 ✓
- `test_agent_loop_resume` 6/88 ✓

## 待 CTest 构建验证（主机锁被 R33-T06 长构建占用，排队中）

- `test_r33_t05_sidecar_recovery`（新，需项目 QGIS/sicnu_data 闭包）
- `test_mission_runtime_store` / `test_mission_context` / `test_mission_stage` / `test_mission_surface_parity` / `test_mission_timeline_surface`
- `test_agent_ops_core`（含既有 delivery/claims/unified report 断言不回归）
- `test_agent_loop_e2e`

## E2E 链映射

- E2E-01（Mission 长历史→分页→新任务→重开）：WP02 腿完成；selection/panel 腿由 test_mission_timeline_surface 复核
- E2E-02（journal 中断→压缩→重开→对账）：WP05/06 腿完成（磁盘往返）
- E2E-03（成功/失败→trace→delivery→export）：WP11/13 腿完成
- E2E-04（A/B sidecar 竞争→故障恢复）：WP08/09 待 CTest
- E2E-05（控制中心完整操作）：未开始（后续批次 WP16+）
