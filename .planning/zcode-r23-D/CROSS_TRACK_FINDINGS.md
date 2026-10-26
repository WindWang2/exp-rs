# CROSS_TRACK_FINDINGS — R23-D 跨轨记录（不越权改，只提交可复现事实）

## CT-D1（→ E 轨，scripts/ + tools/）：repo 学生流程脚本仍指向 fixture 权威（CT-6 的 E 侧一行修复）
- 事实：`scripts/windows/run_lab.cmd` 第 3 步 `lab --lab ndvi_basics --grade output\lab1_ndvi.tif`，而其第 1/2 步生成并处理的是 foundry 场景（256×256 EPSG:32648，seed 42）。`ndvi_basics` 规则派生自 128×128 EPSG:4326 提交 fixture（tests/fixtures/lab/ndvi_basics_reference.tif），因此该脚本在 master 上必然 45/100（R21-E CT-6 实证，本轮已复核仍 LIVE）。
- 本轨 D 侧修复（本 PR）：新增 foundry 场景权威 `data/labs/grading/ndvi_landsat.rules.json`（与 offline bundle overlay 同校准）+ 回归测试 test_r23_d_ct6_lab_flow（foundry 流程 PASS / 错 CRS FAIL / 双权威互斥守卫）。
- 建议 E 轨一行改动：`run_lab.cmd` 第 3 步改 `--lab ndvi_landsat`（或加 `--rules-dir` 指到 foundry 校准目录）。bundle 侧（packaging/bundle）无需改动——bundle 用自己的 grading-overlay，已校准。
- 复现：`scripts/gen_samples.cmd` → `sicnu_geo_rs_cli --offline --pipeline packaging\bundle\labs\lab1\lab1_ndvi.pipeline.json` → `sicnu_geo_rs_cli --offline lab --lab ndvi_basics --grade output\lab1_ndvi.tif`（exit 1，45/100）；同工件换 `--lab ndvi_landsat` → PASS（见 TEST_RESULTS.md）。

## CT-D2（→ C 轨，src/teaching_admin/）：#1648 的 curriculum_editor 部分已修
- 事实复核：curriculum_editor.cpp:226-243 已用 readBounded + kMaxLabJsonBytes=16MiB（a1ffd5c66 帧内修复）。无需动作；仅记录以免其他轨重复修。

## CT-D3（信息，无动作）：#1601 双验证器归属
- src/verification（本轨）为测试接线的并行核；生产权威 = src/verify（C 轨，ADR 0172）。本轨不搬库不删库；test_r23_d_ct6_lab_flow 经 OutputVerifier（src/agent）→ verify_adapters composer（本轨）→ src/verify 权威的现有链路验证，未新增第二套评分器。
