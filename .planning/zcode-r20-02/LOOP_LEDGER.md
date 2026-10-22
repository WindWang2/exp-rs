# R20-02 LOOP_LEDGER — 迭代账本

循环协议：RECON → PLAN → RED/PROBE → MINIMAL FIX → GREEN×2 → INDEPENDENT REVIEW → FIX → REVIEW → REBASE → PR
规则：每 3 轮同因失败强制换假设；所有结论附命令与退出码。

## 轮次记录

### 第 1 轮（RECON + 分诊）
- origin/master == 28e1891390（无新提交）；open PR 0；open issue 126。
- worktree `exp-rs-r20-02-data-model-runtime-20261022T160551Z` 建立；configure rc=0。
- 事故与纠正：首次 configure 误在主仓生成 build-r20-02（shell cwd 重置），**已删除**，主仓 status clean 实证；此后全部绝对路径。
- swarm 静态分诊：本轨域 P1 12 条全已修、P2 抽查全已修、P3 汇总抽查全已修（见 ISSUE_TRIAGE.md）。运行时回归待构建后。
- 深度审计：两个只读子代理（B05+B04 持久化 / B09+B11 推理运行时）返回候选；本人逐条实读源码核实，6 项成立（F1-F6），4 项降级 RISK（A1/A4/A5/A6/B5）。

### 第 2 轮（RED 测试编写，等待构建）
- 全局锁被并行轨 r20-01 持有（00:06 起，flock 实测排队）→ 本轨 build-r20-02 排队。
- RED 测试 6 个全部追加进既有注册测试文件（零 CMake 改动）：
  - test_model_runtime.cpp：F1（100×100/tile64/batch4 混批）、F2（uncertainty NoData NaN）、F4（uncertainty 无头拒绝）
  - test_model_catalog_v2.cpp：F3（interpolation/resize 词表，并入 #646 表驱动用例 +3 期望）
  - test_workspace_services.cpp：F5（DB 拷贝失败→ok=false；rename+mkdir 触发，root 免疫）
  - test_fault_injection.cpp：F6（损坏对象再 put 自愈）
- 计划：构建完成后先跑 6 测试取 RED（此时引擎源码仍为 pristine master），再实施 F1-F6 修复，增量重建，GREEN×2。

### 第 3 轮（构建/RED/修复/GREEN）
（进行中——待构建完成回填）

## 环境事实
- 全局锁：$GIT_COMMON_DIR/exp-rs-zcode-global-build.lock；-j2；与 r20-01 实测互斥。
- cmake 4.4.4 / Unix Makefiles / Debug / ENABLE_TESTS=ON；系统 GDAL/PROJ/GEOS。
- QT_QPA_PLATFORM=offscreen；ctest 可用于 /usr/sbin/ctest。
