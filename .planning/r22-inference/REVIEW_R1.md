# R22-03 REVIEW R1（独立反对式审查）

- 审查对象：commit `b55aa5fd9a`（对比 parent `911e0b17`）
- 审查者：独立 subagent（与开发上下文完全分离，general-purpose agent，无编辑权限，仅只读分析）
- 审查轴：数学正确性 / 滑窗机制 / 行为外溢 / 资源与异常 / 性能 / 测试质量 / 仓库卫生
- 逐项结论（原文摘要）：
  - Axis 1 混合数学：**clean**（三处 finalize 的 per-slot 归一化正确；m_invalid 跨 slot 共享符合 NoData-wins 设计；无残留交叉污染；resetWindow 仅构造器调用，slot 数量不可漂移）
  - Axis 2 滑窗：**clean**（2·slots+1 同几何 Mat 的 slideUp 偏移一致；写侧 CV_Assert 与读侧 clamp 不变；deferred 路径未触及）
  - Axis 3 行为外溢：C=1 位相同；**[P3]** derived 对"部分定义栈" fail-closed（数据→NoData）是无测试钉住的语义变化（旧行为本就是垃圾）；单类+uncertainty 的 Mask/Confidence 数值会变（属修复生效，无测试）
  - Axis 4 资源：**[P3]** 窗口内存 (slots+2)→(2·slots+1) 缓冲（bounded，注释已声明；极端 C=21/tile512/halo128/rasterW25000 ≈ 3.3GB——follow-up 可考虑半精度账本）；异常安全 clean（CV_Assert 异常由引擎 catch-all abandon；finalizeRows 抛点在 slide 之前）
  - Axis 5 性能：**[P3]** 内层 per-class ptr<float> 可行 hoist 优化（量级：噪声级）；slideUp 体量 O(window)/tile-row，immaterial
  - Axis 6 测试质量：seam 权重/	margin 数学/两半缺陷独立钉住 均手工复核正确；**[P3]** 缺 Mask 用例、classMapping remap、divergent-finiteness；**[P3]** NoData 用例的 NoData 行部分在未修复树也会过（由 VALID 行提供 RED）
  - Axis 7 卫生：**clean**（diff 恰为 6 类文件；CMake 纯 append；.gitignore 白名单 scoped）
  - out-of-diff 观察：**[P3]** 多 head derived+uncertainty+feather 疑似 slot 越界崩溃
- **VERDICT: APPROVE**

## R1 后处理（开发方）

- out-of-diff 疑点**证伪**（本人源码复核）：derived+uncertainty 在 :1056 被显式拒绝（"output.uncertainty requires the full probability stack"），derived+多 head 在 :1060 被拒绝；probability 模式 slot 数=writerBands=totalBands(+1 uncertainty) 完整覆盖 `bandOffset+c`（bandOffset 为逐 head 累计）与 `uncertaintyBandOffset=totalBands`。多 head+uncertainty 的 feather 只在 Probability 模式可达，构造一致，无越界。
- P3 测试补强已落实（commit `1381e5d80a`）：Mask 折叠、classMapping remap、poisoned-band（probability per-slot 诚实 + derived fail-closed NoData 255）。7 用例 1203 断言双跑全过。
- P3 性能 hoist：不落实（量级为噪声，避免为性能微扰语义关键路径；记入 PERF_BASELINE N/A 说明）。
- P3 内存：保留 bounded 窗口设计，注释已声明；半精度账本记为 follow-up 候选（FINAL_REPORT）。
