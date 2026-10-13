# 风险登记

| 风险 | 等级 | 缓解 |
|------|------|------|
| isStale 语义翻转(missing owner⇒stale)影响未知调用方 | 中 | 已审计全部 8 个调用点: 全部传 this/self.data() 且先注册; nullptr 路径不变 |
| interpreter 探测在无 Python 机器上拖慢测试 | 中 | 测试环境有 pinned SICNU_PYTHON_EXECUTABLE 短路; 探测仅在 unpinned 时运行 |
| JobEngine executor 注册影响同 binary 其他测试 | 低 | test_guided_workflow_widget 其他用例不提交任务; 前缀唯一 |
| MSVC/Qt6 编译差异 | 中 | 只用 Qt6 公共 API (QProcess::exitStatus 等) |
