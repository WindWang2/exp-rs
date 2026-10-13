# R9 Wave1 测试计划（C1 五项）

| 项 | 复现测试(red) | 验证(green) | 目标 |
|----|---------------|-------------|------|
| C1-1 | test_faultlab_params_r8.cpp 新增: duplicate 无 train / relocate 缺角色 时 grid.extras 不变 | ctest -R faultlab_params | 拒绝路径零变异 |
| C1-2 | test_guided_workflow_widget.cpp 新增: 提交异步 job 后 Next disabled, 终态后恢复 | ctest -R guided_workflow_widget | 按钮状态=任务态 |
| C1-3 | test_scan_pool.cpp: 更新 #1453 用例期望 + 新增 pruning 压力用例 | ctest -R scan_pool | 销毁 owner 的 worker 永久 stale |
| C1-4 | test_teaching_admin_script_adapters_r8.cpp: interpreter 探测可运行性 | ctest -R script_adapters | 拒绝 Store alias |
| C1-5 | 同上: 截断传播 outputTruncated | 同上 | 截断不可伪装完整 |

编译: cmake --preset dev-default && cmake --build build-dev --parallel 2 --target <targets>
CTest: QT_QPA_PLATFORM=offscreen ctest --test-dir build-dev -R <regex> --output-on-failure -j1
