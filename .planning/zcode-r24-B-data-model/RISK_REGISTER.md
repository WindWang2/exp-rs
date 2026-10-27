# R24-B RISK REGISTER

| 风险 | 等级 | 缓解 |
|---|---|---|
| 全仓构建耗时过长（QGIS vendor + 680 测试目标） | 高 | 只构建目标测试 target；增量；-j2；OOM 时 -j1 |
| vcpkg manifest 变化导致依赖重装 | 中 | 复用 R23-D 验证过的 toolchain/Qt 组合；binary cache |
| 兄弟轨（A/C）同时开工造成 tests/CMakeLists.txt 冲突 | 高 | 只 append 自带标记块；推送前 fetch+检查开放 PR；merge-integrity 门 |
| GPU/大权重不可用 | 确定 | tiny ONNX/确定性伪权重仅作测试；真实无 GPU 场景测优雅降级（任务授权） |
| 重做已合并工作的诱惑 | 高 | ISSUE_TRIAGE 全表 FIXED_IN_MASTER；新改动必须先证明 master 上可复现差异 |
| Windows 独占实跑 | 确定 | Linux/macOS 标 NOT_EXECUTED，不虚报 |
| 性能测量受宿主噪声影响 | 中 | 5 次重复+方差报告；不做紧阈值 |
