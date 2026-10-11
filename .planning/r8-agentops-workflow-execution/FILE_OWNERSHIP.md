# R8 File Ownership Matrix

R8 独占（本轨可直接修改）:
- src/agent/**（除 view_control_service.* —— #1469 已改，本轨不动，除非 rebase 后必要）
- src/agent_ops/**
- src/agent_loop/**（journal 修复）
- src/workflow/**
- src/processing/framework/task_center.*, local_worker_pool.*, output_committer.*
- src/recipes/**
- src/agentbench/**
- src/app/agent_ops/agent_ops_control_center_dock.*（事先声明独占）
- src/runtime/chunk/scratch_registry.*（#1459 C4 digest 流式化，最小改动）

与 #1469 重叠文件（#1469 head c8c463d22 已改，rebase 时语义对照）:
- src/agent/mcp_server.cpp（#1469 改了 handleRunWorkflow recording 块；本轨 O1 在同
  邻域实现超集修复；O2/O3/O4/O5 在同文件其他区域 —— #1469 未触及）
- src/agent/llm_streaming_client.*、src/agent/view_control_service.*（#1469 已修，本轨不动）
- src/agent_ops/operations_coordinator.*、task_center_executor.cpp（#1469 已修，本轨不动，
  除非发现相邻缺陷）
- src/processing/framework/task_center.cpp（#1469 修了 cancel/submit race P1；本轨 O7/O9
  在其他函数）
- src/workflow/workflow_checkpoint.cpp（#1469 修了 bounded parse；本轨 O13 archive rename）
- src/workflow/workflow_run_coordinator.cpp（#1469 修了 terminal-save retention；本轨 O11
  IO under mutex）

共享 harness（细粒度 diff，仅追加必要行）:
- tests/CMakeLists.txt 及各测试目录 CMake

依赖轨（只读边界）:
- 轨2 数据模型（dataset/experiment store）、轨4 UI 壳：跨轨 API 变更保持最小。
- #1469 5 个 P2 review threads（model_runtime/plugin_registry/ipc_channel/
  radiometric_state/external_process）非本轨文件，不处理，PR 中说明。

冲突优先级: mcp_server recording 块 = 本轨 O1（超集实现优先）；
其余重叠文件以 #1469 已合并内容为基线，本轨 patch 只做增量。
