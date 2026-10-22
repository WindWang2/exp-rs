# R20-05 ORACLES

BASE = master `28e1891390`;HEAD = 本分支(终码见 PR 正文)。
全部独立驱动源码与原始输出归档:`.planning/zcode-r20-05/red-drivers/`(RUN_LOG.md + 4 个 .cpp)。

## 提前独立 RED/GREEN(构建锁排队期间,直接 g++ 链接 jsoncpp/Qt6Core/GDAL 生产 TU;非最终测试套件证据)

| WP | 驱动 | BASE(红) | HEAD(绿) |
|---|---|---|---|
| WP-ML course_home%/feedback indeterminate | red_driver(base=master_base worktree 28e1891390 三断言全失败) | `overallPercent==100`→base 得 0;`rowStatus=="indeterminate"`→base 得 fail;`overallStatus`→fail。EXIT=0(驱动约定 0=全失败即 RED) | 三断言全成立 EXIT=1(反号=GREEN);另 dbg 直查:base 105-case overallPercent=105(未钳制) |
| WP-RB #1652-1 | rb_driver | engine-legal rubric ok=0(被拒)/flat 方言 ok=1(被收),EXIT=1 | engine-legal ok=1 / flat 被拒,EXIT=0;调试期发现引擎 passingScore 默认 60>totalPoints 即拒(引擎 fail-closed 契约,测试显式钉 passingScore,不改引擎) |
| WP-SV #1600 | sv_driver(100×100 GTiff,9×32×32=9216/10000 px) | evidence 无 hasSampling → NOT-STAMPED,EXIT=1 | hasSampling=true + sampledPoints=9216,EXIT=0 |
| WP-JP #1607 残余 | jp_driver(300 层嵌套) | **SIGABRT(134)**:uncaught `Json::RuntimeError: Exceeded stackLimit in readValue().` | ledger issuesZh 类型化拒绝 + digest 空串拒绝,EXIT=0 |

## 验收 Oracle 状态(提示词 §8)

1. 评分通过/失败可定位:WP-RB 使管理端与引擎同一 rubric 真值(错误带引擎 path/message);WP-SV 证据盖章。
2. verdict/seq 一致:#1664 已交付,本轮未触碰其路径(回归 sweep 覆盖)。
3. 故意破坏 fixture 证红:rb/sv/jp 驱动 + 恢复的双绿(见 RUN_LOG);无 CHECK(true) 新增。
4. 无架构口号重构:#1601 未动(记录证据交 maintainer);#1652-1 用委托而非双写。

## 最终套件证据(待构建锁释放后回填)

- [ ] test_r15_E_teaching_stats_honesty 全绿 ×2
- [x] test_r15_E_batch_refusal_accounting 全绿(9 case / 62 断言)——R2 评审员独立构建+运行证实(disc_driver 另证 base 0 skips RED → head 4 skips GREEN);[待本轨锁内构建复跑 ×2]
- [x] test_teaching_admin_rubric_weights_r16_c_t06 全绿(10 case / 19 断言)——R2 评审员独立运行证实;[待复跑 ×2;admin_core/authoring_failures_r4 待构建]
- [x] test_r18_e_completion_ledger 全绿(4 case / 36 断言,含 300 层 nestbomb)——R2 评审员独立运行证实;[待复跑 ×2]
- [x] test_r15_E_teaching_stats_honesty 全绿(6 case / 33 断言)——R2 评审员独立运行证实;[待复跑 ×2]
- [ ] test_verify_adapters_grid(含新 [sampling][r20_05] 3 case)——sv/sv2 驱动证实盖章行为;[待构建运行 ×2]
- [ ] 域回归 sweep(教学/评测族,见 BASELINE.md 68 目标清单,可执行子集)——[待本轨锁内构建]

注:上表"discovery 半区 master 已绿"为初版误判(R1-P0 已推翻:master 0a8f56a28d 仅记账保留名,808f1c4c52 显式维持 hidden/nested 不可见;base 实测 0 skips)。本行更正即 R2-P3-2 处置。
