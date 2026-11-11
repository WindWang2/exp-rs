# REVIEW_R1 — R29-T02（独立 reviewer：新上下文 subagent，未参与实现）

审查对象：BASE=130990a396 的未提交工作树 diff + 3 个新增测试文件（构建前静态审查 + 调用链核对，未运行构建——构建锁由实现方持有）。

## 发现与处置

| # | 严重度 | 发现 | 处置 |
|---|---|---|---|
| 1 | **P0** | WP05/06 warp 修复不可达：`DataManager::createVirtualRaster` 仍拒绝 `RequiresReprojection`（data_manager.cpp:1396-1404），两个主打回归测试在第一个 REQUIRE 即红 | **已修**：移除拒绝门（provider 已交付 warp）；canCreate（preflight）继续兜底不可创建形态 |
| 2 | **P1** | `isSameCrs` 无法比较 "EPSG:xxxx" 字符串 vs WKT（importFromWkt 拒绝 authid）→ 相等 CRS 被误判需重投影（回归），新测试自身断言也永假 | **已修**：改用 `importFromUserInput`（raster_grid_compat.cpp） |
| 3 | **P1** | `runOnHostQtThread` cancel-grace 超时抛出后，仍排队的 lambda 捕获已销毁栈帧（UB + 终态 job 迟到重跑副作用） | **已修**：共享状态 shared_ptr 按值捕获 + `abandoned` 原子标记，迟到调用 no-op |
| 4 | **P1** | rotated-case 测试 oracle 量纲错误（投影成 lon/lat 却搜米制 UTM 网格）→ 正确实现也必红 | **已修**：`rotatedPixelCenterUtm`（保持米制），windowContains 注明单位约定 |
| 5 | P2 | (size,mtime) 指纹：`cp -p` 同尺寸保 mtime 替换 → 陈旧命中；postfire 输入未指纹 | **已修**：加 st_ctime（POSIX inode 变化不可伪造）+ postfire 同样指纹 |
| 6 | P2 | relocate 用非 warp 的 snapshot overload 重建 VRT → warp recipe 被静默降级回 relabel 布局 | **已修**：提取共享 `layoutVirtualRasterXml`（含 warp staging），resolve 与 relocate 共用 |
| 7 | P2 | warp 发布非组原子：N 个中间文件逐个就位，中途失败留下旧 VRT + 新中间的混合代际；`-te/-tr` std::to_string 6 位小数量化 | **已修**：recipe-digest 版本化中间名（一代 recipe 固定一组名，成功后清扫陈旧代）+ raster_convert %.17g 往返格式化 |
| 8 | P2 | 跨 CRS 单位无显式分辨率 → 回退用输入单位分辨率 → 1×1 退化产品"成功" | **已修**：preflight targetDiffers 分支 canCreate 需显式分辨率 + provider 拒绝 <1 像素网格 |
| 9 | P3 | marker 降级时 remove 失败被忽略，随后 mismatch-wipe 会毁 journal（违背 R8 承诺） | **已修**：removeEc 失败 → fail-closed 抛错 |
| 10 | P3 | 默认探针接受目录（exists+size>0） | **已修**：加 isFile() |
| 11 | P3 | waitSliceServicingHostEvents 重入暴露（processEvents 期间嵌套 waitForTask/信号处理） | **已修**（文档级）：注释明确"循环顶重读状态、跨切片不持预期"；mutex 语义已正确 |
| 12 | P3 | 无 cancel 时 host lane 等待无界；grace 超时抛 NotInitialized 而非 Cancelled | **已修**：grace 超时改抛 Cancelled + abandoned 标记；无界等待在注释中明确前提（host 服务事件是 lane 的存在前提；#1767 timeoutMs 仍兜底） |
| 13 | P3 | mtime 入身份导致 byte-identical 重写 fork 新 state、旧 marker 累积 | **接受**（记录）：保守指纹的正确代价；scratch 清扫已有机制（cleanupScratchOnSuccess/工作目录清扫）；HANDOFF 记录 |
| 14 | P3 | 窄字符串构造 fs::path（Windows 非 ASCII 路径 stat 失败） | **已修**：`sicnu::portable::pathFromUtf8` |

## Reviewer 确认无误的部分（摘）

- WP02 marker-demote 流程（锁内、journal 保留、遥测签名）；R8 测试真断言 R2 复用（tilesReused==4、computes 不变），非同义反复。
- spectral 发布戳顺序（close 前写 dataset 元数据）、verify 回调 RAII、身份摘要确定性。
- structureFromDataset 重构与原内联块逐字节一致；snapshot overload 对非 warp recipe XML 字节不变（DAT-2 相等断言保持）。
- 接口加法安全（requiresHostQtThread 默认 false；Callbacks/Options 新字段不破坏聚合初始化）。
- cartography 三 operator 标记 + const qobject_cast；produce 内联 compose/export 无嵌套 marshal 死锁。
- 链②③ API 形状（ImportSession 状态机、makeMapSpec、TaskStatus/resultPayload）核对一致；CMake 注册与仓库模式一致。

## 结论

1 P0 + 3 P1，全部处置完毕（12 修复 + 2 有据接受并记录）。修复后需重新构建并跑测试（R2 复核绑定最终 HEAD）。
