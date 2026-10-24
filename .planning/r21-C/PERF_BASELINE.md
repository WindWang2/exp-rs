# R21-C PERF_BASELINE — 性能与资源记录

## 构建资源（本轨实际）
- configure：44s（vendor+覆盖配方）
- 全目标构建（49 目标含 qgis_core/gui 及全部测试二进制）：经共享锁 -j2；峰值 RSS/时长 <构建完成后回填>
- 测试：-j1 全程

## 受影响路径的性能论证
- W-A/W-B（CLI 参数拒绝）：每命令 O(n_leftover) 线性扫描，n<10；无新分配热点。拒绝路径复用既有 four-tuple finish 层。
- W-D9（token 文件读）：1MiB cap+1 读 + 一次 JSON parse；token 实际 ~200B。审批是低频人工操作。
- D5（pid 快照）：一次 atomic load 替代普通指针读，x86-64 上 lock-free；零新增同步。
- D6（有界读）：read(cap+1) 替代 readAll()/rdbuf()——大文件场景内存从 O(file) 降到 O(cap)；正常小文件无差异。
- F1（cancel 门）：提交工具名两个 QString 比较/次调用。
- F2（parameters 门）：一次 typeId() 比较。
- F3（terminate 失败短路）：仅在终态误用路径生效。
- F4（pi 钳制）：纯 JS Math.min/max。

## 基准（同条件对比，待回填）
- CLI 冒烟（--help/错误路径）耗时 master vs HEAD：<回填>
- capability/knowledge reload（大目录）耗时与 RSS：<回填>
- plugin 宿主 crash-recovery 往返：<回填>
