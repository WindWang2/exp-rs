# R24-B BASELINE — 数据资产、EO 模型推理与实验可复现

- **UTC 时间**: 2026-10-27T02:38:46Z（侦察开始）；worktree 创建 2026-10-27T02:25:45Z
- **Base SHA**: `5fe642ec6339807823aa56612ba94668592a738d`（= `origin/master`，含 PR #1717 合并）
- **Track branch / worktree**: `agent/r24_b-20261027T022545Z` @ `C:\Users\wangj.KEVIN\projects\exp-rs-r24_b-20261027T022545Z`
- **本地主仓状态**: 主工作树在 `ad98a7a70`（落后 origin/master，干净；唯一未跟踪文件 `data/samples/manifest.json` 只读保留）。存在兄弟 R23-D worktree `../exp-rs-D-20261026T165151Z`（PR 已合并，build-D 目录存在，未触碰）。

## 远端快照（2026-10-27T02:30Z 实测）

- `gh pr list --state open` → **0 个开放 PR**（无并行轨在途冲突）。
- 远端分支：`master` + 2 个已合并 r23 残留（`agent/r23-d-lab-grading-evidence-…`、`agent/r23-e-platform-plugin-release-…`）。
- 最近合并：#1717 (r23-D LabSpec/Grader/FaultLab, 2026-10-27T02:00Z)、#1716 (r23-E plugin/worker/delivery, 01:56Z)、#1715 (r22 conflict-marker fix)、#1714 (r22-agent)、#1713 (r22-workbench)、#1712 (r22-science)、#1711 (r22-teaching)、#1710 (r22-data: SSRF/RangeDiskBlockStore 几何键/原子发布/provenance)、#1709 (r22-inference 多类别羽化)、#1708 (r22-infra 三平台构建)。
- 开放 Issue ≈90 个，全部为 R17 sweep / 技术债清单时代的存量；对 R24-B 所有权区域逐条代码验证见 `ISSUE_TRIAGE.md`。

## 环境实测

- Windows 11 (26200)，8 核/16 逻辑，RAM 32 GB（侦察时空闲 ≈9.4 GB → 按 `-j2` 纪律；不满足 `-j4` 的 ≥12GiB 空闲条件）。
- 磁盘 C: 余 663 GB。MSVC 2022 (14.38.33130)，Ninja，CMake（VS 内置），vcpkg toolchain `C:/deps/vcpkg`，系统 Qt `C:/deps/Qt/6.8.0/msvc2022_64`（复用 R23-D 验证过的配方）。
- 本轨 build dir：worktree 内 `build-r24_b`（RelWithDebInfo，ENABLE_TESTS=ON），与兄弟 build 目录隔离。

## 实读路径（侦察代理实际读取的文件）

- `src/geospatial/`: `remote/range_cache.cpp|h`, `remote/range_cache_disk.*`, `remote/offline_gate.cpp`, `remote/remote_source_validator.*`, `util/resource_uri.cpp`, `util/atomic_fs.cpp`, `formats/format_profiles.*`, `products/*`, `metadata/canonical_metadata.*`, `cog/cog_presets|cog_validator.*`, `stac/stac_client|stac_mapper.*`
- `src/data/`: `governance/workspace_service.cpp`, `governance/governance_store.*`, `data_manager.*`, `workspace_catalog.*`, `artifact_object_pool.cpp` 等
- `src/dataset/`: `dataset_store*.cpp`, `dataset_store_impl.h`, `fold_audit.cpp`, `leakage_audit.cpp`, `split.*`, `dataset_fingerprint.*`, `sample_catalog.*`, `foundry_service.*`, `sample_promotion.cpp`
- `src/experiment/`: `experiment_types.cpp`, `experiment_store.cpp`, `replay_deviation.cpp`, `repeat_execution.cpp`, `benchmark_runner.cpp`, `reproduction_bundle.cpp`, `reproduction_bundle_import.cpp`, `lineage.*`, `capsule/*`, `run_recorder.*`, `run_bridge.*`
- `src/operators/runtime/`: `model_runtime.*`, `tile_inference_engine.cpp`, `detection_tile_engine.cpp`, `detection_fusion.*`, `detection_postprocess.*`, `eo_preflight.*`, `device_planner.*`, `model_execution_service.*`, `model_publish.*`, `provenance_verify.*`, `onnxruntime_provider.cpp`, `openvino_provider.cpp`, `tensorrt_provider.cpp`, `opencv_dnn_runtime.*`, `python_worker_provider.*`
- `src/runtime/`: `chunk/chunk_graph.cpp`, `gpu/gpu_plane.cpp`, `exec/execution_governor.cpp`, `observability/trace.cpp`, `observability/execution_telemetry.cpp`
- `src/operators/python/opencv_type_caster.h`, `src/operators/rs/rs_continuum_removal_operator.cpp`
- `tests/CMakeLists.txt`（680 个 add_executable；注册模式 `# --- <track> registration ---` 块 + `sicnu_discover_tests`）
- `scripts/check_merge_registration_integrity.py`（存在于本 worktree HEAD；主工作树缺失因其落后）

## 关键结论

1. 23 个 R24-B 相关候选 Issue **全部 FIXED_IN_MASTER**（sweep commits `987161c25` `9b42bc93f` `d9f1b886f` `831f8d600` `c8ba51990` `8e0430262`，均为 HEAD 祖先；R22 #1710/#1709 修复实存）。**禁止重做已合并工作**。
2. 真实残余缺口（代码注释/合同自认或测试空缺）：
   - `reproduction_bundle.cpp:418-435` lineage 切片只存远端点、丢方向 → importer 无法重挂边，导入 run 成为 lineage 孤岛（#1581 修复仅"命名缺口"未闭合）。
   - `resource_uri.cpp` literal-'%' 文件名解析无正例回归（#1579 后遗）。
   - `workspace_service.cpp:314-317` ghost-reconcile `removeAsset` 仍 `(void)` 丢弃（低危）。
   - `execution_telemetry.h:144` 注释仍写 "push_back + erase-front"（陈旧文档）。
3. R24-B 的实际工作量转向：深挖热点文件的**未开 Issue 的 LIVE 缺陷** + WP-B1..B9 的体系化测试（metamorphic/E2E/故障注入/性能），以 O-B0..O-B5 为验收。
