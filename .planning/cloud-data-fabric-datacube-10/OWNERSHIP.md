# OWNERSHIP — cloud-data-fabric-datacube-10

## 本 track 拥有（新建/独占写入）

* `src/geospatial/fabric/**` — 10.0 新模块（object store seam、catalog service、
  virtual cube、chunk plan、query planner、prefetch/mirror）
* `tests/test_io_fabric_*.cpp` — 本 track 新测试
* `tests/support/` 内本 track 新增 fixture（只新增文件，不改既有 fixture）
* `docs/io/fabric-10.md`（新增文档）

## 本 track 窄改动（共享文件，append-only / 最小 hunk）

* `src/geospatial/CMakeLists.txt` — fabric/ 源文件注册（append 到 SICNU_GEOSPATIAL_SOURCES）
* `src/geospatial/remote/**` — 仅当 prefetch/mirror 需要挂钩点时的最小扩展；不重构既有实现
* `src/operators/io/**` — 新算子 additive（io:catalog_search / io:cube_plan /
  io:cube_window / io:cache_prefetch）；不改既有算子行为。候选例外：F-OPS-4 死参数
  修复（io:reproject），见下方裁决记录
* `src/operators/io/io_operators_init.cpp` — 新算子注册行
* `tests/CMakeLists.txt` — 测试目标注册（append-only）
* `src/cli/cli_commands.cpp/.h` — `data catalog` / `data cube` / `data cache prefetch`
  additive 子命令
* `CHANGELOG.md` — 末尾一条
* `.gitignore` — planning 白名单三行（已提交）

## 明确不拥有（只读）

* `src/geospatial/stac/**`、`src/geospatial/catalog/**`、`src/geospatial/multidim/**`、
  `src/geospatial/hints/**`、`src/geospatial/identity/**`、`src/geospatial/util/**`、
  `src/geospatial/raster/**`、`src/geospatial/crs/**`、`src/geospatial/doctor/**`、
  `src/geospatial/formats/**`、`src/geospatial/cog/**` — 前代权威实现，只复用
* `src/app/**`、`src/data/**` — 桌面 UI（workbench track）
* `src/processing/framework/**`、`src/workflow/**` — 调度权威（concurrency track）
* `src/agent/**` — agent runtime（harness track）
* `data/help/**` — help 治理归 help track；本 track 算子的 schema 真值由测试断言
* `src/geospatial/products/**` — 产品适配器（#956 刚落地）

## 与其它 10.0 track 的并发边界

* 若他 track 正在改 `io_operators*` / `cli_commands*` / `tests/CMakeLists.txt`：本 track
  的改动保持 append-only、独立 integration commit，rebase 时按 union 解决。
* 若他 track 未来需要 chunk/plan 接口：本 track 只承诺稳定 `fabric/` 头文件契约，
  不提前实现对方业务逻辑。
* F-OPS-4 裁决：Phase 6 时点若 master 仍无 io:reproject 修复且 rebase 干净，本 track
  以独立 commit 窄修复（消费已声明参数 + 既有 review 测试移植），记入 DECISIONS.md；
  若出现冲突迹象，保持 OUT_OF_SCOPE 不动。

## 共享文件冲突表

| 文件 | 本 track 改法 | 潜在冲突方 |
| --- | --- | --- |
| `src/geospatial/CMakeLists.txt` | append 源行 | 其他 geospatial track（低概率） |
| `tests/CMakeLists.txt` | append 测试目标 | 任何新测试 track（高概率，union 解决） |
| `src/operators/io/io_operators_init.cpp` | append 注册行 | 其他算子 track（union 解决） |
| `src/cli/cli_commands.cpp` | additive 分支 | CLI track（窄 hunk） |
| `CHANGELOG.md` | 末尾一条 | 所有 track（末尾追加，冲突天然最小） |
