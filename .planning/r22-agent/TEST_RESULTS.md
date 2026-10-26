# R22-04 (r22-agent) TEST_RESULTS

基线 origin/master = 911e0b17a3533ca0d79c1b68b000d6052c4c3c61；head = 本分支。
构建：build-r22-agent（Debug, ENABLE_TESTS=ON, vendor GDAL/PROJ/GEOS/GSL/netCDF + 系统 sqlite3 + R21-C 同款 link-shim；共享锁 /tmp/exp-rs-r21-build.lock 内 -j4）。
运行环境：`LD_LIBRARY_PATH=<build>/link-shim:<vendor-prefix>`、`QT_QPA_PLATFORM=offscreen`、ghost/ipc 族 `SICNU_PYTHON_EXECUTABLE=/home/kevin/oracle-venvs/fusion/bin/python3`（系统 python3 无 numpy，daemon 无法启动——见 RISK_REGISTER）。

## RED（master src + 本轨测试的 master 兼容变体；全部 exit 42）

| 测试 | 结果 | 证据 |
|---|---|---|
| test_r22_agent_containment | 1/2 例败，50 断言中 25 败（CSV:/GPKG:/WMS:/PG:/未知前缀全部放行） | evidence/RED_containment.log |
| test_r22_agent_env_keys | 3/4 例败（`LD_PRELOAD=/x.so ` 作为 key 时子进程照常启动） | evidence/RED_test_r22_agent_env_keys.log |
| test_r22_agent_job_logs | 2/4 例败（25002 行全保留；1 MiB 单行原样存储） | evidence/RED_test_r22_agent_job_logs.log |
| test_r22_agent_ipc | 1/2 例败（>1MiB 响应 5s 内无回调=挂起复现） | evidence/RED_test_r22_agent_ipc.log |
| test_r22_agent_ghost_algo | 1/1 例败（超时加载后 py:ghost_slow_register 仍在注册表） | evidence/RED_test_r22_agent_ghost_algo.log |

## GREEN ×2（双跑，退出码与断言数逐遍一致）

| 测试 | Pass1 | Pass2 |
|---|---|---|
| test_r22_agent_containment | exit0，64 断言/2 例 | exit0，64/2（一致） |
| test_r22_agent_env_keys | exit0，46/4 | exit0，46/4 |
| test_r22_agent_job_logs | exit0，24031/4 | exit0，24031/4 |
| test_r22_agent_ipc | exit0，17/2 | exit0，17/2 |
| test_r22_agent_ghost_algo | exit0，7/1 | exit0，7/1 |

日志：/tmp/r22a-green-p{1,2}-*.log（本机临时区，关键数值已录入本表）。

## 受影响既有族复跑（修复后单跑；关键项见 GREEN×2 的五目标双跑）

| 测试 | 结果 |
|---|---|
| test_mcp_server | exit0，9686 断言/31 例 |
| test_job_engine | exit0，446/34 |
| test_task_center（delta 消费端） | exit0，533/34 |
| test_r14_C_external_process_env | exit0，23/2 |
| test_exprs_external_process | exit0，83/13 |
| test_runtime_python_channel_r4 | exit0，126/12 |

## KNOWN_BASELINE_FAIL（与本轨无关，A/B 实证）

- test_exprs_plugin_loader：16/38 例败（TrustRejected 族）。**A/B**：将 src/sdk/exprs/plugin_manifest.cpp 单独回退到 master 重编译重跑，同样 16 例败 → 本机信任链/签名环境基线问题，非本轨回归。

## NOT_EXECUTED（构建车道门控）

- test_python_plugin_host / test_python_plugin_manager / test_plugin_host：注册于 `if(SICNU_EMBED_PYTHON)` 块内，本车道 SICNU_EMBED_PYTHON=OFF（与 R21-C 相同车道）。替代覆盖：#1691 由 test_r22_agent_ghost_algo（真实 worker_daemon 子进程 + AlgorithmEngine python_plugins provider 全链路）承担；#755 成功卸载路径代码未被本轨触碰（unload() 既有撤销逻辑原样）。
