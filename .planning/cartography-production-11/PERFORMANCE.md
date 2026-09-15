# PERFORMANCE — cartography-production-11

- 资源硬约束：build -j2（RSS>70% 降 -j1）、ctest -j1、QT_QPA_PLATFORM=offscreen。
- 负载观测手段：Git Bash 无 loadavg；构建期每 60s 用 `tasklist` 采样 RSS（至少首末各一次 + 抽样），不可测时如实记录。
- 逻辑规模界（D-013）：produce/atlas/series 页数 ≤512；循环全部有界可取消；manifest 大小 O(pages)。
- 不以 wall-clock 为 correctness gate；规模证据 = 页数/字段数/钳制断言。
- 实测：待填（首次构建采样）。
