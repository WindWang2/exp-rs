# EVIDENCE — 分类证据账本（Local evidence only; no online CI dependency）

## Phase 0 — 审计与落盘（2026-09-15）

- `git fetch origin --prune`：首次两次 TLS EOF 失败（GitHub 间歇故障），第 3 次成功；
  `git rev-parse origin/master` → `a5b11b7f10fa010c1c060864fb427d777ba9a4aa`。
- `gh pr list --state open` → 仅 #1008（radiometric/spectral）；`gh pr diff 1008 --name-only`
  → 44 文件（清单见 PARALLEL_OWNERSHIP.md）。
- `gh issue list --state open` → #1001–#1007（io/workflow/dataset/georef 域，无 classification 条目）。
- `gh api` 读取 #1008 元数据：base=master，非 draft（read 3 次重试内成功）。
- subagent #1（Explore，只读）分类域科学审计完成：12 节事实 + 25 缺口，
  file:line 证据归档于 CURRENT_ARCHITECTURE.md / CAPABILITY_MATRIX.md(before)。
- worktree：`git worktree add ../exp-rs-classification-intelligence-11 -b
  zcode/classification-intelligence-11 origin/master` → OK（HEAD=a5b11b7f10）。

### 构建环境

- `cmake --preset dev-default`（GNU 16.2.1）：首配因 pybind11 FetchContent
  git clone 反复 TLS EOF 失败（configure1-4 详错见 /tmp/cmake_configure*.log，
  本地临时文件）。处置：复用主仓 `build-dev/_deps/{pybind11-src,catch2-src}`
  + `-DFETCHCONTENT_SOURCE_DIR_PYBIND11=... -DFETCHCONTENT_SOURCE_DIR_CATCH2=...`
  → configure5 **exit 0**，generator=Unix Makefiles（repo preset 未指定 Ninja）。
- 主机：16 核 / 64 GiB；`uptime` load1 ≈ 2.5（构建期间限 -j2）。
- bootstrap 全量构建：`cmake --build build-dev -j2` 后台运行（基线可编译性证明）。

### 资源采样说明

编译期间 60s 采样：`uptime` + `ps -o rss=` 汇总 ninja/cc 子进程 RSS；
单条命令超时上限 600s；若采样失败记录一次 not-executed 并维持 -j2。

## OUT_OF_SCOPE（范围外发现）

- issues #1001（io:clip CRS）、#1002（workflow executor fail-open）、
  #1003（dataset join null）、#1004（dataset:qa scan_capped）、#1005（georef GCP）、
  #1006（PipelineRunCoordinator）、#1007（dataset:qa CRS）：均为 io/workflow/
  dataset/georef 域 finding，与本 track changed files 零交集 → 不修，留原 track。
- `rs_classifier_backend_factory.cpp:23-40` 子串匹配可误命中任意含 rf/bayes
  字样的方法名；`RsSvmBackend` C=10/γ=0.5 硬编码、无超参通道 → 已知兼容性
  负债，改精确匹配会破坏既有调用方字符串，**follow-up**，本 track 不改行为。
- `src/app/classification/qgsclassificationmainwindow.cpp`（3956 行）与 studio
  双 UI 能力割裂 → follow-up（超 UI scope）。
- 三套划分系统长期收敛方案（classification_split / core/spatial_split /
  dataset split）→ follow-up，本 track 以 D-009 解耦方案落地自身需求。

## Phase 记录

### Phase 1–6 实现 commits（bootstrap 构建期间的代码级完成）

| Phase | Commit | 内容 |
|---|---|---|
| 0 | 27903d29a9 | planning 落盘 + .gitignore 白名单 |
| 1 | d1f3bfb20e | rs_class_order / rs_probability_calibration / rs_uncertainty / rs_feature_schema + 4 测试 |
| 2 | d5f6d245e2 | backend additive seam + SVM OvR opt-in + NB 类序旁车 + pipeline uncertainty/校准/sidecar v2 |
| 3 | 85f5c9b615 | rs_spatial_cross_validation + classification_object_postprocess + 2 测试 |
| 4 | d778267bf9 | studio 4 面板 + rs:supervised_classification 参数 + docs/processing/classification-intelligence.md |
| 5/6 | 37ec0ea6c2 | scaler fail-closed + e2e + 100k scale 测试 + CHANGELOG |

- `git status --porcelain` 每次 commit 后 = clean。
- `git diff origin/master...HEAD --check` → exit 0（commit 后复检）。

### 构建/测试 gate（构建中持续更新）

- 共享主机事实：同机存在 5 个并行 track 的 worktree 构建
  （radiometric-physics-11 / multimodal-registration-11 / mosaic-fusion-11 /
  qgis-editing-annotation-11 / 本 track）→ 系统 load 与本 track 的 -j2 无关；
  本 track 严格遵守 -j2。
- bootstrap 构建（旧 configure 图，基线可编译性）在 38% 处被主 agent 终止
  （仅本 track 的 make PID 25009/25012，按 /proc/cwd 验证归属），
  reconfigure 纳入新文件（RECONFIGURE_EXIT=0）后增量重启。

