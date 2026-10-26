# fix(r22-inference): #1706 多类别羽化拼接 per-slot 缓冲与权重账本

**Base**: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（origin/master @2026-10-25 gh api 实测）
**Head**: `agent/r22-inference-20261025T132554Z`（3 commits：`b55aa5fd9a` 修复 → `1381e5d80a` R1 P3 测试补强 → `2d5191447e` R2 注释修正）

## 缺陷（#1706，P1，正常输入下结果损坏）

`FeatherAccumulator`（`src/operators/runtime/tile_inference_engine.cpp`）两处叠加错误：

1. **槽位别名**：`m_sum.assign(n, cv::Mat::zeros(...))`——cv::Mat 拷贝是引用计数浅拷贝，n 个槽位共享同一缓冲，后写覆盖先写；
2. **权重重复记账**：每 tile 的羽化权重在 `add()` 中按槽位累加（每类别一次，C 次）。

任何 C>1 的 feather 推理输出全部损坏：概率波段全部 = 类别均值（RED 实测全波段 0.3333 = mean(0.8,0.1,0.1)）、argmax 全图 class 0（Labels 右半全 0）、uncertainty 槽 = (1+margin)/4。既有羽化测试均为单通道（C=1）故未暴露。

## 修复

每 slot 独立 `m_sum[s]` + `m_weight[s]`：band s 恰被"对该 band 贡献过有限值的 tile 集合"归一（R2 用反例确证全局分母会伪造概率：poison 场景 x=18 处全局分母得 0.0128，per-slot 正确值 0.1）；derived 折叠（Labels/Mask/Confidence）对部分定义栈 fail-closed 到 NoData，不再 argmax 半定义栈。C=1 行为与旧实现位相同（既有金样例零回归）。

## 改动清单

- `src/operators/runtime/tile_inference_engine.cpp`：仅 FeatherAccumulator 类体（~:693-1006）与其文档注释；调用点零改动；runMultiInput 不可达（显式拒绝 Feather，R2 确证）
- `tests/test_r22_inference_feather.cpp`（新）：7 用例/1203 断言——probability 三类独立（seam 余弦已知解 1e-3）、Labels argmax 奇偶 + classMapping remap、Mask 折叠、48 行高栅格滑窗隔离、NoData 全波段、uncertainty 槽独立（softmax margin 精确解）、poisoned-band per-slot 诚实 + derived fail-closed
- `tests/CMakeLists.txt`：test_model_runtime_9 块后纯 append 新目标注册
- `.gitignore`：`.planning/r22-inference/` markdown 白名单（各轨先例模式）
- `.planning/r22-inference/*.md`：BASELINE/ISSUE_TRIAGE/PR_COLLISION_MATRIX/REVIEW_R1/R2/TEST_RESULTS 等

## 关联 Issues

- **Closes #1706**
- #1554 / #1553：**已在 master 修复**（`d9f1b886f1`，本轨源码级验真归档于 ISSUE_TRIAGE.md——gpu_plane.cpp:44-64 析构持锁、chunk_graph.cpp:92-104 addSink 守卫），issue 仍 OPEN，可由维护者核实后关闭

## RED→GREEN 证据（双跑）

| 套件 | RED（未修复树） | GREEN PASS1 / PASS2 |
|------|----------------|---------------------|
| test_r22_inference_feather | 5 用例全败/39 断言失败（0.3333/0.31314 特征） | 1203 断言 ×2 全过 |
| test_model_runtime_9（M5/#r18B 单通道金样例） | — | 1226 断言 ×2 全过 |
| test_model_runtime | — | 110429 断言 ×2 全过 |
| test_r14_B_tile_engine | — | 20695 断言 ×2 全过 |

## 性能

正确性修复，无正式基准（PERF_BASELINE.md N/A）。定性：slideUp 体量 O(window)/tile-row；内存 +（slots−1)×span×rasterW×4B（有界于 tile+2·halo 行窗口，非全栅格；注释与文档已声明）。

## 双独立 Review

- **R1**（独立 subagent，只读反对式 7 轴）：**APPROVE**，0×P0-P2；4×P3 全闭环（测试缺口→已补；内存→文档；ptr hoist→噪声级不落实；out-of-diff 疑点→源码证伪 :1056/:1060 守卫拒绝 derived+uncertainty/多 head）。
- **R2**（另一独立 subagent，明确不复用 R1 结论、独立重推全部数学含 python 复算）：**APPROVE**，0×P0-P2；1×P3 注释已修，1×P3 备忘（derived 死分支，CROSS_TRACK X4）。

## 与 R21/兄弟 PR 冲突排查

- **#1673**（r21-B，open）：同文件 `tile_inference_engine.cpp` 但 hunk 不相交（其 head-role threshold 门控在 :493/:1726/:3779 等，本轨 FeatherAccumulator 类体 :693-1006）；语义无重叠（thresholding vs blending）。head `941af0d18` 已复核。
- **#1708**（r22-infra，open）：仅 `.gitignore` 双方尾部 append（白名单段不同，语义无碰撞）与 `tests/CMakeLists.txt`（注册块位置不重叠）；无 src 交集。
- #1674/#1675/#1676/#1677：无文件交集。
- 详见 `.planning/r22-inference/PR_COLLISION_MATRIX.md`。

## 未执行项（如实申报）

- **CUDA/GPU 实测 NOT_EXECUTED**（本机无 NVIDIA GPU）。本修复层为设备无关纯数学累积，CPU fake-runtime 测试覆盖全部逻辑。
- 线上 CI 不参与完成门：**本 PR 不等待 CI**。

## 遗留风险

- 窗口内存随类别数 +1 份账本（X5，有界）；极端 C=21 大栅格算例见 PERF_BASELINE。
- derived+uncertainty 组合仍被 manifest 守卫拒绝（既有语义，非本 PR 引入）。
