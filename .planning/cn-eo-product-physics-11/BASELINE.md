# BASELINE — cn-eo-product-physics-11

Phase 0 只读审计原始记录（2026-09-15，主仓库 `C:\Users\wangj.KEVIN\projects\exp-rs` 执行）。

## Git / GitHub 事实（启动时刷新）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （"fix: fail-closed fixes for review issues #994–#999 (#1000)"）。
  **注意**：Prompt 快照中的 `ebcafb4d` 已过时。
- 本 track worktree 基线：`a5b11b7f`（`git worktree add ../exp-rs-cn-eo-product-physics-11 -b zcode/cn-eo-product-physics-11 origin/master`）。
- `git log -20 origin/master`（摘要）：
  - `a5b11b7f` fix: fail-closed fixes #994–#999 (#1000)
  - `1cea9892` Merge `grok/dataset-foundry-benchmark-d19` (**#992 已合入**)
  - `c5d4aafe` D18: Unified Mission Workbench — MissionContext + D14/D15/D17 mounts (**#991 已合入**)
  - `77e178ac` fix(ci): macOS/Windows compile errors in d17 and Win32 paths (#993)
  - `08264801`..`44617ffa` D19 foundry/benchmark commits
  - `ebcafb4d` fix(ci): OSR WKT import in test_io_operators (#990)
  - `b91753ff`/`f368b9fd`/`64418b72` merges: classification-change-studio / workflow-pipeline-designer / geometric-registration-workbench
  - `e8c4bf43` D16 Temporal Phenology Timeline Studio (#986)
- Remote branches（按提交时间）：`origin/zcode/radiometric-spectral-workbench`（PR #1008 head）、`origin/master`。无其他 remote branch。

## Open PRs（启动时）

| PR | Branch | Title | State | 本 track 关系 |
|---|---|---|---|---|
| #1008 | `zcode/radiometric-spectral-workbench` | feat(spectral): Day 13 radiometric calibration, 6S atmospheric correction & spectral workbench | open, **CONFLICTING**, head `92fd8091`, updated 2026-09-15T14:25Z | 唯一并发 PR；文件级交集分析见 PARALLEL_OWNERSHIP.md |

Prompt 快照中的 #991/#992 **均已合入 master**（`c5d4aafe`、`1cea9892`），不再是并发风险 → 按规则 3，以新 master 重新审计，不再保留"D18/D19 read-only"的旧假设（它们已落在 master 基线内，本 track 从同一基线分叉，天然无冲突）。

## Open issues（启动时）

#1001（io:clip CRS override）、#1002（workflow registry executor fail-open）、#1003（dataset join null）、#1004（dataset:qa scan_capped）、#1005（georef mapPick CRS）、#1006（workflow syntheticExecute）、#1007（dataset:qa CRS）。
**全部属于 dataset/workflow/georef/io:clip 领域，与产品 registry/import 无文件级或语义交集。本 track 不实施其中任何一条**（dedupe 结论：无需重复实现，也不在本 track 修复）。

## ISSUES.md / CHANGELOG / goal-template 核验

- `ISSUES.md`：确认为旧 D3 教学内容 backlog（T-1..T-3, S-1/S-2, H-1..H-3, C-1/C-2 算子缺口），全部针对 `rs:temporal_*`/`rs:sar_*`/`rs:spectral_*` 算子与制图算子化——不在本 track ownership 内，且其中 T-1/C-2 等已被后续 10.0 track 修复（CHANGELOG 10.0 节证实 monitor scenes seam、temporal_extract_regions）。**不作为本 track backlog。**
- `CHANGELOG.md` 顶部为 Temporal Platform 10.0 + Data Fabric 10.0；无未完成的 CN-product 声明。
- `docs/agents/goal-template.md`：确认 /goal 骨架、预算规则、`.planning` whitelist 模式、`build-dev` preset、D-028（勿用 repo 根 build.cmd）。

## 代码现状（primary scope 事实）

| 事实 | 证据 |
|---|---|
| Product adapter registry：Landsat MTL / S2 SAFE / S1 SAFE / MODIS + GenericRaster fallback；Completeness = Complete/PartialReadable/Invalid/UnsupportedVersion | `src/geospatial/products/product_registry.h:55-115` |
| CN 支持 set：GF-1/2/6 PMS/WFV、GF-7 FWD/BWD、ZY-3 TLC/NAD/FWD/BWD、ZY-1 02C PMS/HRC、HJ-1A/B CCD、HJ-2A/B CCD | `src/geospatial/products/cn_product_metadata.cpp:372-390`（kSupportedPatterns，17 条） |
| CN 显式拒绝（带 reason）：GF-1B/C/D、HJ-1C、GF-7 其他、**GF-3**、**GF-4**、**GF-5**、**ZY-1 02B/02D/02E/IRS、ZY-5**、HJ-2 HSI/AIS、HJ-1 IRS、**CBERS** | `src/geospatial/products/cn_product_metadata.cpp:396-467`（unsupportedFamilyReason） |
| Sensor truth authority：`data/products/sensor_profiles/{gaofen,zy3,zy1,hj}.json`，schema version=1，fail-closed loader，unknown keys 报告 | `src/geospatial/products/sensor_profile.h:32-92`；`data/products/sensor_profiles/*.json` |
| CRESDA sidecar 解析：legacy `<MetaInfo>` vs current `<ProductMetaData>`；generation/unknown_top_level diagnostics | `src/geospatial/products/cn_product_metadata.cpp:511-528, 895-899` |
| 标准导入服务：`planCnProductImport` / `evaluateCalibration` / `executeCnProductImport`；4 个算子 `rs:cn_product_import`、`rs:gaofen_import`、`rs:zy3_import`、`rs:hj_import` | `src/operators/rs/rs_product_import_plan.h:39-101`；`docs/products/cn-satellites.md` |
| GUI：`src/app/dialogs/product_import_dialog.{h,cpp}` + main_window 接线 | `grep ProductImportDialog` |
| Agent surface：`src/agent/spatial_tools/io_tools.cpp` 消费 `ProductAdapterRegistry` | `grep` |
| CLI：`src/cli/cli_commands.cpp` 消费 product metadata | `grep` |
| 测试基线：`tests/test_cn_products.cpp`(1714 行)、`test_io_product_registry.cpp`、`test_io_products.cpp`、`test_satellite_products.cpp`、`test_product_import_dialog.cpp` | `ls tests/` |
| Sensor profiles 无 GF-3/GF-4/GF-5/ZY-1 02B/02D/02E/CBERS 条目 | `data/products/sensor_profiles/*.json` |
| Capability meta：`data/processing/algorithm_meta/capability/rs-gaofen-import.json` 等已存在；knowledge 页 `pi/knowledge/capability-*.md` | `ls` |
| ADR 编号：master 最新 0157（CN product adapters）；0158 已被 open PR #1008 认领（`docs/adr/0158-radiometric-physics-state-system.md`） | `ls docs/adr/`；`gh pr view 1008` |

## 10.0 前序 track 记录的 accepted debt（本 track 直接对应）

`.planning/cn-eo-products-sensor-physics-10/PR_BODY.md`（Known limitations）：
1. GF-3 SAR、GF-4、GF-5/AHSI-class、ZY-1 02B/02D/02E、CBERS remain recognized-but-not-adapted（sidecar 布局 in-repo 无文档，契约拒绝猜测）→ WP-B/C/D/E。
2. Registry schema type-hardening（per-field guards）deferred → WP-A。
3. cnSensorKey root-cause threading、HJ rpc_rpb 真实包确认、Windows read-only test 语义、midpoint-vs-centre 消费语义 → WP-A/E/F 相关深化。

## 结论 / rescope

- Mission 中的 8 个 work package 全部有真实缺口，无需 rescope 砍包；但 **WP-G（GUI/CLI/Agent parity）收窄为 parity/诊断接线**，因为 product_import_dialog/io_tools/CLI 已存在——本 track 不重写 UI，而是让新 family 与 ImportPlan 诊断在既有 surface 上可见。
- GF-5/AHSI 与 ZY-1 02D/02E 的 HDF5/子数据集访问依赖 GDAL HDF5 driver：**不做编译期硬依赖**，driver 缺失 = typed 拒绝（环境探测运行时进行），fixture 用 GTiff + sidecar 等价物表达元数据语义。
- GF-3/GF-4 sidecar 布局以 in-repo 可验证的公开规格命名样例 + golden fixture 表达；不宣称支持未验证的真实分发变体（未知 generation → `UnsupportedVersion`/拒绝，绝不猜测）。
