# R22-02 (r22-data) OWNERSHIP — 写入域与禁区

## 本轨独占写入（经任务书授权）

- `src/geospatial/remote/**`（offline_gate、http_fetch、range_cache、range_cache_disk、remote_source_validator、process_network_policy、probe_remote、vsi_object_identity 等）
- `src/geospatial/util/atomic_fs.*`
- `src/geospatial/raster/raster_reader.*`（仅 #1703-① validateWindow 溢出；② readMask 死代码处置在本轨债务项内）
- `src/data/**`（artifact_object_pool 等）
- `src/dataset/**`（sample_promotion、dataset_store —— 后者与 #1673 有 hunk 级相邻，见矩阵）
- `src/experiment/**`（仅数据归档与血缘；本轮不动）
- `tests/test_r22_data_*.cpp`（新增）+ 既有相关测试文件的**追加**（test_io_remote_validator / test_io_range_cache / test_io_atomic_failures / test_dataset_core 等，仅加 case，不改既有断言语义）
- `tests/CMakeLists.txt`（仅 append-only 注册新测试）
- `.planning/r22-data/**`

## 只读禁区（03/04/05 轨与全局）

- `src/operators/runtime/**`（03 轨模型 manifest 域，只以公开 API 消费）
- `src/agent/tool_catalog/**`（04 轨）
- 桌面 UI / `src/gui` / `src/app`（05 轨）
- 其他 open PR 的 head 分支、他人 worktree、`master` 本身

## 与历史 PR 的关系（只读参照）

- #1673（R21-B，open）：其 write-set 与本轨交集 = `src/dataset/dataset_store.cpp`（不同函数域）+ `tests/CMakeLists.txt`（append-only）。本轨不 cherry-pick、不覆盖其未合并修复。
- #1669/#1667（R20-02/R19-01，已合并）：本轨在其成果之上（artifact_object_pool 自愈逻辑为 #1669 引入，#1700/#1701 涉及文件为 R19 域引入），只做增量。
- #1527（安全下载防线，已合并）：#1679 是其防线的绕过修复，方向一致、不回滚。
