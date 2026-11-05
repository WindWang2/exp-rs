# HANDOFF — R28-T02 → 相邻轨

## → T04（Qt/QGIS 工作台）

1. **#1765 GUI 侧**：`src/app/dialogs/stac_browser_dialog.cpp:244-265` 仍以 `GDALOpenEx(裸 /vsicurl/ href)` 打开 STAC COG。后端权威 API 已稳定：
   ```cpp
   const auto r = sicnu::geo::remote::safeRemoteRasterPath( href.toStdString() );
   if ( !r ) { /* UI 展示 r.code() + r.message（typed：SchemeRefused/OfflineRefused/
                 PrivateTarget/CacheUnavailable/GuardUnavailable） */ }
   else      { GDALOpenEx( r.path.c_str(), ... ); }
   ```
   验收建议：对话框断网/私网目标/未装 cache 三态显示 typed 原因；绝不回退裸 /vsicurl/。
2. **ImportSession 分步驱动**（本轨 WP03 新增 API）：UI 批量导入面板用 `checkSteps(n)/proceedSteps(1)/hasPendingItems()/requestCancel()` 以 QTimer(0) 链驱动（参考 tests/test_r28_t02_import_session_runtime.cpp 的 driveStepwise 驱动器），queued cancel 在 ≤1 item 内生效；`summary()`（sicnu.import.session/1）直接渲染进度/拒绝原因；`verifyFacts()` 可随时刷新"已导入文件被替换/移动"警告。

## 集成注意（R1 P2-2，面向所有轨）

- `geo::fabricCachedPath(remoteHttp)` 在 range cache 未装时从"返回裸 URL"改为 typed GeoError(InvalidArgument)。仓内调用方已审计（chunk_plan/prefetch/virtual_cube/mirror/cli 全部捕获或已装 cache）；**任何轨道外新增调用方必须先 `RemoteRangeCache::install`**。回滚=单文件 revert object_store.cpp。

## → T03（Agent/SDK）

- 取消/失败原因词汇：TaskCancelReason（User/Upstream/Shutdown/StructuredJoin/Engine）与 isTransientExecutionError 前缀为稳定契约；WP01 分类若发现通知语义缺口会在此更新。

## → T05（workflow/实验）

- ImportSession 的 journal schema sicnu.import.session/1 新增可选 `identity_token` 字段（加法兼容；旧 journal 诚实降级）。
- CLI 新增 `data preflight <paths...>`（批次体检 JSON，schema sicnu.import.preflight.batch/1）——workflow/教学侧可作导入预检统一入口。

## → T01（算法）

- 无新增接口；#1740 家族（scale/offset 语义）已在 WP07 roundtrip pin 中锚定 reader 侧事实。
