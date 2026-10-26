# R22-04 (r22-agent) LOOP_LEDGER

格式：轮次 | 假设/改动 | 文件 | 测试命令 | 真实结果 | 下一步

- R0 | 启动盘点+worktree+规划文档 | .planning/r22-agent/* | gh/git 命令实录 | origin/master=911e0b17a3 与任务书一致；5 个 R21 PR open；6 个 agent issue 全部 OPEN_UNFIXED | 开始 M1-D0a #1704
- R1 | configure build-r22-agent（R21-C 配方） | build-r22-agent/ | cmake -S . -B build-r22-agent ... | 退出码 0（见 /tmp/r22a-configure.log） | 构建 RED 基线测试目标
- R2 | #1704 修复：classifyReference 增 GDAL 驱动前缀识别（文件驱动提取内嵌路径递归 containment/网络驱动走 remote 默认拒/未知驱动危险 payload 拒绝），新增 DriverPath kind | src/agent/tool_catalog/workspace_containment.h | tests/test_r22_agent_containment.cpp（待构建运行） | 代码完成，RED/GREEN 待构建 | 构建 RED 基线
- R3 | #1690 修复：dangerousEnvironmentKey 拒空/含'='/NUL key（E5007）+ PluginManifest 解析期 env key 字符集门 | src/sdk/exprs/external_process.cpp plugin_manifest.cpp | tests/test_r22_agent_env_keys.cpp | 代码完成，待构建 | 同上
- R4 | #1691 修复：initialize 失败路径 unregisterRegisteredAlgorithms + ~AppInterfaceBridge 兜底 | src/python/isolated/python_plugin_adapter.cpp app_interface_bridge.* | tests/test_r22_agent_ghost_algo.cpp | 代码完成，待构建 | 同上
- R5 | #1692 修复：oversized 行 256B 前缀提取 id 立即回错+dropInFlight；daemon 共享发送点 >1MiB 回 typed error(-32001) | src/python/isolated/python_ipc_server.cpp src/python/scripts/worker_daemon.py | tests/test_r22_agent_ipc.cpp + py_compile | 代码完成，待构建 | 同上
- R6 | #1705 修复：appendLog 有界（4KiB/行截断 + 10k 行块驱逐 + droppedLogLines + delta cursor 补偿）；#1693-2 终态日志回调早退；#1693-1 StdinReader 死分支删除 | src/jobs/job_types.h job_engine.h job_engine.cpp src/agent/mcp_server.cpp | tests/test_r22_agent_job_logs.cpp | 代码完成，待构建 | 同上
- R7 | configure 第二次（补 GDAL_INCLUDE_DIR/GDAL_LIBRARY）被共享构建锁阻塞（r22-science 先持有） | build-r22-agent | - | 等待锁 | 锁释放后 configure+build RED
- R8 | 构建环境落地：GDAL/PROJ/GEOS/GSL/netCDF 覆盖 + SQLite→系统 .so（vendor .a 无 -fPIC）+ R21-C 同款 link-shim（wrapper2_* 等 26 符号） | build-r22-agent | cmake reconfigure ×5 | EXIT=0 | RED 构建
- R9 | RED 全五项实锤（master src + RED 变体测试）：containment exit42(25断言红)/env_keys exit42(4红)/job_logs exit42(25002>10000, 1MiB行未截断)/ipc exit42(挂起)/ghost exit42(幽灵残留, 需 SICNU_PYTHON_EXECUTABLE=fusion venv numpy) | evidence/RED_*.log | 见 evidence | 全部 RED | 恢复修复进 GREEN
- R10 | GREEN 第一轮修复三处：①env 空键返回标记串（原来返回空串被当干净）；②JobEngine delta 改绝对索引空间（dropped+local，驱逐不再重编号；终态通知也走 delta，防 TaskCenter 终行丢失）；③ipc 测试 takeInFlightRequests 语义修正（它 retire 全部 pending）。job_logs 断言两处笔误修正（1-based offset、7001） | src/sdk/exprs/external_process.cpp src/jobs/job_engine.cpp tests/* | 5 目标全绿 | exit0 | 双跑
- R11 | GREEN×2 双跑：containment 64 断言 / env_keys 46 / job_logs 24031 / ipc 17 / ghost 7，两遍退出码与断言数完全一致（/tmp/r22a-green-p{1,2}-*.log） | 5 目标 | 双跑 exit0 一致 | 通过 | 回归族复跑
- R12 | 回归族复跑：mcp_server 9686/31 ✅、job_engine 446/34 ✅、r14_C_env 23/2 ✅、exprs_external 83/13 ✅、task_center 533/34 ✅、runtime_python_channel_r4 126/12 ✅；exprs_plugin_loader 16 例失败经 A/B（回退 master plugin_manifest.cpp 同样 16 败）判定为 KNOWN_BASELINE_FAIL（本机信任链/签名环境）；test_python_plugin_host/manager/plugin_host 族被 if(SICNU_EMBED_PYTHON)（=OFF，R21-C 同车道）门控 → NOT_EXECUTED，替代覆盖=ghost_algo 真实 daemon 测试 | /tmp/r22a-reg-*.log | 见左 | 回归闭环 | 提交+R1
