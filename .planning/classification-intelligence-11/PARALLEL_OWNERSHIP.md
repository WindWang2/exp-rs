# PARALLEL_OWNERSHIP — 启动时（2026-09-15, master=a5b11b7f10）

## Open PR / remote branch 清单

| Ref | 状态 | changed files 摘要 | 与本 track 文件交集 | 处理策略 |
|---|---|---|---|---|
| PR #1008 `zcode/radiometric-spectral-workbench` | open, base=master, 非 draft | `.gitignore`；`.planning/radiometric-spectral-workbench/*`；`docs/adr/0158-*`；`src/agent/{CMakeLists.txt,spatial_tools/spatial_tool.cpp,spatial_tools/spectral_spatial_tools.*}`；`src/analysis/{CMakeLists.txt,atmospheric/fast_6s_lookup.*,hyperspectral/continuum_removal.*}`；`src/app/{CMakeLists.txt,widgets/band_composite_palette.*,widgets/spectral_profile_widget.*}`；`src/core/{CMakeLists.txt,radiometric_state.*,spectral_library.*}`；`src/processing/algorithms/{radiometric_calibration.*,spectral_indices.*,spectral_unmixing.*}`；`tests/{CMakeLists.txt,test_continuum_removal,test_d13_radiometric_spectral_e2e,test_fast_6s_atmospheric,test_radiometric_calibration,test_radiometric_state,test_spectral_agent_tools,test_spectral_indices,test_spectral_library,test_spectral_profile_widget,test_spectral_unmixing_fcls}.cpp` | **仅共享集成文件**：`tests/CMakeLists.txt`、`src/analysis/CMakeLists.txt`、`src/app/CMakeLists.txt`、`src/agent/CMakeLists.txt`、`src/core/CMakeLists.txt`、`.gitignore` | 这些文件本 track 只做 **append-only 最小接线**（classification 新文件注册、gitignore 三行白名单），不触碰 #1008 的新增/修改区；业务零交集。若 rebase 冲突：保留双方条目（append 合并），不覆盖科学代码 |
| （无其他 open PR / 非 master remote branch） | — | — | — | — |

## 文件级 ownership（本 track）

**本 track 独占写**：
- `src/analysis/classification/**`（新增 8+ 文件 + 既有文件 additive 修改）
- `src/processing/algorithms/classification_object_postprocess.*`（新增）
- `src/app/workbench/classification_studio_widget.*`
- `src/operators/rs/rs_supervised_classification_operator.*`
- `tests/test_class_order.cpp`、`test_probability_calibration.cpp`、
  `test_feature_schema.cpp`、`test_uncertainty.cpp`、
  `test_spatial_cross_validation.cpp`、`test_classification_object_postprocess.cpp`、
  `test_classification_intelligence_e2e.cpp`、`test_classification_intelligence_scale.cpp`、
  `test_classification_studio_widget.cpp`（扩展）
- `docs/processing/classification-intelligence.md`
- `.planning/classification-intelligence-11/*`

**read-only（他人所有权/权威来源）**：
- `src/processing/algorithms/glcm_texture.*`（D15 seam，消费不改）
- `src/core/spatial_split.*`（D15，消费其隔离不变量语义不改）
- `src/dataset/**`（D19，完全不改）
- `src/app/mission/**`、D18 mission 文件
- PR #1008 回避清单（DECISIONS D-014）
- `src/analysis/segmentation/**`（不在 primary scope；OBIA 语义参考）
- `docs/adr/**`（避免号段竞争）

**共享集成文件（append-only 最小 diff）**：
- `tests/CMakeLists.txt`、`src/analysis/classification/CMakeLists.txt`（scope 内，独立可写）、
  `src/processing/CMakeLists.txt`（如需）、`.gitignore`、`CHANGELOG.md`

## Open issue dedupe（#1001–#1007）

全部为 io:clip / workflow / dataset / georef 域 finding（详见 BASELINE.md 表），
与本 track changed files 零交集 → 不在本 track 实施；disposition 见
EVIDENCE.md `OUT_OF_SCOPE`。classification 域启动时无 open issue。

## 并发新增 PR 的规则

任何新 open PR 若触及本 track changed files：以 master 上的稳定 seam 为准
重新审计，本 track 已写的业务代码不复制他人实现；冲突走 rebase + 逐块裁决
（DECISIONS 增补条目）。
