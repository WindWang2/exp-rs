# R22-03 Risk Register

| # | 风险 | 等级 | 缓解 |
|---|------|------|------|
| R1 | 与 #1673 同文件（tile_inference_engine.cpp）merge 冲突 | 中 | hunk 不重叠；push 前重查 #1673 head；语义差异已在 PR_COLLISION_MATRIX 论证 |
| R2 | per-slot 权重使 accumulator 内存 ×2（slots×span×W floats） | 中 | 窗口有界（tile+2·halo 行）；修复注释明确记账；C0 测试覆盖 48 行滑窗；不做无界分配 |
| R3 | 共享构建机资源争用（R21/R22 兄弟轨） | 中 | 全部 build/ctest 走 `/tmp/exp-rs-r21-build.lock` 串行，-j4 硬上限 |
| R4 | github.com 直连超时（fetch/push 网络故障） | 中 | gh API 已验证可用；push 前重试；失败保留阻塞证据与恢复命令，绝不谎报 PR |
| R5 | vendor GDAL/Qt 伪失败（R21 已报告 wrapper2_/PCRE2 类问题） | 低 | 复用 r21-B 已验证 configure 参数；环境红/基线红/本轨回归分开记录 |
| R6 | 无 CUDA GPU | 确定 | GPU 实测标 NOT_EXECUTED；不虚构 GPU 基准 |
| R7 | 测试注册块与兄弟轨在 tests/CMakeLists.txt 尾部追加顺序冲突 | 低 | 本轨插入点在 test_model_runtime_9 之后（非文件尾）；push 前 fetch 复查 |
| R8 | 权重语义变更影响既有单通道 golden | 低 | 单通道 slots=1 行为不变（每 slot 权重=原 m_weight）；test_model_runtime_9 回归双跑证明 |
