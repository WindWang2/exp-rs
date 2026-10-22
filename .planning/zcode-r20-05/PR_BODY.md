# hardening(r20-05): 教学实验反馈真值与批量证据闭环 — merge-loss 恢复 + 评分/校验器契约收敛(双轮独立评审)

## 基线与范围

- BASE:`28e1891390`(fetch 实测 origin/master;无开放 PR,PR #1662–#1667 全部已合并)
- HEAD:`9603a4f35`(分支 `agent/r20-05-teaching-verification-20261022T190641Z`,独立 worktree)
- 写区:src/teaching、src/teaching_admin、src/app/teaching{,_admin}、src/verify、src/verify_adapters、tests(教学/评测族 + append-only 注册)、.planning/zcode-r20-05;逐文件对照见 OWNERSHIP.md

## 交付(4 工作包 + 双轮评审处置)

### 1. merge c8f5e0805422 教学侧丢失恢复(R19-02 CT-1 的本轨半区)
git 考古实证:该 merge(r16-b 并入 PR #1517 的 r15-E 侧)丢弃教学修复与两个回归测试文件,`course_home_view_model.cpp`/`teaching_admin_dock.cpp` 至今 == merge-base:
- **course_home 进度真值**:overall_percent 钳制 [0,100] + 从 completed-lab 集合按 labId 去重派生展示百分比(上游百分比只是 hint;腐坏的 105 / 陈旧的 0 不再逐字渲染)
- **反馈真值**:grader 证据缺/非布尔 `passed` → `indeterminate`(不再把从未评过的断言判 fail 并聚合翻整实验未通过)
- **批量发布报告 fail-closed 加载器** `loadBatchReportFromFile`(有界读、非对象行拒绝、total/rows 不一致警告)+ 控制台两调用点改用之:截断/半同步/无关 JSON 报错,不再渲染"0 份作业 / 0% 完成"
- **discovery 记账**(R1 评审 P0 补全):hidden 学生目录/hidden 文件(含根级、学生目录内)/嵌套班级目录 → 类型化可归因 DiscoverySkip 拒绝,"graded 5/5 而班上 6 人"的静默蒸发不再
- 恢复 `tests/test_r15_E_teaching_stats_honesty.cpp`(6 case)与 `tests/test_r15_E_batch_refusal_accounting.cpp`(9 case;append-only 注册)

### 2. #1652-1 教学管理台 rubric 校验委托引擎(ONE rubric truth)
`validateProcessRubric` 原校验引擎从不消费的扁平 `criteria[]` 方言(weightΣ1.0/metricKey/answerKey):引擎接受的 rubric 被管理台判非法(release_preflight 发布门随之误拦),管理台放行的被引擎拒。现经 grader 严格 reader 委托 `GradingRubric::fromJson`,错误映射带引擎 path/message;仓库无任何存量 sicnu.grader.rubric/1 文档(R2 全查 data//examples//models//tools/),无兼容性回归。三套件改钉引擎方言。

### 3. #1600 网格分数采样盖章
probe 的 3×3 窗格算出的 nodata/finite fraction 是有界估计,却被 verbatim 写进证据当整幅事实比对 pin;`hasSampling/sampledPoints`(自引入即序列化+roundtrip)全仓无生产者。现 GridInfo 携带 fractionsFromSample/sampledPixels,census 判定按**逐轴连续性**(extent ≤ 3×window;R1-P2:细长栅格 200×40@axis32 采样数 9216≥像素 8000 但长轴列 32..84/116..168 未读,按计数比较会假称 census),证据全分支盖章并进入封印 digest。

### 4. #1607 残余:教学面 jsoncpp 解析钉定(5 站点)
lab_completion_ledger、verdict_stamp、lab_operator_launch×2、lab_cockpit_dock(stackLimit=256 + typed catch):300 层嵌套炸弹从 SIGABRT(base 实测 134)变为类型化拒绝/空摘要拒绝。

## 证据(全部本地;CI #1491 失效,不等 CI)

**独立驱动 base-RED / HEAD-GREEN**(`.planning/zcode-r20-05/red-drivers/RUN_LOG.md`,直接 g++ 链接生产 TU):
| 驱动 | base(28e1891390) | HEAD |
|---|---|---|
| red(ML 语义) | 三断言全失败(stale 0%/105 未钳制/passed 缺失判 fail) | 全成立 |
| rb(#1652-1) | 引擎合法 rubric ok=0 / 扁平 ok=1,EXIT=1 | ok=1 / ok=0,EXIT=0 |
| sv/sv2(#1600) | NOT-STAMPED,EXIT=1 | hasSampling+sampledPoints=9216(100×100 与 200×40 两形),EXIT=0 |
| jp(#1607) | **SIGABRT(134)** uncaught Json::RuntimeError | 类型化拒绝,EXIT=0 |
| disc(记账) | skips=0 静默蒸发,EXIT=1 | skips=4 每路径恰一次,EXIT=0 |

**套件运行(本人环境,手工链接生产 TU 闭包,连续两轮全 exit 0)**:test_teaching_admin_core **3442 断言/37 case**(含 fake grader CLI 全流程)、test_verify_adapters_grid 146/5(含新采样 3 case)、test_r15_E_batch_refusal_accounting 62/9、test_r15_E_teaching_stats_honesty 33/6、test_r18_e_completion_ledger 36/4(含 nestbomb)、test_teaching_authoring_failures_r4 30/7、test_teaching_admin_rubric_weights_r16_c_t06 19/10 —— 合计 **3768 断言 ×2 轮零失败**。R2 评审员独立编译另跑绿其中 4 套件交叉印证。构建方式与边界(共享锁被四轨占用,故用 g++ 手链而非 CMake 全量;CMake 域 sweep 未执行)详见 TEST_RESULTS.md 披露。

## 独立评审
- R1(fresh,REQUEST-CHANGES:P0×1/P2×1/P3×6)全处置于 7b1d6a2778:P0=discovery 记账块未恢复(恢复+纠正错误声明);P2=census 条件改逐轴连续性;P3=loader total 警告/文案去重/同面站点/死 API/gitignore。见 REVIEW_R1.md
- R2(fresh #2,READY-WITH-FIXES:零 P0-P2,P3×5)处置于 c0737d3af6:预览文案泛化/catch 锚点注释精确化/ORACLES 更正回填/OWNERSHIP 补行;P3-5(单参 discoverSubmissions legacy 入口)保留并记录理由。见 REVIEW_R2.md

## 已在 master 修复、本轨仅核验不重复修(建议随本 PR 关闭或单独关闭)
#1558、#1598、#1599、#1602、#1593、#1610、#1611、#1612、#1613、#1607 主站点、#1652-2、#1653(全部实测 master 已修,明细 ISSUE_TRIAGE.md)。**本 PR 直接修复:#1652-1、#1600、#1607(残余 5 站点)+ R19-02 CT-1 教学半区 merge-loss**

## 跨轨风险与移交(CROSS_TRACK_FINDINGS.md)
#1536→04 轨(widgets);#1601 双核→maintainer 决策;src/recipes 两处 #1607 同类;CT-1 剩余 app/shell/help 路径→03/04;grader passingScore 默认 60×小分制观察项。

## Known Fail / Not Executed
- KNOWN_BASELINE_FAIL:无(本轨改动闭包内零已知红)
- NOT_EXECUTED:CMake 域 sweep 全量(68 目标;锁被兄弟轨占用,7 个直接受影响目标已手链双绿覆盖)、test_teaching_foundation_e2e、Windows/macOS lane、线上 CI(#1491,按章程不等)、TSan(未标)
