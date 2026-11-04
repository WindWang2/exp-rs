# Issue triage（2026-10-30 实时核实）

| Issue | 标题 | 判定 | 证据 |
|-------|------|------|------|
| #1538 | HistogramEqualize 未拒 ±Inf | FIXED_IN_MASTER_BUT_OPEN | image_enhancement_streaming.cpp:984 已有 `!std::isfinite(v)‖v==nodata` 注释引用 #1538 |
| #1550 | JM 可分性上溢 | FIXED_IN_MASTER_BUT_OPEN | rs_jm_separability.cpp:48-70 已有 logDeterminant() 对数域实现，注释引用 #1550 |
| #1544 | 裸 GDALDatasetH 无 RAII | 待核实（跨多文件，可能跨轨） | 需定位具体调用点是否在本轨写区 |
| #1553 | ChunkGraph::addSink | CROSS_TRACK（runtime 平面，harness 轨更近） | 不接手 |
| #1541 | 检测 lane CV_CN_MAX | CROSS_TRACK（模型 lane） | 不接手 |
| #1631 | src/analysis 技术债汇总 3 项 | 待读 | 本轨写区，读全文后逐项判定 |
| #1646 | src/processing 技术债汇总 6 项 | 待读 | 部分本轨写区，读全文后逐项判定 |
| #1687 | science lane 技术债汇总 | DEFERRED（上下文预算内未逐条读，无既有 SHA 证据不接手） | 留给后续轨 |
| 新发现 | registry 包装算子同进程第二次运行 rs:change_detection 掩膜路径失败 | CROSS_TRACK（runtime 包装层，非本轨写区，复现见 PERF_BASELINE.md） | 不修，记录 |
