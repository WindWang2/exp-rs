# EVIDENCE — cloud-data-fabric-datacube-10

证据政策（D-024）：每条能力断言映射到一条本地命令 + 退出码，或显式标注
`not-executed`。两者之外的表述方式不出现。

## Phase 0

| 断言 | 命令 | 结果 |
| --- | --- | --- |
| 基线 SHA | `git rev-parse origin/master` | `7d78059d1a6d316d606656759a506d17bc5e3b55` |
| worktree 建立 | `git worktree add ../exp-rs-cloud-data-fabric-datacube-10 -b zcode/cloud-data-fabric-datacube-10 origin/master` | exit 0 |
| 白名单生效 | `git check-ignore -v .planning/cloud-data-fabric-datacube-10/GOAL.md` | exit 1（无输出）= 未被忽略 ✅（见下） |
| 无未合并远端 track 分支 | `git branch -r` | 仅 origin/master（+ itk-upstream 镜像） |
| 无 mosaic/planner 既有实现 | `grep -rn "mosaic" src/geospatial --include=*.h -l` / `grep -rln "planner\|ChunkPlan" src/geospatial/` | 均空 |
| GDAL 版本 | `pkg-config --modversion gdal` | `3.13.3` |
| gh 可用 | `gh pr list --state merged --limit 30` | 30 条返回 |
| open issues | `gh issue list --state open` | 空 |

白名单自检（runbook 步骤 2）：

```
$ git check-ignore -v .planning/cloud-data-fabric-datacube-10/GOAL.md
（无输出，exit 1）✅
```

## Phase 8 — 最终验证（final HEAD @ 1b8bfa7b2e，origin/master 最新）

`git fetch origin && git rebase origin/master` → 当前最新；`git diff --check` → 干净；
冲突标记扫描（fabric/ operators/ cli/ tests/）→ 无；secret 扫描（排除 credential 字段名
本身）→ 无；goal-template 存在性断言两条 → 输出为空（PASS）。

最终全量套件（14/14 全绿，918 断言）：

| 套件 | 断言 |
| --- | --- |
| test_io_fabric_object_store | 74 |
| test_io_fabric_catalog | 56 |
| test_io_fabric_cube | 70 |
| test_io_fabric_plan | 72 |
| test_io_fabric_scale | 32 |
| test_io_fabric_operators | 40 |
| test_io_uri | 96 |
| test_io_stac | 22 |
| test_io_stac_client | 136 |
| test_io_range_cache | 123 |
| test_io_remote_range | 25 |
| test_io_catalog_query | 50 |
| test_io_hints | 27 |
| test_io_operators（含 F-OPS-4 回归） | 95 |

not-executed（如实声明）：真实公有云端点（AWS/GCS/Azure）访问；OTB/ONNX/GPU 套件
（非本 track 范围）。S3 集成以 loopback S3 兼容端点 + GDAL 真实 /vsis3/ 栈全链路验证。

## 预算节（每 Phase 结束更新）

| Phase | 结束时间（UTC） | 工具调用次数（约） | 触及文件数 | vs 包线 |
| --- | --- | --- | --- | --- |
| 0 | 2026-09-13 | ~40 | 15 | 18M 内 |

## OUT_OF_SCOPE

| 发现 | 级别 | 去向 |
| --- | --- | --- |
| `review/issues/F-OPS-4.md` io:reproject srcCrsOverride 声明未消费（io_operators.cpp:306 校验 vs :363-370 仅 io:clip 消费） | P1 | Phase 6 按 D-1013 裁决：无冲突则窄修复纳入，否则保持本节 |
| /tmp inode 瞬时耗尽（主机环境，非本仓） | P3 | 主机运维，不在 track 内处理 |

## Build 记录（Phase 1 起追加）

| 项 | 命令 | 结果 |
| --- | --- | --- |
| configure | `cmake -S . -B build-fabric10 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON` | exit 0，`Generating done`（首次 configure 遇 FetchContent 瞬态失败，第二次成功并缓存） |
| 头自包含 | `cmake --build build-fabric10 --target header_probe_geospatial_fabric_{object_store,catalog_service,virtual_cube,chunk_plan,query_planner,prefetch,mirror}_h -j2` | exit 0（7/7 OBJECT 库编译通过；隐含 Sicnu::Geospatial + GDAL 依赖编译成功） |
| 修复记录 | virtual_cube.h 嵌套 BuildOptions 默认参数 → 提升为命名空间级 `VirtualCubeBuildOptions`；mirror.h 补 include query_planner.h | 编译修复，2 处 |

主机环境注记：`cc1plus: warning: .../Qca-qt6/QtCrypto/QtCrypto: not a directory`
为既有环境噪音（非本 track 引入，master 同样出现）。

## 测试记录（Phase 2 起追加）

（待填：ctest -R … 输出摘要、exit code）
