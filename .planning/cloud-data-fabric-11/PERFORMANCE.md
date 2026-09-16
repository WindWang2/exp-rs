# PERFORMANCE — cloud-data-fabric-11

## 资源模型（host 事实，2026-09-16）

- Windows 11 host：16 逻辑核 / **31.2 GB** 内存。
- **宿主为多 track 共享**：Phase 1 期间同时观测到 8 个 ninja（兄弟 11.0 track：teaching-lab、
  spectral、workflow、cartography、contracts 等）+ 本 track 的 1 个，共 ~11 个 cl.exe。
  空闲内存曾低至 1.1 GB（非本 track 独占造成）。本 track 遵守：**同一时刻只跑一个构建，
  ninja -j2 上限**（envelope 规则），不 kill 他 track 进程。
- 本 track 编译 RSS 采样：单 cl.exe 130–160 MB（Debug, /Ob0）；两路并发 ≈ 0.3 GB — 在
  -j2 上限内安全。测得 LoadPercentage 多次为 100（多 track 叠加）。
- 本机 Git Bash 下 load average 不可测（envelope 允许）：以任务管理器等价采样
  （Get-CimInstance LoadPercentage + FreePhysicalMemory + cl.exe 计数）替代，逐次记录。

## 构建记录

| 项 | 命令 | 结果 |
|---|---|---|
| configure（首次成功） | track-cmd.cmd（见 D-1106） | Configuring done (521.1s) / Generating done (93.3s) |
| reconfigure（新增源文件后） | 同上 | Configuring done (506.7s) + Generating done |
| sicnu_geospatial | `cmake --build build-dev --target sicnu_geospatial -j 2` | exit 0（仅 2 个既有 prefetch.cpp 警告：C4189 budgetExhausted、C4101 error） |

## 逻辑规模（设计上限；逐项证据见 TEST_MATRIX）

- chunk 枚举：u64 计数 + `materializeChunks(begin,maxCount)` 有界窗口 — million+ chunks
  不物化全计划（10.0 契约保留；11.0 补 multidim 侧证明）。
- manifest 快照缓存：上限 8 个目录条目（超出整体清空 — 重放进程只需少数目录）。
- MirrorReport.chunks：新增 1024 条保留上限 + outcomesDropped 计数（11.0 修复：10.0 无上限）。
- range cache：内存 LRU 字节预算 + 逐块校验（disk 层 SHA-256 trailer 既有）；
  对象条目键 = canonical + 16 hex 凭据上下文指纹（64 bit 分离度）。
- prefetch/execute：遥测 delta 预算语义保留；mirror 命中跳过保留。
