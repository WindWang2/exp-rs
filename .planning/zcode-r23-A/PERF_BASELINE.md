# r23-A PERF_BASELINE

本轮默认无性能主张(修复为语义/数值正确性,结构性开销与 std::isfinite 相同阶)。
若审计选出性能热点(>=15% 目标)则按 baseline/profile→单点优化→复测补充于此;未达目标将如实说明。
