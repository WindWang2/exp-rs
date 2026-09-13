# WHOLE_REPO_REVIEW — exp-rs 全库逐行审查 · 执行摘要

**Track**: `zcode/whole-repo-line-review` @ `27b9aa0a63`（origin/master；GOAL 钉定的 `efc5c52f` 之后 23 个 commit 以 delta-pass 全部过完）。
**Mode**: review-only——`src/` 与 `tests/` 零改动，本 PR 只含 `WHOLE_REPO_REVIEW.md`、`review/`、`.planning/`、`docs/`（无 docs 改动）。
**方法**: 六透镜（科学计算契约 / 生命周期与内存 / 并发与状态同步 / 求解器语义 / 契约与元数据漂移 / 测试可信度）；DECISIONS D-004 声明的双层深度（逻辑密集文件全文逐行，声明/样板文件结构过 + 透镜风险扫描）；每条发现逐字引用 + 复现路径；两轮复核（主代理自审 + 子代理 V/C）。

## 结论

仓库整体处于**高审计饱和状态**：250 个已闭环 issue（#595–#945）+ 两份历史审计的修复在本次抽查中**全部验证在位**（#694 双关闭、#756 容器校验、#774 提交、#791/#807 原子发布、#801/#856 尺度探测、#811 BEGIN IMMEDIATE、#825/#863 求解器、#896/#925/#926 IPC 帧、#928 锁降、#903 快照配置、#927 userTouched、#931/#944 锁外 IO、#945 scale 乘法语义）。本轮产出的 **7 条新发现** 集中在：修复漂移（pi 分叉、io:reproject 参数）、声明值与编码的边界缺口（class_mapping、QA fail-open 语义）、以及公共 API 的潜伏死角（TensorBlob）与无界计算（NMS）。

## 严重度矩阵

| | P0 | P1 | P2 | P3 | 合计 |
|---|---:|---:|---:|---:|---:|
| operators | 0 | 1 | 4 | 1 | 6 |
| pi | 0 | 0 | 2 | 0 | 2 |
| **合计** | **0** | **1** | **6** | **1** | **7** |

（发现计数按文件归属：F-OPS-1/3/4/5 及 F-OPS-2 在 operators/runtime·io，F-PI-1/2 在 pi。）

## Top 风险（全部含逐字引用与复现，见 review/findings/、review/issues/）

1. **F-OPS-4 (P1)** `io:reproject` 的 `srcCrsOverride` 是死参数——无 CRS 输入经"唯一获准兜底"重投影后像素零变换却被标注 targetCrs（静默错配地理参考）。同文件 `io:clip` 对同名参数有功能性消费，证明是漂移。
2. **F-OPS-1 (P2)** Labels 输出 `class_mapping` 重映射值未按输出编码校验——≥255 的产品类被钳制为 NoData 哨兵，整类像素静默丢失，palette/统计仍声称该类存在。
3. **F-OPS-3 (P2)** `rs:qa_mask` 对不可读 QA 样本 fail-open（→词 0=clear）——质量门失效方向错误，云/雪像素流入下游（#699 修复 UB 时有意保留了该语义，本发现针对语义本身）。
4. **F-PI-1 / F-PI-2 (P2)** pi 桥：失步后不拆流成僵尸桥；startup-deadline 修复只落在 mcp_bridge.ts 未回移 Pi 实际加载的 exp-rs-spatial.ts（双向分叉漂移的实证）。
5. **F-OPS-5 (P2)** 检测全栅格 NMS O(n²) 且无取消注入点——逼近 max_detections 预算时 worker 长时间不可取消阻塞。
6. **F-OPS-2 (P3)** `TensorBlob::fromMat` 非连续 ND Mat 回退拷贝依赖 `mat.rows`（ND 时为 -1）——产出字节数合法、内容全零的张量；当前第一方调用方全部连续，属公共 API 潜伏死角。

## 覆盖（review/COVERAGE_LEDGER.csv）

| 区域 | 文件 | LOC | 状态 |
|---|---:|---:|---|
| Tier A 第一方（src/ 除 core/gui/external + pi TS + tests/） | 2,998 | ≈590,000 | **100% reviewed**，0 skipped |
| Tier B vendored（src/core, src/gui, src/external） | 3,663 | 1,242,759 | excluded（QGIS 4.0.2 baseline）；**ANTIGRAVITY 局部改动 3 文件全量逐行**（settingsregistry 替换、codeeditor stubs、弱符号 stubs——均无第一方调用方/与上游默认一致） |
| H 资源面（src/ui 551 个 .ui） | 551 | — | H 层结构检查 |
| delta（efc5c52f..27b9aa0a63，23 commits/65 文件） | — | +2,588/−458 | **delta-pass 100%**，修复全部验证，无新发现 |

排除条目 15 行全部带理由且全部为 vendored/非代码树。

## 复核与假阳性

- **子代理 V**（假阳性清扫）：7 条发现 6 条送达复核（F-OPS-5 在 V 运行后落档，由主代理同方法自审），**0 撤下**；2 处证据精化已折回 finding 文本（F-OPS-2 全零而非未初始化、F-PI-1 触发条件收窄到无终止换行/持续洪泛），F-OPS-4 获加强证据。
- **子代理 C**（覆盖审计）：账本结构诚实（全文件枚举、排除带理由、无 pending 残留）；其发现的书记问题（孤儿计数、缺方法笔记、ui 缺行、PLAN 状态滞后）全部整改；其 thin-evidence 标记文件（rs_sift_matcher、plugin_manifest、ipc_channel）已补 spot 深读——ipc_channel 本已全文审过。
- **假阳性率**：撤下 0 / 提交 7 = 0%。第一轮内部撤下候选 4 条，全部记录于 `review/REVIEW_LOG.md`（Considered-and-dropped）。
- 与既有 250 条 issue 及两份历史审计的逐条对照：`review/DEDUPE.md`——**零重复**。

## 测试可信度（透镜 6）

vacuous 断言 5 处均为"压测存活即断言"惯用法（#656 已裁决该族）；零断言启发式 68 命中经抽样全部为 helper/REQUIRE_THAT/fuzz-lambda 误报；`[!shouldfail]`/SKIP 无滥用；delta 新增测试断言密度合格、数值断言带容差。无新发现。

## 证据与可复现性

- 全部 P1/P2 配 Catch2 断言草稿（`review/tests/F-OPS-{1,3,4,5}.cpp`、`F-OPS-2` 草稿）或可执行复现说明；P3 亦附草稿。草稿**不进 CMake 构建**（track 契约）。
- 本机验证输出与资源记录：`.planning/whole-repo-line-review/EVIDENCE.md`（delta-pass 明细、基线捕获）。验证性构建：0 次（全部发现以静态逐行+调用链+修复族交叉验证定案，构建只在会执行断言时才需要）。

## 局限（诚实声明）

- 双层深度方法下，非深读文件（约 60% 的 Tier A 文件，主要为 schema/metadata 样板与声明头）的保证是"结构完整 + 透镜风险扫描干净 + 其依赖的修复族已验证"，非逐字符阅读；深读清单见 `REVIEW_LOG.md`。
- 历史指名 dossier 两文件在检出中不存在（未跟踪历史产物），去重基线以其下游 issue 批次 + 全量 issue 列表重建（DECISIONS D-002）。
