# ISSUE_TRIAGE — R25-C（执行时实时核验）

核验方式：`gh issue view`（2026-10-28 实时）+ 当前 master `2610cb4741` 生产代码逐条比对 + 回归测试定位。

| Issue | 标题摘要 | 状态判定 | 证据（master 代码/测试） | 本轨动作 |
|---|---|---|---|---|
| #1690 [P1] | ExternalProcess env key 含 `=` 绕过 LD_PRELOAD 拒绝表 | **FIXED_IN_MASTER_BUT_OPEN** | `src/sdk/exprs/external_process.cpp:105-135` 已拒绝含 `=`/NUL/空 key（带 #1690 注释）；钉：`tests/test_r22_agent_env_keys.cpp` | 维持回归钉绿；不重修 |
| #1704 [P1] | MCP 沙箱放行 GDAL 驱动前缀连接串（CSV:/GPKG:/WMS:/PG:） | **FIXED_IN_MASTER_BUT_OPEN** | PR #1714 修复（r22-agent 轨）；钉：`tests/test_r22_agent_containment.cpp` | 同上 |
| #1691 [P2] | PythonPluginAdapter 超时不撤销 py: 幽灵算法 | **FIXED_IN_MASTER_BUT_OPEN** | PR #1714；钉：`tests/test_r22_agent_ghost_algo.cpp` | 同上 |
| #1692 [P2] | PythonIpcServer 静默丢弃 >1MiB 响应行 | **FIXED_IN_MASTER_BUT_OPEN** | PR #1714；钉：`tests/test_r22_agent_ipc.cpp` | 同上 |
| #1705 [P2] | JobEngine logLines 无界增长 | **FIXED_IN_MASTER_BUT_OPEN** | PR #1714（bounded retention + drop accounting）；钉：`tests/test_r22_agent_job_logs.cpp` | 同上 |
| #1654 [P3] | src/workflow 技术债（1 项） | **PARTIAL（具体债项需对照）** | WorkflowRunCoordinator 成熟（flock/状态墙/finalize in-flight 等待）；本轨 W3 发现的缺口（恢复后 runs 不可发现）即其延伸 | W3 已实现 run_id 诊断面 |
| #1640 [P3] | src/jobs 技术债（1 项） | **NOT_REPRODUCIBLE（无具体代码缺口证据）** | JobEngine 当前实现含 bounded logs/typed failures；#1705 修复吸收主要债务 | 不动 |
| #1628/#1629/#1693 [P3] | src/agent / agent_ops 技术债汇总 | **PARTIAL** | 汇总型 issue；本轨 W1/W2/W3 均为 agent/harness 债务的实质清偿 | 代码即交付 |
| #1650 [P3] | src/sdk 技术债（2 项） | **NOT_REPRODUCIBLE**（无法定位具体未修项；无最小复现） | — | 不动（不虚构修复） |

## 结论
- 无 LIVE（当前 master 仍可复现）的 P1/P2 缺陷落在本轨领域；五个 R17-agent 安全缺陷已修但 issue 仍 open（属流程性 open，不重修、不代关）。
- 本轨新增价值来自纵深缺口（W1 漂移不可观测、W2 前置条件丢弃、W3 恢复诊断缺失），非重复修复。
