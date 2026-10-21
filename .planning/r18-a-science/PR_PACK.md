# hardening(r18-a): 遥感科学算法数值正确性、质量契约与吞吐性能 — 恢复 #1514 被静默回滚的修复 + Inf 契约 + SoA 内存收敛

Generated: 2026-10-19T14:04:32Z

## Baseline

- base `origin/master` @ `e12a9afbc8883a474dc9ffcaf3345ae55863dc71` (Merge pull request #1662 from WindWang2/fix/full-review-sweep)
- head `HEAD` @ `37f92132c339009aed9ec127d9455c3cb7b26204` (docs(r18-a): record live baseline drift at delivery (master +145, only .gitignore conflicts, mosaic auto-merges))
- commits in range: 12

## Scope

- sources (8): `src/analysis/segmentation/rs_multires_segmenter.cpp`, `src/operators/rs/rs_mosaic_operator.cpp`, `src/operators/rs/rs_raster_spatial_operators.cpp`, `src/operators/rs/rs_sam_classify_operator.cpp`, `src/processing/algorithms/atmospheric_correction.cpp`, `src/processing/algorithms/chunked_processor.cpp`, `src/processing/algorithms/image_fusion.cpp`, `src/processing/algorithms/mosaic.cpp`
- public_headers (1): `src/analysis/segmentation/rs_multires_segmenter.h`
- tests (8): `tests/test_atmospheric.cpp`, `tests/test_image_enhancement.cpp`, `tests/test_image_fusion.cpp`, `tests/test_known_answer_corpus_r4.cpp`, `tests/test_mosaic.cpp`, `tests/test_obia_segmentation.cpp`, `tests/test_raster_spatial5.cpp`, `tests/test_spectral_classification.cpp`
- docs (11): `.planning/r18-a-science/BASELINE.md`, `.planning/r18-a-science/CROSS_TRACK_FINDINGS.md`, `.planning/r18-a-science/DECISIONS.md`, `.planning/r18-a-science/LOOP_LEDGER.md`, `.planning/r18-a-science/ORACLES.md`, `.planning/r18-a-science/OWNERSHIP.md`, `.planning/r18-a-science/PERFORMANCE.md`, `.planning/r18-a-science/REVIEW_R1.md`, `.planning/r18-a-science/REVIEW_R2.md`, `.planning/r18-a-science/TASKS.md`, `.planning/r18-a-science/TEST_RESULTS.md`
- other (1): `.gitignore`

## Non-goals

- 不新增算法 family、不换框架、不动 R18-B/C/D/E 范围（rs:infer/模型推理/runtime/UI）。
- 不接管 #1513（geodata 持久化）写集；#1467 遗留两项维持前人「搁置」裁决（CROSS_TRACK_FINDINGS）。
- 缓办项（证据在 TASKS.md「缓办清单」）：WP8b kmeans RNG（已被新 master 6942ed100 处理）、
  band_math_simd regBuffer 提升、ISODATA 分裂扫描 O(k·d·n)、PCA-difference NaN 策略裁决、
  SAR 地形算子 DEM 网格/取消（CF-4）。

## Design

- **WP1 整体恢复而非重写**：#1514 的 6 个文件被 #1516 合并静默回滚（git 考古：parent1
  blob 349463c91 含防护/parent2 d2f9e3275 无/merge 取 parent2；`git log a8d5cd510..HEAD
  -- <files>` 为空）。`git diff c6b96a5e2 41fd31b57 -- <6 files>` 为空 = 逐字节恢复。
- **Inf 契约对齐家族现行惯例**：band_math/atmospheric streaming 已以 `!isfinite` 为 void
  先例；融合三处读边界 + 内存版 DOS/QUAC 内核 + mosaic 内核与算子统一该谓词，有限值语义不变。
- **SoA 内存布局**：分割器光谱矩从每像元 2×vector<double> 改为两个平铺数组，逐带累加顺序
  与节点编号不变 → 输出逐位一致（R2 三重验证：id 序/RNG 序/累加序）。

## Issue mapping

- 修复 #1514 内容被 #1516 静默回滚的回归（非 issue 但属 master 活缺陷）。
- 相关：#1467（跨轨发现 CF-2 记录，不接管）、#1491（不等线上 CI，本地 Oracle 交付）。
- 与新 master 6942ed100（r10a Inf 治理）互补：文件集不相交，无重复修复。

## Tests

本地（不等线上 CI）：构建 `build-r18-a`（Release+ENABLE_TESTS=ON，-j2，全局五轨锁），
运行时环境与完整口径见 `.planning/r18-a-science/TEST_RESULTS.md` 头部。

最终树全量双跑（11 套件 ×2 全绿）：test_image_fusion 44/1687、test_atmospheric 45/37507、
test_known_answer_corpus_r4 9/288、test_obia_segmentation 14/850、test_mosaic 10/38、
test_image_enhancement 14/6224、test_raster_spatial5 13/87、test_spectral_classification
30/197、test_fusion_quality_report 7/41、test_change_detection 41/888684、
test_band_math 49/130277。

RED 证据（修复前）：WP2 融合 Inf×4（含 hpf 邻居 −Inf 毒化）、WP3 dos1/dos2 全带 +Inf×3、
WP4 band=0/99 契约×4、WP5 null band SIGSEGV、WP6 mosaic/chunked×3、WP1 恢复测试
（multires epsilon 误杀）。NOT_EXECUTED：ASan/TSan lane（编译资源约束，补偿：改动均为
纯标量谓词/边界洗涤，无双线程新共享状态；WP5 UB 由 RED SIGSEGV→GREEN 直接证明）。

## Resources

-j2（仓库纪律上限；未修改任何资源保护脚本）；配置与测试均在
`/tmp/exp-rs-five-tracks-build.lock` + `resource_guard.py --parallel 2 --max-parallel 2`
下执行。targeted 构建 5+4+1 个测试二进制；全量运行按套件串行（CTEST_PARALLEL_LEVEL=1）。

## Review disposition

- R1（独立 context，对实际 diff 逆向 + 5 项定向证伪）：P0×1（quac 提交悬空 validEnd——
  构建竞态假绿，F4 证伪成功）+ P1×1（rs:mosaic 生产路线未修）+ P2×3 → 全部修复于
  529481e72（含 4 个新回归测试）。REVIEW_R1.md。
- R2（另一独立 context，6 项证伪）：**R1 修复全部成立，无新 P0/P1/P2，判定可合并**；
  P3×3（TEST_RESULTS 未刷新→已修；.gitignore EOF 冲突预期→已记录；内存版内核
  isnan-only 谓词为零调用者潜在面→记录维护约束）。REVIEW_R2.md。
- 未处理 P3 清单与理由见两份 review 记录。

## Known limitations

- **needs-rebase**：交付时 master 已前进 145 提交（e12a9afbc）。试验合并实测：唯一冲突
  为 `.gitignore` 双方 EOF 追加块（一行级）；`rs_mosaic_operator.cpp` 自动合并且 isfinite
  修复存活。本轨基点 2d2881631，12 commits。
- `.gitignore`（根共享文件）：仅追加本轨 3 行白名单块（各轨先例模式），PR 中单独标出。
- 质量报告参考重采样混入有限 sentinel（CF-5，#1514 已定性为 fixture 误报，现行 dual-lock
  测试钉住）；PCA-difference 单 NaN 全图 NaN 策略待 D15 域裁决（WP7 缓办）。
- 性能：分割器 SoA 512²/1024²×4band 实测 RSS −10.9%/−18.8%（3 次）、时延 −7~15%，
  输出逐位一致（PERFORMANCE.md）。

## Conflict hotspots

overlap evidence for this diff's 29 paths:
- open_prs: 0
- merged_prs: 7
  - `agent/r18-d-workbench-cartography` touches 1 of these paths
  - `agent/r18-c-pi-mcp-workflow` touches 1 of these paths
  - `agent/r18-b-model-runtime-perf` touches 1 of these paths
  - `hardening/r16-d-workbench-cartography` touches 1 of these paths
  - `hardening/r16-c-teaching-classroom` touches 1 of these paths
  - `hardening/r16-b-scientific-decisions` touches 1 of these paths
  - `hardening/r15-E-desktop-teaching-help` touches 1 of these paths
- remote_branches: 0
- sibling_worktrees: 0

---

## Diff stat

```
.gitignore                                         |   6 +
 .planning/r18-a-science/BASELINE.md                |  61 ++
 .planning/r18-a-science/CROSS_TRACK_FINDINGS.md    |  42 ++
 .planning/r18-a-science/DECISIONS.md               |  19 +
 .planning/r18-a-science/LOOP_LEDGER.md             | 107 +++
 .planning/r18-a-science/ORACLES.md                 |  44 ++
 .planning/r18-a-science/OWNERSHIP.md               |  39 +
 .planning/r18-a-science/PERFORMANCE.md             |  37 +
 .planning/r18-a-science/REVIEW_R1.md               |  22 +
 .planning/r18-a-science/REVIEW_R2.md               |  22 +
 .planning/r18-a-science/TASKS.md                   |  76 ++
 .planning/r18-a-science/TEST_RESULTS.md            |  93 +++
 .../segmentation/rs_multires_segmenter.cpp         | 127 ++--
 src/analysis/segmentation/rs_multires_segmenter.h  |   9 +-
 src/operators/rs/rs_mosaic_operator.cpp            |   2 +-
 src/operators/rs/rs_raster_spatial_operators.cpp   |  25 +-
 src/operators/rs/rs_sam_classify_operator.cpp      |   5 +
 .../algorithms/atmospheric_correction.cpp          |  41 +-
 src/processing/algorithms/chunked_processor.cpp    |   7 +
 src/processing/algorithms/image_fusion.cpp         |  94 ++-
 src/processing/algorithms/mosaic.cpp               |   5 +-
 tests/test_atmospheric.cpp                         |  73 ++
 tests/test_image_enhancement.cpp                   |  22 +
 tests/test_image_fusion.cpp                        | 795 +++++++++++++++++++++
 tests/test_known_answer_corpus_r4.cpp              |  39 +
 tests/test_mosaic.cpp                              |  36 +
 tests/test_obia_segmentation.cpp                   |  39 +
 tests/test_raster_spatial5.cpp                     | 124 ++++
 tests/test_spectral_classification.cpp             | 115 +++
 29 files changed, 2046 insertions(+), 80 deletions(-)
```

## Commits

```
37f92132c docs(r18-a): record live baseline drift at delivery (master +145, only .gitignore conflicts, mosaic auto-merges)
f5cf6e8f5 docs(r18-a): R2 review record (mergeable, no P0/P1/P2) + refresh TEST_RESULTS with post-fix final-tree double-run
46e7485d3 docs(r18-a): R1 review record, ledger lesson (build-race false green), 3rd benchmark run
529481e72 fix(review-r1): address the independent R1 findings (P0 + P1 + P2)
0c15169f4 docs(r18-a): track planning evidence — baseline, ownership, oracles, tasks, ledger, test results, performance, cross-track findings
9b8976c14 perf(atmospheric): drop the redundant full-band scratch copy in in-memory quac
285757a2f perf(segmentation): flat SoA storage for multires segmenter spectral moments
77802cf37 fix(processing): distinguishable failure states for mosaic ±Inf and ChunkedProcessor degenerate dims
97d78c585 fix(operators): validate the band index for the raster-spatial mask/label and window families
d7d7b7adc fix(atmospheric): in-memory dos1/dos2 treat ±Inf samples as invalid, not as the dark object
3f2608543 fix(fusion): wash ±Inf at the read boundary; guard linearWeighted; name the PCA all-NoData refusal
41fd31b57 restore(r15-a): re-land the #1514 fixes silently reverted by the #1516 merge
```
