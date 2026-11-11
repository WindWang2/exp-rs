# HANDOFF — R29-T02 → 相邻轨

## 接口变化（加法兼容，无需对方改动即编译）

1. **`RSOperator::requiresHostQtThread()`**（`src/operators/framework/rs_operator.h`，默认 false）——执行线程契约。JobEngine 对返回 true 的 operator 在 dispatch 期把 run() 经 queued invocation 投递到 Qt application 线程执行（worker 以信号量等待，非 BlockingQueuedConnection）。T03/T04 新 operator 若需要 GUI/QGIS 渲染线程，override 返回 true 即可；无 QCoreApplication 的宿主自动走原路径（operator 自行诚实拒绝）。cartography:compose/export/produce 已标记。
2. **`ResumableTileRun::Callbacks::publishedOutputValid`**（可选回调，`src/runtime/chunk/resumable_tile_run.h` R8）——PUBLISHED marker 命中时先验证声明产物；返回 false + reason → 仅删 marker 降级重建（journal/tiles 按 R2 复用）。`ChunkedRunOptions::verifyPublishedOutput`（`chunked_run.h`）是 kit 层入口；commitSpec 运行默认验证 stablePath（存在+非空+非目录）。
3. **`providers::layoutVirtualRasterXml(recipe, snapshots, vrtPath, errCode, errMsg)`**（`src/data/providers/virtual_raster_source_provider.h`）——虚拟栅格唯一布局入口（含 WP06 warp staging）。resolve() 与 relocate 共用；`buildVirtualRasterXml` snapshot overload 保留（非 warp recipe XML 字节不变）。
4. **`isSameCrs` 现接受 authid 字符串**（"EPSG:4326" vs WKT 语义比较）——修复跨形制误判；所有调用方受益。

## 行为变化（可能影响其他轨的观察点）

- **rs:spectral_index 家族**：published 缓存命中现在要求产物可重开 + `SICNU_SPECTRAL_IDENTITY` 戳匹配 + 几何一致；否则自动重建（#667 自愈语义上移）。运行身份新增 `input{Bytes,Mtime,Ictime}`（+postfire 同名）指纹字段——同路径内容替换强制重算。`already_published` 计数诚实。
- **虚拟栅格**：输入彼此同 CRS 但显式 targetCrs 不同 → RequiresReprojection（需显式分辨率）且真实 warp 到目标网格（不再仅换 SRS 标签）；旋转/剪切输入同。跨单位无分辨率 → 明确拒绝（不再 1×1 假成功）。`DataManager::createVirtualRaster` 不再拒绝 RequiresReprojection。
- **TaskCenter::waitForTask/waitForPipeline**：在 Qt application 线程上等待时按切片 processEvents（GUI 主线程不再饿死 host lane）；其他线程行为不变。重入语义：见 task_center.cpp 注释。
- **warp -te/-tr 数值**：%.17g 往返格式化（原 6 位小数量化）；`geo::warpRaster` 调用方无感知。

## → T01（科学算子）

- 身份指纹模式（size+mtime+ctime 进 canonicalParams + 产物 SICNU_*_IDENTITY 戳 + verify 回调）可直接复制到其它 resumable 算子；rs_change_streaming 保持其 R28 fail-closed 语义未动。

## → T03（Pi/MCP/Harness）

- TaskCenter host lane 使 cartography:* 经 MCP/GUI 宿主的 TaskCenter 提交可获得真实渲染产物（H1 修复）；isolated worker 进程无 QgsApplication 时 operator 仍诚实拒绝（typed NotInitialized，文案含线程原因）。
- 取消词汇：host lane 的 grace 超时抛 `ErrorCode::Cancelled`（原 NotInitialized 语义废弃）。

## → T04（Workbench/教学）

- B5 验收：`SICNU_CP11_THREAD_PARITY=1` 现在应全表面字节一致（修复落地）；`skipIfHostRenderBroken` 的旧 WARN 分支保留（不破坏）。教学链 ① 的 D-T05-CHAIN1 false-green 已消除——chain1_teaching.sh 可移除 `/tmp/sicnu_operators/chunked-*` 清理 hack（保留亦无害）。
- 数据管理 UI：ImportSession API 未变；虚拟栅格创建面板现在可以安全暴露 targetCrs/targetResolution（preflight 会给出准确 verdict 与可创建性）。

## 已知限制（接受并记录）

- 同路径 byte-identical 重写会 fork 新 chunk 状态（保守指纹代价）；旧 `.published` marker 依赖既有 scratch 清扫。
- warp 中间文件为 VRT 同目录 `<vrt>.warp<i>-<digest8>.tif`；recipe 代际更替后旧代被 best-effort 清扫，进程中途死亡可能残留（无害、可再生成）。
- host lane 无 cancel 时等待无界——前提是宿主服务事件循环；#1767 timeoutMs 兜底（见 job_engine.cpp 注释）。
