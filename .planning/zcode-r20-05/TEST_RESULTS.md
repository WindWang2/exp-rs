# R20-05 TEST_RESULTS

基线 `28e1891390`;主机 Arch CachyOS(gcc 16,Qt6.12,jsoncpp 1.9.8,GDAL 系统 .so)。
CI 全仓失效(#1491)→ 全部本地;构建经全局锁 `main/.git/exp-rs-zcode-global-build.lock`(-j2),测试二进制 -j1,QT_QPA_PLATFORM=offscreen。

## 构建链
| # | 内容 | 结果 |
|---|---|---|
| 1 | configure(系统依赖配方,Unix Makefiles,Debug,ENABLE_TESTS=ON) | <FILL:锁排队时长+exit> |
| 2 | 定向构建受影响测试目标(8 套件 + 依赖闭包) | <FILL> |

## 套件矩阵(目标 ×2 连续两轮)
| 套件 | run1 | run2 | 备注 |
|---|---|---|---|
| test_r15_E_teaching_stats_honesty | <FILL> | | R2 评审员已独立跑绿 6 case/33 断言 |
| test_r15_E_batch_refusal_accounting | <FILL> | | R2 独立跑绿 9/62;disc_driver 另证 |
| test_teaching_admin_core | <FILL> | | 含 fake grader CLI |
| test_teaching_authoring_failures_r4 | <FILL> | | |
| test_teaching_admin_rubric_weights_r16_c_t06 | <FILL> | | R2 独立跑绿 10/19 |
| test_r18_e_completion_ledger | <FILL> | | R2 独立跑绿 4/36(含 nestbomb) |
| test_verify_adapters_grid | <FILL> | | 含新 [sampling][r20_05] 3 case |
| 域 sweep(教学/评测族其余) | <FILL> | | 按 BASELINE 68 目标可执行子集 |

## 独立驱动(不经构建系统;red-drivers/RUN_LOG.md 原始输出)
5 驱动 × base/HEAD 双跑全记录(见 ORACLES.md 表)。

## 变异探针(测试非恒真证明)
<FILL:disc/rb/sv 驱动在 base 上红 = 变异等价证据;套件级变异(故意破坏 fixture)于构建后补>

## NOT_EXECUTED / KNOWN_BASELINE_FAIL
- CI(#1491)、Windows/macOS、TSan 未执行。
- <FILL:域 sweep 发现的预存红(如有)>
