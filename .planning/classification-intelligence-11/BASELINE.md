# BASELINE — Phase 0 启动审计（2026-09-15）

## GitHub / origin 事实（启动时刷新）

- `origin/master` = `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`
  （`fix: fail-closed fixes for review issues #994–#999 (#1000)`）。
- Prompt 快照中的 `ebcafb4d02` 已过时：#991（D18 unified mission workbench）与
  #992（D19 dataset foundry/benchmark）**均已合并进 master**，另叠加 #993（CI 修复）
  与 #1000（fail-closed 修复）。
- 最近 20 条 master 提交已记录（`git log -20 --oneline --decorate origin/master`，
  见 EVIDENCE Phase 0）。
- 注意：启动时 GitHub API 曾出现间歇性 TLS/EOF 故障（`git fetch` 与 `gh` 均重试后成功）；
  所有事实以重试成功后的返回为准。

## Open PRs（启动时）

| PR | branch | 状态 | 与本 track 交集 |
|---|---|---|---|
| #1008 | `zcode/radiometric-spectral-workbench` | open，非 draft，base=master | 无 classification 文件；但改了 `src/processing/algorithms/spectral_indices.*`、`src/analysis/CMakeLists.txt`、`src/processing/algorithms/`（radiometric/spectral/unmixing 新文件）、`tests/CMakeLists.txt`、`src/agent/CMakeLists.txt`、`.gitignore` |

详见 `PARALLEL_OWNERSHIP.md`。

## Open issues（启动时，全部非 classification 域）

| # | 域 | 严重度 | 与本 track 关系 |
|---|---|---|---|
| 1001 | io:clip CRS | P1/critical | 无 |
| 1002 | workflow registry executor fail-open | P1 | 无 |
| 1003 | dataset joinFeaturesBySampleId JSON-null | P1 | 无 |
| 1004 | dataset:qa identity scan_capped | P1 | 无 |
| 1005 | georef mapPickToLayerCrs | P1/critical | 无 |
| 1006 | workflow PipelineRunCoordinator syntheticExecute | P2 | 无 |
| 1007 | dataset:qa CRS audit | P2 | 无 |

结论：无 open classification issue；`ISSUES.md` 为旧 D3 平台算子 backlog
（temporal/SAR/hyperspectral/cartography 缺口），与 classification 无直接条目，
不作为本 track backlog。

## 分类域现状科学审计（subagent #1，只读）

完整报告（12 节事实清单 + 25 项缺口表，全部带 file:line）由主 agent 归档于
`CURRENT_ARCHITECTURE.md`（现状 authority/seam 图）与 `CAPABILITY_MATRIX.md`
（before 状态）。

核心结论（全部有 file:line 证据，见上两文件）：

1. **概率**：OpenCV 后端中仅 NormalBayes/MLP/RF 支持 `predictProbabilities`
   （RF/MLP 以 `<model>.labels.json` 固定升序类列序；NormalBayes 类序未持久化）；
   SVM/kNN/Mahalanobis/MinDistance 无概率；native `classifier_engine` 侧
   SVM/KMeans/ISODATA 输出 1-hot 伪分布。
2. **校准**：全仓无 Platt/isotonic/Brier/ECE/reliability 任何代码与测试。
3. **空间验证**：`RsCrossValidation::kFold` 仅分层 k-fold（无 group/spatial）；
   D15 `src/core/spatial_split.h`（块划分+buffer+Moran's I）**无生产消费者**
   （仅 tests/support/d15_e2e_pipeline.cpp 与 test_spatial_block_leakage.cpp）；
   classification 域无 leakage audit（dataset 域另有一套 13-kind audit）。
4. **特征**：训练 X 仅来自波段栈（`RsTrainingDataExtraction`），无特征名/顺序/
   fingerprint 持久化（旁车 features 仅 1-based 波段号）；`RsFeatureScaler`
   fit/transform 无 NaN/NoData 防御；GLCM 已有纯 STL seam
   `src/processing/algorithms/glcm_texture.h`。
5. **不确定性**：对象级有 per-segment Shannon entropy CSV（rs:obia_classify
   `outputUncertainty`）；像素级仅 best-class 概率栅格 + meanConfidence 标量；
   无 entropy/margin 图、无 reject/abstain。
6. **对象级**：`rs:obia_classify` 逐对象特征直接预测；后处理仅像素级
   majority/sieve/clump（D15 `classification_postprocess.h`）；无 segment
   邻接图/graph smoothing/面积-邻接规则。
7. **artifact**：OpenCV YAML 模型 + `.meta.json` superset 旁车（version=1：
   method/scaler/classes/features(波段号)/validation/kmeansRemap）；
   **不存 seed/超参/类名/特征指纹/校准**；RF/MLP labels 旁车写失败 best-effort 静默。
8. **Studio**：`classification_studio_widget`（.h 144 行/.cpp 458 行）= 魔棒 +
   密度散点 + 调色板 + swipe；无概率/置信/混淆 pair/特征重要性视图。
9. **规模**：无 100k 分类样本有界测试（现有 100k 均为 metadata/UI 散点）。
10. **Surface**：分类相关算子 14 个已注册（rs_operators_init.cpp）；无 CV 算子、
    无分类专用 agent 工具。

## 构建环境事实

- CMakePresets：`dev-default`(binaryDir=`${sourceDir}/build-dev`)、`ci-fast`、
  `ci-full`、`sanitizer-debug`、`release-package`。**无 `build-dev` preset 名**，
  GOAL 所写 `build-dev` 指其 binaryDir；本 track 使用 `--preset dev-default`。
- 工具链：GNU 16.2.1，Ninja 可用；主机 16 核 / 64 GiB。
- 主仓 `build-dev/` 已 populated `_deps`（catch2、pybind11）；worktree 首次
  configure 因 pybind11 FetchContent 克隆失败（GitHub TLS 间歇故障），采用
  `FETCHCONTENT_SOURCE_DIR_PYBIND11/CATCH2` 指向本地 populated src 离线复用
  （详见 EVIDENCE）。
