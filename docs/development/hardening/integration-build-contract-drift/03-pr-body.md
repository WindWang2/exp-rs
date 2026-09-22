# PR body — fix(build): wire six merged-but-unwired modules, register their 19 tests, add a wiring drift oracle

## 目标（Track: integration-build-contract-drift, hardening 01/20）

收敛 RS14 大批量合并留下的"源码在、测试在、但 target 未注册/未链接/未进入构建图"问题：
六个模块合并时从未接入构建系统，18 个测试源文件从未注册，master 默认 `cmake --build`
（target `all`）是坏的。只补接线，不创造新模块、不改生产行为。

## Recon 基线

`origin/master` = `a9dc33fa73`（#1236 merge）。启动时 open PR：#1237（teaching cockpit，
尊重其 ownership）＋本任务进行中新增的 #1238/#1239/#1240。open issues：0。
完整现状矩阵与证据：`docs/development/hardening/integration-build-contract-drift/01-recon.md`。

## 根因（机械 inventory + 实证）

| 模块 | PR | 接线缺陷 |
|---|---|---|
| `src/verify` (sicnu_verifier) | #1191 | CMakeLists 无任何 add_subdirectory 可达 |
| `src/repair_planner` (sicnu_repair_planner) | #1194 | 同上 |
| `src/agent_loop` (sicnu_agent_loop) | #1201 | 同上；5 个 test_agent_loop_* 链接幻影 target |
| `src/recipes` | #1206 | 目录根本没有 CMakeLists |
| `src/preflight` (sicnu_preflight) | #1207 | CMakeLists 不可达 |
| `src/study/bridge` (sicnu_study_bridge) | RS14-07 | CMakeLists 不可达 |

- 实证（修复前 master）：`cmake --build build-dev --target test_agent_loop_core` 双重失败
  —— `fatal error: agent_loop/session_state.h: No such file or directory` ＋
  `cannot find -lsicnu_agent_loop`。configure 阶段静默通过（幻影 target 被当作
  `-l` 旗标传给链接器），所以配置绿、`all` 必炸。
- `git log -S "src/verify" -- CMakeLists.txt` 等为空：这些模块**从未**被接线，不是回归。
- 18 个未注册测试：`test_verifier_schema`、`test_preflight_report_schema`、
  `test_repair_planner_schema`、`test_recipe_{schema,compiler,validator,registry,lookup,equivalence}`、
  `test_lab_document`、`test_lab_source`、`test_study_e2e`、`test_curriculum`、
  `test_explain_schema`、`test_faultlab`、`test_grader_schema`、`test_lab_runtime`、
  `test_sci_inspector`（815 个 `tests/test_*.cpp` 的词边界全量核对）。
- 为什么 CI 没拦住：master 最近 8 次 CI 全部 `completed/cancelled`（1–5 分钟）或
  `queued`>1h —— merge 未等待绿 CI。
- `test_sci_inspector` 的被测源 `src/app/workbench/scientific/sci_inspection.cpp`
  同样不被任何 target 编译；本 PR 让测试直编该文件（`test_interaction_tools` 先例），
  不动 #1237–#1239 正在改的 `src/app/CMakeLists.txt`。

## 改动清单

- `CMakeLists.txt`：`add_subdirectory(src/experiment/debugger)` 后一个连续块接入 6 模块
  （插入点与 #1240 的 scientific_state 之后不同区，可语义 union）。
- `src/recipes/CMakeLists.txt`（新）：`sicnu_recipes` STATIC 叶子，jsoncpp 解析块与
  sicnu_grader/sicnu_verifier 同构。
- `tests/CMakeLists.txt`：注册上列 19 个 target（18 个缺失 + oracle）。纯叶子测试走
  既有裸 `add_executable` + `Catch2::Catch2WithMain` + 模块库模式（模块 PUBLIC jsoncpp
  传递提供 include/链接）；`test_study_e2e`/`test_curriculum` 走 `sicnu_add_test` 全栈。
- `tests/test_build_wiring_drift.cpp`（新）：结构 oracle，见下。
- 删除 `src/gui/codeeditors/qscilexer_stubs_moc.cpp`（死文件：全树零引用、不参与任何
  构建；其注释宣称的 AUTOMOC 触发机制已被 src/gui/CMakeLists.txt 直接列 stub 头为源
  取代）。link 中性。

## 结构 oracle（test_build_wiring_drift）

三规则，全部从仓库文本机械推导：① 每个 `src/**/CMakeLists.txt` 从 root 经
add_subdirectory 文本边可达（变量/绝对路径边 fail-open 跳过）；② 每个 `src/**/*.cpp`
被"合格"脚本按 token 引用——裸 basename 限本模块作用域（最近含 CMakeLists 的祖先）或
src/ 之外，路径限定 token 全局后缀匹配，注释先行剥离；③ `tests/test_*.cpp` 的 stem
以整词出现在 tests/CMakeLists.txt。三个有意保留的源（qwt 离线 stub lane、两个
app maptools 延迟集成文件）进显式豁免集，各附理由。

## 测试证据（本机 dev-default Debug，-j2）

- 修复前：`test_agent_loop_core` 构建 RED（见根因）。
- 修复后轻量 17 个 target 全部构建成功，二进制直跑**连续两遍 17/17 PASS**，合计
  ~1800 断言（含 verifier 126、faultlab 492、lab_runtime 512、equivalence 79）。
  `test_recipe_equivalence` 初跑暴露缺 `CMAKE_SOURCE_DIR` 定义，补上后 5/5 case 过。
- `test_build_wiring_drift` 在仓库内 GREEN。
- 杀伤力证明（mutation/sabotage，各自验证后还原）：
  1. 从 src/agent_loop/CMakeLists.txt 删除 `session_state.cpp` 行 → oracle 红并点名
     该文件（兄弟模块同名文件/注释遮蔽均不再有效，S4 专门验证注释路径已封闭）；
  2. 删除 root 的 `add_subdirectory(src/verify)` → 红并点名 src/verify/CMakeLists.txt；
  3. 删除 test_recipe_schema 注册块 → 红并点名 test_recipe_schema。
- 全部六个此前不可构建的库均已编译成功并通过链接（含 Qt lane 的
  `libsicnu_study_bridge.a`——对 `Sicnu::study` + `sicnu_task_center` 的链接闭合）。
- 未在本机完成：`test_study_e2e`、`test_curriculum` 两个全栈测试二进制及既有
  contract/census gates 的运行——vendored qgis_gui/agent 栈在本机（20 个 campaign
  任务并发、load~30、-j2 纪律）未能及时编完，构建验证在 `libsicnu_study_bridge.a`
  链接成功后按指示停止。三个库对注册表/算法数据面零改动，既有 contract gates 的
  输入不受本 PR 影响（determinism census 快照不含构建 target，已核对）。
- 预存告警：`/usr/include/qt6/Qca-qt6/QtCrypto/QtCrypto: not a directory` 为环境级
  include 路径告警，所有测试 target（含既有）同样出现，非本 PR 引入。

## 性能/资源

纯接线与文件文本扫描 oracle（仓库 ~150 个脚本、~2.4k 源文件，测试耗时 <1s）。
无算法/数据面改动。

## 独立 review

独立只读 adversarial reviewer 全 diff 审查：初始判定 READY，P0=0 P1=0；两个 P2
（check-2 basename 跨模块遮蔽、扫描吃进 build-dev 非密闭且慢）与三个 P3（regex
变量边、tests 递归、误导性排序注释）全部修复；reviewer 复验结论见 PR 评论/recon
文档追加。修复过程还暴露并封闭了真实注释遮蔽案例（tests/CMakeLists.txt:4663 注释
中的 session_state.cpp 曾足以伪造覆盖）。

## 与 open PR / issue / 旧分支去重

- #1237/#1238/#1239/#1240：均改 root/tests CMake，但各自插入点不同区域；本 PR 两个
  中央文件 hunk 保持单一连续块，append-only 语义 union。`src/teaching/**`、
  `src/app/teaching/**` 未触碰。
- 无 issue 重叠（0 open）。
- `rs14-unified-verifier` 等旧分支仅作线索矿，未移植任何代码；本 PR 的 src/verify
  接线复用已合并的权威实现。

## 未做事项（按类）

- 已被其他 Track 拥有：teaching/shell 接线（#1237–#1239），science context（#1240）。
- 无法在本机复现：线上 CI runner 的 cancelled/queued 异常（host 端问题，建议
  维护者检查 runner 池；本 PR 的全部验证在本地完成）。
- 需要真实平台环境：Windows/macOS lane（本机 Linux；本 PR 不触碰平台相关代码）。
- 明确未来方向（未实现）：src/agent_loop 注释宣称的 Qt 生产 adapter
  （src/agent/tools/agent_session_adapter.*）、verify 的 harness/exp-prov/workflow
  adapters、maptools 与 qwt stub lane 的接线——均为产品决策，非接线清理。

## 已知限制

- oracle 为文本级过近似：变量组合的 add_subdirectory 边、引号内 '#' 会被跳过/
  过度剥离（方向为 fail-open，仅影响未被 CI 覆盖的假想场景，现树无此形态）。
- 注释剥离使"只在注释中列出的源"不再算已接线——这是有意收紧。
- `test_study_e2e`/`test_curriculum` 的运行证据缺失是本 PR 的主要已知限制：二者
  的注册与既有 sicnu_add_test 模式逐字一致，其依赖库（Sicnu::study、
  sicnu_task_center、sicnu_study_bridge、sicnu_agent）均已在本机编译成功；
  建议合并前在有完整 Qt 栈缓存的机器上补跑这两个二进制。

## 回滚

`git revert` 四个提交即可；无数据迁移、无 API/ABI 变化、无生产行为变化。
