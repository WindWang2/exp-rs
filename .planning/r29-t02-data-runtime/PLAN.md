# PLAN — R29-T02 工作包

优先级：WP02（B1 假绿根除）> WP05/06（B3 warp）> WP09（B5 线程）> WP01（基线证据）> 其余按证据。

## WP01 基线复现（先行，为 WP02 提供 RED）
六假设各自独立取证（独占 scratch `/tmp/r29t02-*`，不动共享 /tmp/sicnu_operators）：
1. H-A 首次运行失败？→ 预期正常产出。
2. H-B 相同目标重复运行 → 复用（产物在，合法 cache hit）。
3. H-C 删除输出保留 marker → **预期假绿（缺陷）**：run 成功 + 无文件。
4. H-D 同源路径内容替换（identity 不含内容指纹）→ 取证。
5. H-E 跨 workdir（不同 cwd/工作区）→ identity 含 outputPath 后应无假绿；取证。
6. H-F 恢复时注册记录丢失（marker 在、DataManager 无记录）→ 取证 publishedAssetId 恢复路径。

## WP02 published claim 统一验证（B1 核心）
框架级：`ResumableTileRun::execute()` marker 命中时调用可选 `publishedOutputValid` 回调；
不可证明 → 仅移除 marker（保留 journal/tiles，R2 复用），正常重建产物。
- `chunked_run.cpp runResumable`：提供默认验证（commitSpec->stablePath 存在+可打开（GDAL raster）或文件非空），operator 可覆写。
- spectral index：alreadyPublished 时按 change-streaming 契约重开输出验证身份（输出元数据 SICNU_* 身份戳）。
- CLI `registerOutputAsset` warning → 保留（防御性），但源头不再产生该状态。
- 测试：`tests/test_r29_t02_published_claim.cpp`（H-C/H-D RED→GREEN）。

## WP05 preflight 判定对齐（B3 前半）
- 输入彼此同 CRS 但显式 targetCrs 不同 → RequiresReprojection（canCreate=true，由 WP06 交付 warp）。
- 旋转/剪切输入 + VRT 无法表达 → RequiresReprojection 或明确拒绝（有原因、可执行替代）。
- tiny oracle 数据：equivalent WKT、北向/旋转、半像元 origin、不同分辨率、union/intersection、NoData 边缘、多源无交叠。

## WP06 受控 warp（B3 后半）
- `buildVirtualRasterXml` 路径：需要重投影（target≠input CRS 或旋转源）时，经 `geo::warpRaster` seam 把每个输入 warp 到目标网格的 staged 中间文件，再以同 CRS 轴对齐矩形组 VRT。
- 中间文件与 VRT 同目录、确定性命名（recipe 派生），resolve() 每次重建。
- 分类/标签 → NearestNeighbour 契约保持（preflight 已拒绝连续重采样+分类输入）。
- 序列化 recipe 重开 → 结果一致（确定性 warp）。

## WP09 制图 host-thread 契约（B5）
- `RSOperator::requiresHostQtThread()`（默认 false）；cartography produce/export/compose → true。
- JobEngine：op 需 host 线程且 QCoreApplication 存在 → queued invoke 到 app 线程 + worker 信号量等待（非 BlockingQueuedConnection），cancel 轮询；无 QCoreApplication → 原路径（operator requireQgisHost 诚实拒绝）。
- `TaskCenter::waitForTask/waitForPipeline`：app 线程等待切片改为 processEvents（bounded），使 host-lane 可被服务。
- `requireQgisHost` 增加线程检查（QThread::currentThread == app thread）→ 消费契约。
- 验收：SICNU_CP11_THREAD_PARITY=1 全表面字节一致（offscreen）。

## WP03/04/07/08/10 —— 按 WP01-11 证据推进，优先真实缺口
（详见任务文件；本轮以 B 表为准取舍）

## WP11 三条链
① CLI NDVI → 删产物 → 重跑恢复；② 混合导入 → 取消 → 重启恢复 → 虚拟目标网格 → 项目重开；③ TaskCenter 制图 → 真实内容导出 → 取消/失败不污染 → 再成功。
