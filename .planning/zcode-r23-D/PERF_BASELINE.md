# PERF_BASELINE — R23-D

## 结论：NO_MEASURABLE_BENEFIT 类改动（无性能声明），逐项 N/A 理由

本 PR 的三组生产改动均不处于任何热路径，也没有做性能优化声明，因此不设 before/after 基线：

| 改动 | 性能影响面 | N/A 理由 |
|---|---|---|
| session_store.cpp readJsonFile 补 try/catch | 会话文件加载路径 | 零开销路径（C++ 异常仅在真正抛出时付费；合法会话文件不触发）。该路径生产调用频率为每会话加载一次，非批量热路径 |
| spec_runtime.cpp expected_numeric isfinite 门 | LabSpec 解析路径 | 两个 std::isfinite 分支（内联 FPU 比较），解析本身是每文档一次；微基准无意义 |
| ndvi_landsat.rules.json 新规则文件 | 无代码改动 | 纯数据；评分成本与既有 ndvi_basics 规则同阶（同为 5 断言、256×256 窗口化评分） |

实测旁证（非声明，仅记录）：
- test_r23_d_ct6_lab_flow 三个用例端到端（foundry 生成 256×256×2 件 + NDVI + 两次评分 + 跨跑对照 + 负控克隆）合计 ~0.9s（ctest Passed 0.87s；第二轮 0.72s；R1/R2 复跑同量级）
- test_r23_d_lab_hardening 两用例合计 <0.1s（0.05s + 0.03s）
- 评分窗口化预算契约（maxBytes 64MiB）未触碰；#1602 的 readFileBounded 峰值内存修复在本轨回归矩阵中绿（test_verify_adapters exit 0）

资源记录（协议 §5 要求的测量上下文）：16 核主机、31.2GB RAM（构建期 free ≥6.8GB 持续实测）、build-D 隔离目录、-j2 默认（协议条款下短暂 -j4）、CTest --parallel 1。
