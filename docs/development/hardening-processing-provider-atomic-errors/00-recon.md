# hardening/processing-provider-atomic-errors — Recon & 现状矩阵

Baseline: origin/master `a9dc33fa7329a0cf4b40fe838bb7c6177ad2ee01` (post #1236), 2026-09-22.
Worktree: `exp-rs-hardening-proc-atomic`, branch `hardening/processing-provider-atomic-errors`.

## 1. 启动时事实（Phase 0）

- Open PR: 仅 #1237 (`feat/undergrad-lab-cockpit`) — 拥有 `src/teaching/**`、`src/app/**`、根 `CMakeLists.txt`、`tests/CMakeLists.txt`。本任务不与其争抢实现；共享文件只做 append-only 最小 delta。
- Open issues: 0。
- 远端分支 `agent/flash-processing-atomic-errors`（基线 2caac836c4，落后数百提交）有 5 个线索 commit；`rs14-unified-verifier` 是旧平行实现，不复活。
- 历史线索 commit → 当前 master 状态核查：

| 历史 commit | 线索 | master 现状 | 结论 |
|---|---|---|---|
| ec4dfe449f | postProcess(false) 每个 adapter 失败路径恰好一次 | `provider_algorithm_adapter.cpp:451-484` 已在 QgsProcessingException/std::exception/catch(...) 与 cancel 路径全部调用且只调用一次 | 已修复，不移植 |
| 125eefc76c(部分) | cancel 中止 provider 算法 | 同文件 cancel watcher 线程 + typed `Cancelled` 错误 (#1043) | 已修复 |
| 4a18e360b6 | 外部工具 wrapper 对 timeout/cancel/写输出失败可见失败 | gdal/otb/generic wrapper 详查中（见 §3/review-agent 报告） | 待核实 |
| 022fd0c2bf | 短写与 close-time flush 失败可见 | `operators/rs/*` 已用 `closeWithError`；**`processing/algorithms/*` 五个文件族仍吞掉 close flush（§3 F1）** | 部分未修复 → 本 slice |
| 68fcbd4434 | 写失败可见性/取消清理回归测试 | 部分（io 层有 `test_io_atomic_failures`；算法层缺 flush-failure oracle） | 本 slice 补 oracle |

## 2. 权威与调用/所有权图（框架层）

```
用户/GUI/MCP 任务
  └─ TaskCenter (framework/task_center.cpp, 5134 LOC)
       · findOutputPathInParams: 从参数探测 output 路径 → task.outputLayerPath
       · isScratchPath: QDir::tempPath() 或含 ".scratch" → 取消时 unlink
       └─ 成功后 commit: output_committer_task_center.h::commitTaskOutput
            (tempPath=outputLayerPath, stablePath=调用方给定/调度器派生)
  └─ ToolCallDispatcher (framework/tool_call_dispatcher.cpp:113-160)
       · stablePath = "<dir>/<base>_committed.<ext>"，tempPath=算法写的路径
  └─ WorkflowSessionController (app/shell/workflow_session_controller.cpp:635-655)
       · stablePath = resultPayload["output"]（算法自报路径 → isInPlace 注册）

OutputCommitter (framework/output_committer.cpp) — 权威提交器
  validate(GDALOpenEx) → publish-then-swap(.new/.old) → registerSource → 派生记录
  失败回滚恢复 .old；#1174 sidecar 先、主文件后；fault point "output_committer.publish"

atomic_fs (geospatial/util/atomic_fs.cpp) — 权威发布原语
  stagedPathFor (O_EXCL 防碰撞, 扩展名保留) / fsyncFile / publishStagedFile
  (EXDEV 回退) / publishStagedGroup (sidecar 先主后, .bak 回滚) / writeFileAtomic

GdalDatasetWrapper (processing/gdal/gdal_dataset_wrapper.{h,cpp})
  create/writeBandWindow(检查返回) / close()(吞 flush 错,仅 qWarning) /
  closeWithError()(fail-closed) ← operators/rs 全家已用; processing/algorithms 五族未用
```

关键架构事实：**file-based 算法族（processing/algorithms）把结果直接写到调用方给的 `outputPath`**（qgis_algorithms provider 的 `parameterAsOutputLayer` 即用户路径），不经 OutputCommitter 的 temp→stable 流（注释自证：image_fusion.cpp:750-751 "GUI/CLI direct paths bypass the OutputCommitter (#617)"）。因此这一族的原子性与失败语义完全由算法自身负责 —— 这是本 track 的主缺口。

## 3. 现状矩阵 — file-based 算法族输出完整性（主 agent 实证）

| 组件 (文件) | 输出创建 | 失败时 partial 清理 | close-time flush | 旧结果保护 | 备注 |
|---|---|---|---|---|---|
| atmospheric_correction.cpp `processFileMultiBand`(443) `processFile`(578) `processFileDos`(716) | GDAL 直写 outputPath | 检查路径 QFile::remove ✓；异常路径 ✗ | **析构 close() 吞掉 (F1)** | **无：create 即截断旧文件 (F2)** | QUAC 多带流式 (#634/#675) |
| image_fusion.cpp (744) | 同上 | OutputCleanupGuard ✓ (#617) | **析构吞 (F1)**：guard keep=true 后截断文件留下 | **无 (F2)** | |
| image_enhancement.cpp PCA(1705)/MNF(1987) | 同上 | **失败路径无清理 (F3)**：读/写失败 return false 留半成品 | **析构吞 (F1)** | **无 (F2)** | |
| radiometric_calibration.cpp (788) | 同上 | tile 失败 close()+remove ✓；异常 ✗ | **显式 close() 吞 (F1)** | **无 (F2)** | D13 typed seam 在下层 |
| satellite_products.cpp `stackToGeoTiff`(1579 raw createOutputTiff) | raw GDAL | failRemovingPartial ✓ + catch(...) ✓ (#703) | **1873 `GDALClose(outDs); return true;` 不查 flush (F1)** | **无 (F2)** | 元数据/辐射状态标注完备 |

- **F1 (P1)**：close-time flush 失败（磁盘满、配额、NFS 中断）→ GDALClose 后 truncated/corrupt 文件留在 outputPath，算法仍报成功；OutputCommitter 的 isStructurallyOpenable 只验证"能打开"，截断 GTiff 常能打开 → 半成品冒充成功。历史 commit 022fd0c2bf 针对同类问题；`operators/rs` 已修（closeWithError 88 文件），本五族漏网。
- **F2 (P1)**：GDAL Create 直接在用户最终路径上以 truncate 方式建文件 → create 成功后任何失败（含 F1 的静默失败）都毁灭旧的好结果。违背 track 目标"旧结果不丢"。修复方向：entry point 用 atomic_fs::stagedPathFor + 写 staged + fsync + publishStagedFile（复用权威原语，异常时 discardStaged）。
- **F3 (P2)**：image_enhancement 两处失败路径不清理半成品（与同族 image_fusion 的 #617 guard 不一致）。

## 4. 测试/验证基线

- 目标测试已存在：test_output_committer、test_atomic_algorithm_adapter、test_atomic_algorithm_registry、test_provider_algorithm_adapter、test_io_atomic_failures（Catch2）。
- 本任务新增 oracle 计划：
  1. `gdal_wrapper.close_flush` fault point（closeWithError 内 GDALClose 后注入真实 CPLError，仅测试 arm）→ 所有采用方统一可测。
  2. 五族各一 flush-failure oracle：arm 后必须 fail-closed（返回 false+错误信息）、输出文件不得存在；未 arm 时正常出图（防过杀）。
  3. 旧结果保护 oracle：预置旧结果 → arm 失败 → 旧结果字节不变。
  4. 杀伤力证明：对旧实现（还原 closeWithError 调用）跑 oracle 必须 RED。
- 构建：build-dev (Debug, Unix Makefiles, -j2)。窄 target：sicnu_processing + 上述测试。

## 5. 冲突与去重

- 五个目标文件（processing/algorithms）不在 #1237 文件清单中；`git log -5 --` 各文件最近改动均来自已合并 PR（#1223/#1227 等）。
- 共享文件触及：tests/CMakeLists.txt（#1237 也改）→ 仅 append 最小 delta；src/processing/CMakeLists.txt 不动（除非新测试文件需要挂接 — 优先并入现有测试二进制）。
- 提交 PR 前 re-fetch 并复查 open PR 文件清单。

## 6. Review-agent 证据（gdal_tools / otb_tools / generic_cli + 共享 infra）

独立 recon（read-only）确认并新增（均为 master 上读码实证）：

- **[P1] otb_pixel_info.cpp / otb_read_image_info.cpp：5 个 fail-open `return {}`**（OTB 缺失、启动失败、取消、exit≠0 全部报成"成功空结果"）；无 watchdog、无 CrashExit 分类 → 本任务修复（throw + terminate→kill 阶梯 + watchdog + CrashExit）。
- **[P1] gdaltransform.cpp：无 CrashExit 检查**（信号杀死 = exitCode 0 + CrashExit → 当成功，并把部分 stdout 写入 OUTPUT 文件）；取消路径 `return {}`；OUTPUT 写入/关闭不查 → 本任务修复。
- **[P2] provider_algorithm_adapter.cpp catch 块丢失 typed Cancelled**：wrapper 因取消抛异常时被重抛为 generic runtime_error → 本任务修复（catch 块内 `isCancelledFn()/feedback.isCanceled()` 检查）。
- **[P2] gdal_tool_wrapper.cpp stdout-dump 短写**（info/ogrinfo/gdal2xyz 的 FileDestination 落盘只查 open）→ 本任务修复（flush + status 检查，失败删除并走缺失失败路径）。
- **[P2] feature_cube.cpp writeSidecar `write() >= 0` 接受短写；gcp_manager.cpp saveToCsv 无 flush/status 检查** → 本任务修复。
- [P2] wrapper 退出码错误信息丢失 stderr（poll 循环 drain 后 remaining 为空）→ 未修（需跨三个 wrapper 重构 drain 缓冲，见未做事项）。
- [P2] 进程树遏制（WorkerProcessGuard）未用于 provider wrapper → 未修（架构级，见未做事项）。
- [P2] OTB 扩展名猜测可把 `.xml` 当输出；8 个 read-but-undeclared 参数；EXTENT 数组被静默忽略 → 未修（见未做事项；drift 清单已登记）。
- 外部工具直写用户最终路径 + `-overwrite` 默认（warp/ogr2ogr）→ 本任务**未修**，属"provider 目的地走 scratch + OutputCommitter"的架构改造（tool_call_dispatcher 的 `_committed` 命名契约牵连），证据与本 PR 已修复的 file-based 算法族 staging 可作为后续模板。**已列入未做事项分类：需要更大架构改动 / 明确后续 slice。**

历史 commit 终审：ec4dfe449f 大部分已在 master；4a18e360b6 / 022fd0c2bf / 125eefc76c 各有残留（见上）；68fcbd4434 的回归测试线（test_processing_error_paths.cpp）本任务以同名文件重建。

## 7. 未做事项分类（PR 前 final）

- **已被其他 track 拥有 / 架构级（需独立 slice + 与共享 infra owner 协调）**：provider 目的地 scratch 化 + OutputCommitter 发布改造；WorkerProcessGuard 接入三个 wrapper；`_committed` 命名契约演进。
- **已登记证据、修复面过大（本 PR 记录，后续 slice）**：wrapper stderr 丢失重构；OTB 扩展名猜测收紧；8 个未声明参数（schema drift 补声明）；extract_roi/warp/rasterize 的 EXTENT 数组解析；gdaladdo/gdaltindex/gdal_retile 参数修缮；NaN 通过 numeric 校验（schema_validator 无有限性检查）。
- **无法在本机复现（需真实平台/外部环境）**：Windows ReplaceFileW 锁定目标路径为；真实 OTB bundle 行为（本机无 OTB；fake-tool 覆盖进程纪律）；NFS/FUSE 上的目录 fsync 语义（atomic_fs 已 best-effort 设计）。
- **明确未来方向（不属于本 track，不实现）**：任何新 provider/新算法族；worker 协议变更。
