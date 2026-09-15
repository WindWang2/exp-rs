# PERFORMANCE — multimodal-registration-11

资源模型（GOAL 硬约束）：build `CMAKE_BUILD_PARALLEL_LEVEL=2`（高负载降 `-j1`）、test `CTEST_PARALLEL_LEVEL=1`。
规模证据用内存上限/操作数/逻辑规模，不用 wall-clock 当 correctness gate。

（待填：各算法复杂度、cap/降级策略、实测 RSS）
