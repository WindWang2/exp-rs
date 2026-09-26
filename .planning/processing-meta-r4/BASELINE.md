# BASELINE — Track 6: Processing Registry Metadata Completeness (R4 Deep Edition)

实测时间：2026-09-27。所有计数均在本机以 rg/ls/python3 实测，不沿用提示词写作值。

## 1. Git 基线

- `origin/master` = `15e5c66b543ef3874cb929f17529ef456bd6c059`（fetch 后与本地 master 一致，ahead/behind = 0/0；提示词写作时 SHA 相同，未前进）。
- 本轨道 worktree：`/home/kevin/project/exp-rs-processing-meta-r4`，分支 `hardening/r4-processing-meta`（基于 origin/master）。
- open issue 实测：**0**。

## 2. 锚定表复核（提示词 3.1 逐项）

| 锚点 | 提示词值 | 实测值 | 复核方式 |
|---|---|---|---|
| `src/operators/rs/*.cpp` | 128 | **128** ✓ | `ls src/operators/rs/*.cpp \| wc -l` |
| `src/operators/rs/*.h` | 131 | **131** ✓ | 同上 |
| `REGISTER_RS_OPERATOR(` 注册数 | 157 | **157** ✓ | `rg -c` on rs_operators_init.cpp（注册区间 131–292 行，宏定义在 src/operators/framework/rs_operator_registry.h:80） |
| `Rs*Operator` 类声明 | 151 | **151** ✓ | `rg -o 'class Rs\w+Operator' -g '*.h'` 去重计数 |
| 顶层 sparse sidecar | 57 | **57** ✓ | `ls data/processing/algorithm_meta/*.json` |
| `capability/` 侧车 | — | **157 个 rs-*.json + capability_relations.json = 158** | `ls data/processing/algorithm_meta/capability/` |

> 口径互引：sparse 57 = 56 个 rs:* 声明者 + 1 个 gdal:polygonize（注册口径计 56/157），见 DECISIONS #1，两处数字非矛盾。
| `data/help/commands.json` | 76 | **76** ✓（JSON list） | python json.load |
| 快照文件 | data/contracts/ | **✓** `determinism_census.snap.json` + `contract_graph.snap.json`（另有 `contract_exemptions.json`） | ls |
| contract_inventory 用法 | —out/--check + census | **✓ 且实测单工具双快照**：`--out/--check`（契约图，需注册表初始化）、`--census-out/--census-check`（census，纯源扫描）；`--check` 退出 0 的条件是**字节一致且 findings 为空** | 通读 src/contracts/tool/contract_inventory_main.cpp（189 行） |
| capability 链文件 | harness 10 个 | **✓ 10 个**（catalog/graph/knowledge/pages/relations × h/cpp；另有 repair_capability_source.{h,cpp} 不属本链） | ls |
| `renderCapabilityKnowledgePages()` | capability_pages.h:31 | **✓ :31** | rg |
| gen-meta / gen-pages 工具 | "不存在" | **✗ 提示词此结论错误**：二者是 `capability_knowledge_tool`（tests/CMakeLists.txt:10801，源 scripts/capability_knowledge_tool.cpp，371 行）的**子命令**：`gen-meta <root>` / `gen-pages <root> [--check]` / `dump`。提示词的 rg 只搜了*文件名* | 通读该工具源码 |

## 3. 前提修正（本轨道最重要的 Phase 0 产出）

P1. **顶层 57 个 sparse sidecar 是生成工件且被门禁钉死**。`data/processing/algorithm_meta/README.md`（ADR 0122）：字段真值在算子代码 `metadata()`，经 `sicnu_geo_rs_cli --export-catalog` 导出，禁止手改。`tests/test_algorithm_meta_drift.cpp` 三重断言：① `generateCatalog(descriptors)` 集合 == 磁盘 57 文件（双向成员，`REQUIRE(expectedCatalog.size() == 57)` 两处）；② 字节级可再生；③ exportCatalog 往返恰好写 57。**因此"sparse 集合 == 在代码中声明 taskFamily 的算子集合"是设计不变量，57/157 不是丢失，是 opt-in 声明的当前规模**。手补 70+ 个顶层 JSON 会直接打红该门禁——提示词 WP-B 原样执行即回归，不执行（铁律："前提不成立→记录证据后收窄"）。

P2. **文件级完备已在 capability 层达成**：`capability/` 157 个侧车与 157 注册一一对应，`tests/test_capability_knowledge.cpp` 的 D8 coverage 门禁把该集合钉到活注册表。**元数据完备的真实缺口在字段级（authored enrichment）**：`scripts/capability_enrichment.py --strict` 实测红——其 D1 账本 115 条、磁盘 157 侧车，**42 个算子无 authored enrichment**（逐名清单见 §6）。该脚本无任何 ctest/CI 挂载（rg 零命中），属 D1 一次性迁移工具，账本分母过期；本轨道**不改它**（scripts/ 不在白名单），authored 键按 README 许可"直接编辑侧车 JSON（只动 authored 键，永不改 derived 键）"。

P3. **authored vs derived 键边界**（scripts/capability_knowledge_tool.cpp 头注 + capability_enrichment.py 头注）：
- authored（再生保留，允许直接编辑）：`summary`、`failure_modes`、`applicability`、`teaching_use`、authored `prerequisites`/`limitations`、`band_roles`、`modality`、`crs.requires_projected`（authored 部分）。
- derived（gen-meta 从活描述符再生，禁止手改）：io 端口块、determinism、parameters、family、crs.requires_shared_grid 等。
- failure_modes 的 code 是闭集（capability_enrichment.py CLOSED_CODES，20 个码），`when`/`remedy` 为中文 agent 面文本（与既有语料一致）。

P4. **authored 键字段级缺口实测**（2026-09-27 对 157 个侧车的审计，空/缺/[]/{} 记缺）：applicability 缺 **18**、teaching_use 缺 **22**、prerequisites 缺 **56**、limitations 缺 **80**；summary / failure_modes **0 缺**（D1 契约当前满足）。42 个"无任何 authored enrichment"的算子是最大连续块（一次补齐即同时消除四类缺口的主要部分）。

P5. **sparse taskFamily 100 算子缺口**：按 P1 属产品级 opt-in 声明，补齐需改 100 处 C++ `metadata()` 并扩大 sparse 目录，改变 task→算法发现行为（用户可感知）。本轨道**不执行**，记入 PR backlog 与 DECISIONS.md。

## 4. 在途 PR 盘点与 file-overlap map（5 个 open PR，提示词只列了 3 个）

| PR | 分支 | 与本白名单的重叠 | 处置 |
|---|---|---|---|
| #1334 fix/review-p1-security | 约 30 文件 | `src/agent/tool_catalog/*`、`tests/CMakeLists.txt`、`src/operators/framework/model_catalog.*` | 不触碰 tool_catalog；tests/CMakeLists.txt 若需加测试目标，rebase 时按行合并 |
| #1335 fix/review-p0-build-restore | 约 19 文件 | **`tests/test_capability_knowledge.cpp`**、`tests/CMakeLists.txt`、顶层 CMakeLists.txt | WP-E 若扩展该测试文件，基于合并后 master rebase；先在其当前 master 版本上工作 |
| #1336 closure-ui-runtime-r4 | 约 13 文件 | **`data/help/commands.json`**、多个 tests | WP-F 对照基于当前 master；若 #1336 先合并则 rebase 后复核 76 条目数 |
| #1337 closure-workflow-contracts-r4 | 约 24 文件 | **`data/contracts/contract_graph.snap.json`**、`data/help/commands.json`、`src/contracts/error_code_scanner.*`、`graph_assembly.cpp` | 快照文件本轨道拥有，但 #1337 在途：完成时若未合并，rebase 后重新生成快照再提交 |
| #1338 closure-io-processing-r4 | 约 29 文件 | `src/processing/gdal|providers`（白名单内但我方无计划触碰的具体文件） | 无冲突预期 |

评审材料：提示词所列 4 份（PROJECT_REVIEW_DOSSIER_5.0.md 等）**仓库根目录实测不存在**，按提示词预案以 git 历史与 PR 描述为准；PR #1335/#1336/#1337 描述已在 Phase 0 通读要点（构建恢复、help gap 关闭标准、快照/契约收敛）。

## 5. 构建与验证环境（实测）

- cmake：`/home/kevin/toolchain/cmake-dist/bin/cmake`；ninja：`/home/kevin/pwb-sdks/root/usr/bin/ninja`（均不在默认 PATH，从相邻轨道 `build-review/CMakeCache.txt` 反查）。
- 配置：`-G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DCMAKE_PREFIX_PATH=/home/kevin/pwb-sdks/root/usr`；Qt6 6.11.2（/usr）、GDAL（pwb-sdks）。
- 资源红线：`ninja -j2`、`CTEST_PARALLEL_LEVEL=1`、RSS>70% 降 -j1；QT_QPA_PLATFORM=offscreen。
- 机器：40 核 / 64 GiB（Phase 0 时 RSS 28.4/64 GiB ≈ 44%）。
- 基线红绿分布：待全新构建完成后执行 `ctest -R "capabilit|contract|meta|registry|snapshot" -j1`，结果回填 §7。

## 6. 42 个无 authored enrichment 的算子（enrichment --strict 实测输出）

> 修正注记（独立评审 P2）：本节是 capability_enrichment.py --strict 的**账本口径**输出，其中 13 条为假阳性
> （rs:sar_interferogram、rs:sar_unwrap、rs:library_select、rs:mnf_inverse、rs:sar_coregister、rs:sar_displacement、
> rs:sar_phase_filter、rs:sar_polsar_decompose、rs:sar_temporal_events、rs:spectral_band_select、rs:terrain_solar、
> rs:terrain_landform、rs:terrain_viewshed——master 上四键已非空），系该脚本账本分母过期（115）所致。
> **交付口径以 §3 P4 的四键实测（18/22/56/80，与最终 diff 完全吻合）为准**；本清单仅作"迁移工具账本过期"的证据保留。

rs:brdf_normalization, rs:cem_detection, rs:change, rs:classify, rs:endmember_analysis, rs:library_select, rs:local_rx_anomaly, rs:mnf_inverse, rs:osp_detection, rs:quality_mosaic, rs:radiometric_qa, rs:register_images, rs:regress, rs:sar_coregister, rs:sar_coregister_local, rs:sar_displacement, rs:sar_interferogram, rs:sar_network_inversion, rs:sar_pair_network, rs:sar_phase_filter, rs:sar_polsar_decompose, rs:sar_remove_topographic_phase, rs:sar_temporal_events, rs:sar_unwrap, rs:solar_geometry, rs:sparse_unmixing, rs:spectral_band_select, rs:spectral_similarity, rs:spectral_spatial_fuse, rs:stack_register, rs:tcimf_detection, rs:temporal_extract_regions, rs:temporal_harmonic_breaks, rs:temporal_model_select, rs:temporal_phenology_multi, rs:temporal_region_features, rs:temporal_regularize, rs:temporal_sar_fusion, rs:temporal_seasonal_breaks, rs:terrain_landform, rs:terrain_solar, rs:terrain_viewshed

## 7. 基线 ctest 红绿分布（2026-09-27 实测，meta 编辑前）

命令：`LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib QT_QPA_PLATFORM=offscreen ctest -R "capabilit|contract|meta|registry|snapshot" -j1`（日志 `logs/baseline_ctest_run1.log`）。
结果：77 项，62 绿 / 15 红；15 红中 12 项为 NOT_BUILT（Not Run，目标在 master 即未随本次定向构建产出），**真实红 3 项，全部为 master 既有缺陷**：

1. `Help ↔ registry: no phantom and no uncovered commands (#869 class)`（test_command_contract_9）：`mission.task.resume` / `mission.task.retry` 两条命令无 help 条目——#1336/#1337 修复领域。
2. `Contract snapshot is fresh (byte-compare against live graph)`：master 的 `contract_graph.snap.json` 对活图已陈旧——#1337 修复领域；census 快照新鲜（既有 `committed census snapshot byte-matches` 测试绿）。
3. 本轨道新测试 `committed snapshots equal the fresh generation byte-for-byte`：与 2 同根因（测试正确捕获存量漂移，属 WP-D 交付的职责证明；本轨道将再生成两快照）。

另有本轨道新增 `Capability authored enrichment census` 测试补齐前红（TDD 设计态，111 缺口），补齐后转绿。
构建环境记录：master 在本基线存在 27 个测试可执行断链（sicnu_agent 未传递 agent_loop/agent_ops 符号，#1335 "restore master build/CI — link graph" 领域）与 test_capability_knowledge.cpp 缺右括号（同 PR 领域）；本轨道在白名单内最小修复（tests/CMakeLists.txt foreach 链接块 + 1 个右括号，均标注 #1335 合并后可删/以对方版本为准）。

## 8. 本轨道边界声明（Scope，含 P1–P5 修正后的执行口径）

允许触碰：`data/processing/algorithm_meta/`（capability/ 侧车 authored 键补齐）、`pi/knowledge/`（gen-pages 再生产物）、`data/contracts/*.snap.json`（按需再生成）、`tests/`（防回归/完备门禁收紧）、`tests/CMakeLists.txt`（测试目标注册）、`src/contracts/`（快照 gate 最小修复，仅当实测发现缺陷）、`src/processing/`（注册表一致性最小修复，仅当实测需要）、`.planning/processing-meta-r4/`。
不触碰：白名单外一切目录；`scripts/capability_enrichment.py`（账本过期问题记 backlog）；sparse 顶层 57 文件集合（除非 taskFamily 声明变更，本轨道不做）。
