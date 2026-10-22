# R20-05 TEST_RESULTS(终版)

基线 `28e1891390`;主机 Arch CachyOS(gcc 16.2.1,Qt6.12,jsoncpp 1.9.8,GDAL 系统 .so 39)。
CI 全仓失效(#1491)→ 全部本地;QT_QPA_PLATFORM=offscreen;每二进制连续两轮。

## 构建方式(重要披露)

共享构建锁被四条并行轨长时间占用(r20-02 全量构建持续多小时)。为不阻塞他人轨,本轨对**全部直接受影响的 7 个测试目标**采用了与 R2 评审员相同的**手工链接**方式:直接 g++ 编译本 worktree 的测试 .cpp + 其生产 TU 闭包(teaching_admin 全部 12 TU + grader 4 TU + verify 9 TU + verify_adapters 3 TU + geospatial 读取链 5 TU + lab 6 TU + platform + Catch2(r20-02 构建树的 v3 库,只读引用)+ 系统 Qt6/jsoncpp/GDAL)。RFC8785 canonicalizer 从 src/data/execution_fingerprint.cpp 按函数锚点逐字提取(canon_extract.cpp,仅存于 /tmp,不入库)。CMake 构建系统的全套件验证**未执行**(锁队列未轮到,排队任务已主动取消让位兄弟轨)——这是本 PR 的声明边界,不是谎报。

## 套件矩阵(本人环境 ×2 连续两轮,全部 exit 0)

| 套件 | 断言/用例 | run1 | run2 |
|---|---|---|---|
| test_teaching_admin_core(含 fake grader CLI 全流程) | 3442/37 | 全绿 | 全绿 |
| test_verify_adapters_grid(含新 [sampling][r20_05] 3 case) | 146/5 | 全绿 | 全绿 |
| test_r15_E_batch_refusal_accounting(恢复) | 62/9 | 全绿 | 全绿 |
| test_r15_E_teaching_stats_honesty(恢复) | 33/6 | 全绿 | 全绿 |
| test_r18_e_completion_ledger(含 nestbomb) | 36/4 | 全绿 | 全绿 |
| test_teaching_authoring_failures_r4(改钉引擎方言) | 30/7 | 全绿 | 全绿 |
| test_teaching_admin_rubric_weights_r16_c_t06(改钉) | 19/10 | 全绿 | 全绿 |
| **合计** | **3768/78** | **0 fail** | **0 fail** |

交叉印证:R2 评审员(独立子代理、独立编译)另行跑绿其中 4 套件(62/9、33/6、36/4、19/10)。

## 变异/等价 RED 证明(测试非恒真)

- 6 个独立驱动对 **base(master 28e1891390)** 全部红:stats 三断言、rubric 引擎合法被拒/扁平被收、sampling NOT-STAMPED×2 形、nestbomb SIGABRT(134)、discovery skips=0;同驱动对 HEAD 全绿。原始输出:red-drivers/RUN_LOG.md。
- 套件级:恢复的两个 r15-E 套件在 base 上无法编译(生产 API 缺失)且语义测试(rb/sv)在 base 红——恢复的断言确实钉住本轨改动。

## NOT_EXECUTED / KNOWN_BASELINE_FAIL

- CMake 构建系统全量域 sweep(68 目标清单):未执行(锁未轮到;7 个直接受影响目标已由手工链接双绿覆盖,admin_core 3442 断言含批量/判分/预检/打包回归面)。后续轮在锁可用时补。
- test_teaching_foundation_e2e(重依赖 experiment/dataset 链):未执行(同因)。
- CI(#1491)、Windows/macOS lane、TSan:未执行。
- KNOWN_BASELINE_FAIL:无(本轨改动闭包内零已知红)。
