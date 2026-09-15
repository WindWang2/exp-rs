# EVIDENCE — F20 context-help-diagnostics-11

只记录本地可复现证据（命令 + exit code + 关键输出）。不引用在线 CI。

## 环境

- 构建目录：worktree `build-dev`（preset dev-default，Debug，ENABLE_TESTS=ON）。
- 网络限制：`git clone https://github.com/...` TLS EOF（多次重试复现）；`gh` 正常。解决：`GIT_CONFIG_GLOBAL=/tmp/f20-gitconfig` 将 pybind11/Catch2 URL insteadOf 到主仓库 build-dev/_deps 本地源（版本与 pinned tag 一致：pybind11 v2.13.6=a2e59f0e，Catch2 v3.7.1=fa43b774）。FetchContent _deps 漂移问题（复制 _deps 残留旧 CMakeCache）→ 删除复制体改用 URL 重写，configure 通过。
- 资源监控：build `-j2`，test `-j1`，`QT_QPA_PLATFORM=offscreen`；`CMAKE_BUILD_PARALLEL_LEVEL=2`。

## 基线测试事实（master @ a5b11b7f10，无本 track 改动）

- `test_help_core`（构建于基线代码）：**RED** — `QT_QPA_PLATFORM=offscreen ./build-dev/tests/test_help_core`，exit 42；11 个 test case 中 10 过 1 失败；失败点 tests/test_help_core.cpp:256 `CHECK( result.errors.isEmpty() )`；错误流为 `:/help/terms/rs_glossary.json: invalid id ''` ×307（glossary 被 HelpContentStore 当内容页加载）。根因：8df5111b6c 把 glossary 别名进 `/help` 前缀，而 loadFromResources 递归迭代 `:/help` 全部 *.json。log: /tmp/f20_baseline_help_core.log
- 源码级确定性推导（待运行确证）：
  - `test_command_contract_9`（Help↔registry 双向、白名单空）应红：commands.json 缺 10 个已注册命令页（cartography.compose/export/preflight/repair、workbench.cartography/classifyStudio/georefDual/ir2Pipeline/operatorCatalog/visualAnalytics）。
  - `test_diagnostics_contract_9` census 应红：harness kEntries=41 码 vs diagnostics.json 28 页 → 12 码无页且不在白名单（IO_ERROR、TEACHING_REFUSAL*、COMPLEX_BANDS_REQUIRED、ACQUISITION_DATES_MISSING、DATES_NOT_ASCENDING、UNWRAP_PROVIDER_UNAVAILABLE、WAVELENGTH_INCOMPATIBLE、TEMPORAL_MISALIGNMENT、CATEGORICAL_MISMATCH、RESOURCE_OVER_BUDGET、OUTPUT_PATH_COLLISION、NONDETERMINISTIC_CHAIN、FACT_CONFLICT；*在白名单）。重测差集脚本输出见上（12 项 missing）。
  - `test_help_coverage` 反向检查应红：扫描前缀集合缺 workflow → commands.json 已有 command.workflow.* 5 条不在前向扫描结果内。
- 后续 7 个目标二进制仍在基线构建中；构建完成即补各目标实际 exit。

## Phase 记录

（每 Phase commit 后追加）

## OUT_OF_SCOPE（阻断性）

- **master @ a5b11b7f10 的 sicnu_agent 无法编译**：`src/agent/data_platform_tools.cpp` 有 4 处未限定引用（3× `BenchmarkService`、1× `benchmarkRunStatusToString`，均为 `sicnu::experiment::` 域），由 #992 的 1ae474541f 引入，全仓无任何 using-directive/别名。影响：所有链接 sicnu_agent 的目标（GUI、全部 sicnu_add_test 目标）均无法构建。本 track 施加**最小阻断修复**（4 处补全 `sicnu::experiment::` 限定，与文件内既有全限定惯例一致），随首个 commit 提交并在 PR_BODY 顶部声明。修复验证：sicnu_agent 编译通过。
- 构建环境备注：同机有并行 track（exp-rs-deployment-packaging-11）在构建，负载长期 >4；本 track 严格 -j2。曾出现被中止构建的 make 子进程与新构建竞态导致 .o 缺失（已通过单目标重建修复）；此后中止构建前确认无孤儿 make。
- Issues #1001–#1007：dataset/workflow/io/georef 域 bug，非帮助体系；不认领（BASELINE.md 有 dedupe 记录）。
- data/help JSON 英文化：违背 repo 中文优先契约且无消费方（DECISIONS D5）。
- WorkbenchGuidance 无消费方：组件属于 app 空态引导面，接线涉及 D18 workbench 布局（#991 已合入但其布局仍在演进）；本 track 在 census 文档登记，不改 workbench 布局（P3 disposition 见 REVIEW_LOG）。
- HelpViewerDialog（USER_GUIDE.md）与 HelpCenterDialog 并存：合并查看器属 UI 信息架构重设计，超出本 track 最小增量原则；以「HelpCenter 话题页外链 USER_GUIDE 锚点」为最小互补（若实现）或在 REVIEW_LOG disposition。


## Round 1（改动后首轮）与 Round 2 结果

- R1（修复 3 处编译/数据问题前）：test_i18n 1 case 红（XML 解析方式缺陷）；test_help_coverage 2 case 红（①composition dangling: policy_refused→workbench.plugin_manager 引用未注册主题——master 上即存在的数据缺陷，因 master 无法构建该套件而从未暴露；②zero-diff 全 5 页漂移）；test_ux_guidance_corpus 1 case 红（workbench.obia facts 盲区——即本 track 要修的缺陷，已在 requirementFacts 修复）；其余 6 套件绿。
- 修复：①policy_refused related 改指 command.workbench.operatorCatalog；②SICNU_REGEN_HELP_DOCS=1 再生 5 页（+1945/-117，证实提交物漂移）；③obia 案例入 requirementFacts；④i18n gate 改用 lupdate 稳定输出的正则提取 + 实体解码。
- R2：**9/9 套件全绿**，合计 17,032 断言（明细见 TEST_MATRIX）。资源：build -j2（并行 track 同机构建，load 4-7），test 串行 offscreen；全套件墙钟 < 3 分钟。
