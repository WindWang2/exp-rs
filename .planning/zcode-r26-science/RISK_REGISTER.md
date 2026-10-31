# RISK_REGISTER — R26-science

| 风险 | 级别 | 缓解/状态 |
|------|------|-----------|
| S2 capped 路径候选级内存（100 Mpx ROI ≈ 1.2GB@3 波段） | P2 | 画像已记录 PERF_BASELINE；无 NoData 时与历史逐位一致；彻底方案留给后续 perf 轨 |
| S10 依赖 #1728 领地的 isGeographicCrs / MathUtils 弧长助手 | P2 | 只读依赖；#1728 若改语义将影响 S10 面积换算——已在 PR_BODY 跨轨节声明 |
| tests/CMakeLists.txt 与 #1724-#1728 同点尾部追加 | P2 | 唯一 append 块 + BEGIN/END 标记；冲突时保留双方块即可 |
| registry 包装算子同进程第二次运行 rs:change_detection(makeMask) 失败 | P3 | CROSS_TRACK（runtime 包装层 commitSpec/workspace 状态），复现见 PERF_BASELINE.md；不在本轨写区 |
| S8/P2-7 连通性变化影响既有 OBIA 矢量化消费方 | P3 | golden E2E + obia_segmentation + object_hierarchy 回归全绿；8 连通与分割器对象定义对齐 |
| OpenCV 版本依赖：S11 契约守卫的成败随 copyTo 实现而变 | P3 | 已判 UNREPRODUCED 并记录探针证据；守卫即为此类变化而设 |
| 未测矩阵 | — | GPU/ONNX/Windows/MSVC 未执行（NOT_EXECUTED）；Release 构建 perf 对照未跑；master 对照基线未跑 |
