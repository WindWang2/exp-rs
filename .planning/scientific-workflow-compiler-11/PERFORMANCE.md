# PERFORMANCE — 资源模型 / 逻辑规模 / 实测

- 原则：规模证据用内存上限、操作数/队列上限、逻辑规模与可复现 invariant；不用 wall-clock 当 correctness gate。
- 构建硬上限：CMAKE_BUILD_PARALLEL_LEVEL=2 / Ninja -j2（高载降 -j1）；测试 CTEST_PARALLEL_LEVEL=1 / -j1；QT_QPA_PLATFORM=offscreen。
- 逻辑规模锚点（实现后填充）：facts 上界（irLimits 风格单表）、probe 字节/超时预算、explain 8KiB、projection bounded keys、corpus ≤400 cases。
