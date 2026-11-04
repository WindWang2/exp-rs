# r25-B: 地理空间数据资产、EO 模型推理与实验可复现工程深化 — 导入预检解释报告 / 可解释推理计划与质量摘要 / 复现差异原因报告（RED→GREEN×2，双轮独立评审）

**BASE** `2610cb4741e69a89124921e44c03e340e2522ee2`（= origin/master @ 2026-10-27）
**HEAD** `d777573994`（11 commits: 侦察账本 → F1-F3 → E2E/文档 → RED/GREEN 证据 → R1 收口 → R2 收口 → 评审/perf 文档）
**分支** `agent/r25-B-data-model-runtime-20261028T140313Z`
**平台** Linux 本机实跑（GCC 16.2.1 / Ninja -j2 / Release / Qt 6.12 / GDAL 3.13.3，共享宿主 flock 单锁）。Windows/macOS：NOT_EXECUTED。

## 侦察结论（执行时实查，不重做已合并工作）

2026-10-28 14:03 UTC：open PR = **0**；open Issues = 144（B 域点名 Issue #1541/#1545/#1546/#1547/#1548/#1581/#1637 逐条实码核验全部 FIXED_IN_MASTER_BUT_OPEN，证据 `.planning/zcode-r25-B/ISSUE_TRIAGE.md`）。本轨全部改动来自三个经实码验证存在缺口的**既有功能深化**（详见 BASELINE.md）。

## 三项深化（每项：对外 schema + RED→GREEN×2 + 回归）

### F1 导入预检解释报告 `sicnu.import.preflight/1`（src/data/import_preflight.{h,cpp}）
- 缺口：`registerSource` 一步解析+提交；唯一机器可读通道是扁平 Diagnostic；普通文件导入无任何预检/歧义/去重预测（CN/collection 有，普通文件没有）。
- 交付：只读 `ImportPreflight::inspect`，接生产件——ProductAdapterRegistry（新增 `adaptersAccepting()` 歧义面）、`inspectRaster/inspectVector`（规范元数据）、`localIdentityToken`（fail-closed 身份）、`DataManager::findByPath`（去重预测）。verdict：importable/needs_user_choice/refused/out_of_scope（远程拼写不探测，零网络）。17 个 typed finding code 带 remediation。零目录副作用有断言。
- RED：BASE 上测试 TU `COMPILE_EXIT=1`（`data/import_preflight.h：没有那个文件或目录`）。
- GREEN：`test_r25_b_import_preflight` 9 用例 ×2 轮（数值见 TEST_RESULTS.md）。

### F2 可解释推理计划与质量摘要 `sicnu.inference.plan/1` + `sicnu.inference.quality/1`（src/operators/runtime/inference_plan.{h,cpp}）
- 缺口：`runModelInference` 是唯一执行 seam 但无 plan/dry-run；运行后质量散落 payload/stats。
- 交付：`planModelInference` 只读干跑——与执行 seam **同一 gate 序列**（契约门/feature cube/runtime readiness/lane/head 契约/EO 预检/blend），每个 typed refusal 转为 error finding；网格/批量计划用引擎自己的静态规划器（`effectiveTileSize/effectiveHalo/effectiveBatchSize`）——计划与引擎不可漂移（metamorphic 断言：plan.tilesPlanned == engine.stats.tilesPlanned，含尾块几何 50×35/tile16）。session 获取计数与输出文件存在性双零断言（真只读）。`summarizeInferenceQuality`：覆盖/网格完整性（r24-B 尾批不变量）/类分布/检测抑制比/EO 存在性；空 stats typed 拒绝。
- RED：BASE 上 `COMPILE_EXIT=1`（头文件不存在）。
- GREEN：`test_r25_b_inference_plan` ×2 轮。

### F3 复现差异原因报告 v2（src/experiment/replay_deviation.{h,cpp}）
- 缺口：deviations 是扁平字符串——无领域归属、无可操作建议。
- 交付：`deviation_details[]`（dimension×domain[data/model/experiment/environment]×identity_pin×detail×remediation 非空）；**v1 字符串与 verdict 语义逐字保持**（#1547 identity-only gate 回归钉不变）；schema_version 1→2 向后兼容。
- RED：BASE 上 `COMPILE_EXIT=1`（`‘ReplayDeviationEntry’不是一个类型名`）。
- GREEN：`test_r25_b_replay_deviation_v2` ×2 轮 + 既有消费者 `test_mlops9_evidence` 回归。

### W7 跨层 E2E（tests/test_r25_b_end_to_end.cpp）
preflight→registerSource（生产 GDAL provider）→planModelInference→TileInferenceEngine.run→质量摘要→ExperimentRunRecorder（真实 SQLite）→ReplayDeviationAnalyzer v2 归因 model domain——仅模型 runtime 为注入 fake（无 ONNX/TRT），其余全生产路径。

## 实跑证据

- RED×3：BASE worktree + 生产编译旗 `-fsyntax-only`，三个测试 TU 全 COMPILE_EXIT=1（TEST_RESULTS.md 表格）。
- GREEN×2：`ctest -R "r25_b"` 两轮 + 邻域回归（r23_b/r24_b/test_data_manager/test_dataset_core/test_experiment_consistency_r4/test_model_runtime/test_mlops9_evidence/test_collection_import_service）——数值与退出码见 TEST_RESULTS.md。
- 构建闭包 EXIT=0（共享宿主 -j2、flock 单锁、ctest -j1）。
- 注册门：`python scripts/check_merge_registration_integrity.py --base origin/master --head HEAD --require-refs` exit 0（数值见 TEST_RESULTS.md）。
- `git diff --check` 干净。

## 性能（真实测量，无加速宣称）

F1 预检开销 vs registerSource、F2 plan 开销（只读解析级）、F3 v2 vs v1——各 5 次中位 + 同输入同输出 digest 一致性判别器（PERF_BASELINE.md；共享宿主墙钟只报数据不设硬门槛）。

## 双轮独立评审

- **R1**（fresh 子代理，全量 diff，REQUEST_CHANGES：1×P0+1×P1+5×P2+6×P3）：全部 P0/P1/P2 + 主要 P3 关闭（OpenCV 门、多输入通道核算按引擎 admission 语义、EO lane 保真+GeoError 全量、引擎门覆盖、generic 歧义修正、目录产品 verdict、qint64、scene/ensemble lane 诚实化）。处置表 `REVIEW_R1.md`。
- **R2**（另一 fresh 上下文，复审 R1 修复+并行合并风险+跨平台，APPROVE_WITH_FIXES：R1 修复自身引入 1×P2 + 6×P2 覆盖缺口 + P3 族）：全部关闭（per-feed dtype/roles/preprocess 按绑定 contract、temporal 帧元数+missing_timestep+dynamic-T、检测/scene/单输入 lane 契约门、窗口 admission 预算、ensemble tile 乘算、feed 绑定重名拒绝、测试钉）。处置表 `REVIEW_R2.md`（含 3 项测试侧修正的如实记录）。

## 跨轨说明

- 新增 API 均为 B 域写区（src/data、src/operators/runtime、src/experiment、src/geospatial/products registry 扩展一个只读方法、tests 末尾 append-only `# BEGIN R25-B` 块、.gitignore 末尾 allow-list）。
- D 轨可消费 F1 JSON 做导入向导；C 轨可暴露 F2 dry-run；契约说明在 CROSS_TRACK_FINDINGS.md。

## 风险与未执行

- GPU/TRT/OV 真机：NOT_EXECUTED（本构建无 SDK；F2 用注入 provider 的真实引擎路径验证）。
- Windows/macOS：NOT_EXECUTED（评审确认跨平台面：QFileInfo/QLocale 未用，路径处理 QString/GDAL 标准 API）。
- 线上 CI：按任务书不等待。

## Push/PR 状态

**BLOCKED（外部）**：push 时 GitHub 持续不可达（`TLS connect error: unexpected eof while reading`，api.github.com EOF；连续探测 >15 分钟、多协议 HTTPS 均 000；非本轨代码问题）。本地全部完成：分支 `agent/r25-B-data-model-runtime-20261028T140313Z` 11 commits ahead of origin/master（0 behind），worktree `/home/kevin/projects/rs-studio/exp-rs-r25-B-20261028T140313Z` 保留；网络恢复后一条命令即可完成：

```bash
git push -u origin agent/r25-B-data-model-runtime-20261028T140313Z
gh pr create --repo WindWang2/exp-rs --base master \
  --head agent/r25-B-data-model-runtime-20261028T140313Z \
  --title "r25-B: 地理空间数据资产、EO 模型推理与实验可复现工程深化" \
  --body-file .planning/zcode-r25-B/PR_BODY.md
```

不 auto-merge；不等线上 CI。
