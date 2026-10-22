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

### 第 3 轮（构建 × 排队 + RED 运行取证）
- 全局锁串行实测：r20-01 构建占锁 00:06-02:20（exit 2，其自身测试代码错误，与本轨无关）→ 本轨 02:20 起构建，02:44 达 89% 时 ld 段错误（test_contract_platform_9 链接，工具链瞬时崩溃）；本轨 5 个关键测试二进制已产出。
- **RED 运行取证（pristine master 引擎 + 新测试，全部实锤）**：
  - test_model_runtime：3/24 用例失败（F1 "mixed tile sizes (36x64 vs 64x64)"、F2 mismatchedConfidence==2048、F4 无异常抛出）；100,407/100,410 断言过 → 无预存回退
  - test_model_catalog_v2：3 断言失败（bad-interpolation×2 + bad-resize-token 均 Ready 而非 InvalidManifest）；433/436 过
  - test_workspace_services：3 断言失败（F5 ok==true、error 空、快照目录未清理）；209/212 过
  - test_fault_injection：REQUIRE(second) 失败（F6 nullopt）；156/157 过
- 构建期 GCC 16.2.1 ICE ×4（dwarf2out/qmetatype Debug 崩溃）——make 内部重试自愈，最终不影响产物；ld 一次段错误经重跑恢复。

### 第 4 轮（F1-F6 修复实施）
全部按最小改动落地（详见各文件内 R20-02 注释）：
- F1：提取 flushPendingBatch lambda（allNoData 延迟 + OOM 阶梯 + 进度原样搬移，lastTileIndex 替换 tileIndex/continue→return），几何断点在 push 前比较 tileMat 与 batchMats.front() 尺寸并 flush(tileIndex-1)；to_input 时窗口恒定 → 零行为变化。
- F2：unc 带写出前镜像类别平面的 NoData 恢复循环（单输入带 blendThisHead halo 偏移；多输入 core 尺寸无偏移）。
- F3：model_catalog 全局 + per-input 两处词表校验（interpolation ∈ {bilinear,nearest}；resize 仅 to_input）。
- F4：两 lane 在 writer 创建前 typed refusal（InvalidParameter，output.uncertainty 无 ≥2 通道头）。
- F5：DB 拷贝失败 → error（含 governance）+ removeRecursively + return（与工程文件拷贝分支一致）。
- F6：损坏对象 → 记账回退 + QFile::remove + 递归 put 重排期；stage 后 rename 前 syncFileUtf8 durability 门。
- 增量构建（等 r20-03 释放锁）→ GREEN×2 待跑。

### 观察记录（跨轨锁纪律）
- 04:3x 观测 r20-03/r20-05 存在未持全局锁的并发编译（~227 进程）；本轨全程持锁 -j2，不清理他轨进程（规则禁止）。


## 环境事实
- 全局锁：$GIT_COMMON_DIR/exp-rs-zcode-global-build.lock；-j2；与 r20-01 实测互斥。
- cmake 4.4.4 / Unix Makefiles / Debug / ENABLE_TESTS=ON；系统 GDAL/PROJ/GEOS。
- QT_QPA_PLATFORM=offscreen；ctest 可用于 /usr/sbin/ctest。
