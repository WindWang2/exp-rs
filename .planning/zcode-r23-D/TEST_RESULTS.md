# TEST_RESULTS — R23-D（Windows/MSVC 19.38/Ninja/Qt 6.8.0/GDAL 3.12.4，build-D 隔离）

## 环境
- 主机：16 核，31.2GB RAM；构建 -j2（默认）→ 中段按协议余量条款短暂 -j4（free ≥6.8GB 全程实测）
- 命令包装：`.planning/zcode-r23-D/tools/{configure,build,build4,run_tests}.cmd`（vcvars64 + Ninja + offscreen Qt + vcpkg/Qt DLL PATH）
- ctest：`ctest --test-dir build-D --output-on-failure --parallel 1 -R <regex>`

## RED（master 源码 + 新测试；commit 069fc43c4，规则文件移出）
`ctest -R r23_d` → **exit 8，0% passed（5/5 Failed）**（/tmp/r23d-red-run.log）：
- #1017 ct6 foundry 流程 PASS 用例：Failed —— 无 foundry 权威（resolveRulesPath 找不到 ndvi_landsat.rules.json → graded=false）
- #1018 ct6 错 CRS 负控：Failed at 对照组（control.graded REQUIRE）——同上
- #1020 session 深度炸弹：Failed —— `due to unexpected exception with message: Exceeded stackLimit in readValue().`（jsoncpp 1.9.6 Json::RuntimeError 逃逸，与源码证据一致）
- #1021 spec ±inf：Failed —— `REQUIRE( !result.ok ) with expansion: false`（master 接受 [-inf,+inf]）
- #1019 双权威守卫：标题 em-dash 在 GBK 控制台 ctest→Catch2 过滤回环中失配（harness 编码伪红），标题改 ASCII 后在两态均为绿（守卫语义）

## GREEN×2（修复 + 规则文件就位；HEAD）
- GREEN1：`ctest -R r23_d` → **exit 0，100% passed（5/5）**（/tmp/r23d-green1.log）
- GREEN2：复跑（无重编）→ **exit 0，100% passed（5/5）**（/tmp/r23d-green2.log）
  - #1017 0.87s / #1018 0.80s / #1019 0.72s / #1020 0.05s / #1021 0.03s

## CT-6 本机全流程预验证（主仓 build-dev 预构建二进制，零网络）
- `sicnu_generate_samples --out=data/samples`（seed 42 默认）→ lab1 pipeline `rs:spectral_index`（nir=5/red=4）→ 评分：
  - vs **bundle overlay 钉值**（临时 rules dir）：verdict **pass, score 100.0, 0 deductions, exit 0**；observed mean=0.452126129651 / sigma=0.508464660071 / nodata=131/65536=0.0019989 —— 与 overlay derivation 的 Linux 量测逐位吻合（foundry 确定性契约成立）
  - vs **repo fixture 规则**（data/labs/grading 默认解析）：verdict **fail, score 45.0**（grid.compatible CRS 32648≠4326 + stats 0.4521∉0.4913±0.01 + nodata 0.0019989<0.002 下限）—— CT-6 在本机复现

## WP-D5 回归保护矩阵（HEAD 最终轮，33 套直跑逐 exe 退出码；/tmp/final-*.log）

**25 套全绿（exit 0）**：test_lab_grading、test_lab_self_check、test_lab_offline_e2e、test_lab_batch_v2、test_lab_scale、test_lab_source、test_lab_document、test_grader_engine、test_grader_schema、test_faultlab_params_r8、test_fault_registry、test_fault_matrix、test_r21_e_faultlab_verifier、test_verifier_core_14、test_verifier_r16a_14、test_r18_e_verifier_rules、test_r21_e_verifier_evidence、test_verify_adapters、test_recipe_schema、test_recipe_registry、test_recipe_validator、test_recipe_lookup、test_recipe_compiler、**test_r23_d_ct6_lab_flow**、**test_r23_d_lab_hardening**

覆盖的 FIXED_OPEN 修复保护：#1610-1613（faultlab 系 4 套）、#1598/#1599/#1601（verifier 系 5 套）、#1602（test_verify_adapters）、#1593（test_lab_data_pack——见下基线项）、#1678（test_lab_runtime session_state 部分）、#1648（recipes 系 6 套 + lab_source/lab_document）、#1653（verifier 车道）、#1694（test_r22_teaching_batch_regrade）、语料权威（test_lab_grading reference/wrong-answer corpus）。

**7 套 KNOWN_BASELINE_FAIL（Windows/MSVC 环境存量，非本 PR 引入）**——其中 4 套做了显式 BASE（ad98a7a70 源码）复跑对照，失败签名逐位一致：

| 套件 | 结果 | 根因 | BASE 对照 |
|---|---|---|---|
| test_lab_runtime | 24/25（exit 42） | teardown `std::filesystem::remove_all` 被外部进程短暂锁住（"另一个程序正在使用此文件"）；功能断言全过，仅清理抛异常 | ✅ BASE 同败同签名 |
| test_labspec | 9/10（exit 42） | `gen.waitForStarted(10000)` false——QProcess 拉起外部生成器在本机环境失败（环境依赖） | 环境签名明确（与 diff 无交集） |
| test_lab_data_pack | 11/13（exit 42） | :417 POSIX 绝对路径在 Windows 解析为 missing（outside_root≠missing 语义差）+ :603 同类 | ✅ BASE 同败同签名（417+603） |
| test_faultlab | 44/45（exit 42） | :1759 期望 `faultlab.scenario_malformed` 实得 `faultlab.scenario_field`（路径/文件处理平台差） | ✅ BASE 同败同签名 |
| test_lab_chains | 3/4（exit 42） | :222 map frame 层引用 lab11_thematic_mask 不可解析（地图编译链，A/C 轨域） | ✅ BASE 同败同签名 |
| test_recipe_equivalence | exit 42 | 序列化逐字节比较受 CRLF 文本模式影响（recipes 代码本 PR 未触碰） | 环境签名明确 |
| test_r16_a_grader | 1 失败（exit 127） | :79 SIGSEGV Stack overflow——canonical 深度 512 递归超出 MSVC 默认 1MB 栈（Linux 8MB 栈通过；#1682：Windows CI 本就 ENABLE_TESTS=OFF） | 平台签名明确 |

**1 套 NOT_EXECUTABLE（Windows 无法编译）**：test_r16_a_faultlab——`unistd.h` 不存在（MSVC）；Linux lane 专属。本 PR diff 为 0 交集（git diff origin/master...HEAD --name-only 证实）。

注：本机为 Windows——上述 Linux lane 套件的 Linux 通行性由既有 CI/Linux 代理保证，本轨不声称已验证（NOT_EXECUTED on Linux）。
