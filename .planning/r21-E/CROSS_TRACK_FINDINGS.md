# R21-E CROSS_TRACK_FINDINGS — 跨轨记录（不抢修）

## CT-1（构建环境，全体轨受益）：vendor prefix 的静态 expat/sqlite 破坏 qgis_core 共享库链接
- **现象**：`CMAKE_PREFIX_PATH=/home/kevin/project/paleo-singlefactor-closure/vendor/prefix/usr/lib/x86_64-linux-gnu` 使 FindEXPAT/FindSQLite 解析到 prefix 里的 **静态 .a（无 -fPIC）**，链接 libqgis_core.so 时 `relocation R_X86_64_PC32 ... can not be used when making a shared object`。
- **修法（本轨已验证）**：显式钉系统共享库：
  `-DEXPAT_LIBRARY_RELEASE=/usr/lib/libexpat.so -DSQLite3_LIBRARY=/usr/lib/libsqlite3.so`（仅设 `EXPAT_LIBRARY` 无效——find 模块走 RELEASE 变量）。
- **期望所有者**：任何需要 qgis_core 链接的轨（B/D 尤甚）。

## CT-2（共享锁）：`/tmp/exp-rs-r21-build.lock` 被轨 D 全量构建长期持有（07:08 起 `make -j2 -k`）
- 本轨增量链接排队等待。协议设计如此（防叠加并行）；各轨长全量构建时建议分 target 增量构建而非 `make -k` 全量，减少锁驻留。

## CT-3（#1597/#1596/#1595，CLI 轨）：grade_all.cmd 依赖的 CLI flag 解析族在 src/cli
- 本轨 e2e 只调用既有 CLI 入口（零 src/cli 改动）。若 CLI 轨修 flag 解析，`lab --grade/--batch` 的退出码契约（0/1/2/3）与 CSV 形状是本轨 e2e 的依赖面，勿破坏。

## CT-4（app 轨/3-4）：experiment_studio_dock 调 studio_live 的 fault 教学流
- 本轨在 studio_live 装了 faultlab verifier 生产钩子（`runFaultScenarioTeaching`）。dock 侧无需改动即获得行为（声明 verifier 的场景真实评分）；若 dock 展示 report JSON 的新 `verifier` 块，属可选增强。

## CT-5（#1601 的 maintainer 记录）
- split 已在两处 CMakeLists 文档化（权威=src/verify）；R21-E 不搬库不删库，走"生产接线"路径把权威核证据接进 OutputVerifier 教学报告（additive `verifier` 字段）。ADR 0172 正文如需更新归属措辞，留 maintainer（docs/adr 非本轨白名单）。

## CT-6（教学内容缺陷，证据充分）：文档化离线 lab1 流程无法通过自身规则
- **实证**（e2e_offline_lab.sh，Linux）：`sicnu_generate_samples --out=data/samples`（profile=lab 默认 **256×256, EPSG:32648 UTM, 7 波段 Landsat**）→ `rs:spectral_index` NDVI → `lab --lab ndvi_basics --grade` 得 **score 45/100**：
  - `grid.compatible` fail（width/height 256≠128、crs EPSG:32648≠4326、pixel 30m≠0.001°）
  - `stats.ndvi_mean_sigma` fail（mean 0.4521 vs 钉 0.4913±0.01；sigma 0.5085 vs [0.39,0.41]）
  - `mask.nodata_ratio` fail（0.001999 vs [0.002,0.05] 下界）
- 而 `data/labs/grading/ndvi_basics.rules.json` 的 derivation 明示规则派生自 **128×128 EPSG:4326 合成水/植被场景**（= tests/fixtures/lab/ndvi_basics_reference.tif，评分 100/100）。
- **影响**：`scripts/windows/run_lab.cmd`、`packaging/bundle/labs/lab1/INSTRUCTIONS-zh.md` 文档化的"生成→跑流水线→批改"学生流程，在当前样例生成器下必然低于及格线——新生第一次上课即看到 FAIL。
- **期望所有者**：教学实验内容/lab-pack 维护者（tools/sicnu_generate_samples 的 lab profile 与 data/labs/grading 规则集二者需对齐：要么生成器提供 128×128 EPSG:4326 合成场景的 spec，要么按生成器场景重新派生规则闭式）。本轨不改 tools/ 与 data/labs 灰区，仅实证记录。
- **本轨应对**：e2e 的评分/批量/审计步骤改用**权威参考工件**（规则派生场景），全链路 PASS；pipeline 步骤保留为"学生流水线可离线运行"的证明，分数差异如实呈现。
