# CROSS_TRACK_FINDINGS — R25-C

## 发现（不需他轨改动的，本轨已闭环）
1. **pi/test r18_c 双 flake（预存）**：`r18_c_timeout_cancels_server_side`（跨进程 append/读竞态，机制已证，本轨已修 bounded poll）；`r18_c_id_null_error_fanout`（机制未定，负载相关，单跑 0/25、基线 2/8 复现）。两者在 pristine master 均复现 → 非本轨回归。
2. **recoverAtStartup 丢弃恢复的 runs（W3 背景发现）**：恢复的 run 不入 coordinator 内存 map（workspace runs list 看不到），但 resume_workflow 从 checkpoint 直接工作 → 本轨以「只读诊断面」补齐可发现性（get_workflow_status run_id 路径），未改恢复语义（所有权裁决保留在 resumeRun）。若未来希望 runs() 列出恢复的 runs，属 workflow owner 轨决策。
3. **rs_obia_hierarchy 的 prerequisites 是字符串非数组**：`AgentMetadata::fromJson` 只解析数组 → 该 operator 的前置条件被丢弃。文件属 A 轨（src/operators/rs/）→ 登记 CT-A1，本轨不改。

## 给他轨的交接（无阻塞项）
- D 轨：workspace charts/MapSpec 本轨未触碰；W3 的新增键只加在 workflow 状态输出，不动 chart/layer referent。
- B 轨：data/model runtime 未触碰。

## 共享文件变更登记
- tests/CMakeLists.txt：末尾追加 `# BEGIN R25-C`…`# END R25-C` 块（2 个测试目标）。
- .gitignore：末尾追加本轨 planning allow-block。
- tests/surface_diff_snapshot.json：将随 get_workflow_status schema 变更再生（协议要求的合法漂移，pi/C++ 双侧钉会验证）。
