# TEST_RESULTS — R25-C

## 已完成（真实命令与 exit code）
- W1: `node --test pi/test/`（worktree pi/ 下）→ 45 pass / 0 fail，exit 0，两轮（2026-10-28）。
  - 首轮含 r18_c_timeout 修复（bounded poll）后全绿。
  - 预存 flake 备注：r18_c_id_null 在基线（stash 后）2/8 全套件运行复现；单文件 0/25 不复现；与本轨 diff 无关（同形失败在 pristine master 出现）。
- 基线构建：dev-default preset，configure exit 0；sicnu_geo_rs+test_mcp_server+r22×5+test_workflow_runtime 后台进行中。

## 待构建完成后运行（命令模板）
- ctest --test-dir build-dev -j1 -R 'test_r25_c_capability_honesty' （×2）
- ctest --test-dir build-dev -j1 -R 'test_r25_c_workflow_resume_diagnostics' （×2）
- ctest --test-dir build-dev -j1 -R 'test_mcp_server|test_r14_C_mcp_workflow_cancel' （邻域回归）
- ctest --test-dir build-dev -j1 -R 'test_r22_agent_(containment|env_keys|ghost_algo|ipc|job_logs)' （W4 回归钉）
- ctest --test-dir build-dev -j1 -R 'test_workflow_(runtime|run_coordinator|recovery|crash_recovery_r4)' （W3 邻域）
- EXP_RS_MCP_BIN=build-dev/surface_mcp_host node --test pi/test/r25_c_e2e_real_host.test.mjs （×2，W7）
- ninja -C build-dev surface_snapshot_regen → surface_diff_snapshot.json 再生提交
