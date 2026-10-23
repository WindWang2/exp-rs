# R20-05 FINAL_REPORT — 遥感本科实验教学、自动评测、证据与测试可信度收敛

## 交付边界
- **PR #1668**(OPEN,非草稿,未自动合并,不等 CI):https://github.com/WindWang2/exp-rs/pull/1668
- 分支 `agent/r20-05-teaching-verification-20261022T190641Z`,head `4187088239`,BASE = origin/master `28e1891390`(推送前 fetch 复核:master 零漂移、零开放 PR、无重复工作)
- 13 提交 / 43 文件(+2064/−192),全部位于本轨白名单(OWNERSHIP.md 逐文件对账,终检 grep 空)

## 修复清单(全部 RED→GREEN×2)
1. **merge c8f5e0805422 教学侧丢失恢复**(R19-02 CT-1 本轨半区;git 考古 + 双 worktree 三方核对实锤):
   course_home 进度钳制+派生;反馈投影 passed 缺失→indeterminate;fail-closed 批量报告加载器 + 控制台两调用点;discovery hidden/nested 记账(R1-P0 补全);恢复两个被删除的测试文件(15 case)
2. **#1652-1**:rubric 校验委托 `GradingRubric::fromJson`(ONE rubric truth);仓库零存量 rubric 文档 → 无兼容回归(R2 全查)
3. **#1600**:网格分数采样盖章;census 按逐轴连续性(R1-P2 细长栅格修正);`hasSampling/sampledPoints` 首次有生产者
4. **#1607 残余**:教学面 5 个 jsoncpp 解析站点 pin 256 + typed catch(300 层嵌套从 SIGABRT 134 → 类型化拒绝)

## 证据等级
- **RED**:6 个独立驱动(g++ 直链生产 TU)对 base `28e1891390` 全红——stats 三断言 / rubric 引擎合法被拒+扁平被收 / sampling NOT-STAMPED×2 形 / nestbomb SIGABRT(134) / discovery skips=0;输出归档 red-drivers/RUN_LOG.md
- **GREEN×2(本人环境)**:7 个直接受影响套件连续两轮 exit 0,合计 **3768 断言 ×2 零失败**(admin_core 3442/37 含 fake grader CLI 全流程)
- **交叉印证**:R2 评审员独立编译另跑绿 4 套件(62/9、33/6、36/4、19/10)
- **构建边界(如实披露)**:共享构建锁被四条并行轨占用,CMake 域 sweep 全量与 foundation_e2e 未执行;7 个受影响目标以手工链接 TU 闭包覆盖;测试二进制无 ctest,直跑 Catch2

## 独立评审
- R1(fresh subagent #1,89 调用):REQUEST-CHANGES(P0 discovery 记账未恢复 + P2 census 计数法 + 6 P3)→ 全处置于 7b1d6a2778,REVIEW_R1.md
- R2(fresh subagent #2,109 调用,自行编译+运行 4 套件):READY-WITH-FIXES(零 P0-P2,5 P3)→ 处置于 c0737d3af6(1 项保留并记录理由),REVIEW_R2.md

## Issue 处置汇总
- 本 PR 直接修复:**#1652-1、#1600、#1607(残余 5 站点)** + R19-02 CT-1 教学半区(非 issue)
- 实测 master 已修(仅核验,建议关闭):#1558、#1598、#1599、#1602、#1593、#1610-#1613、#1607 主站点、#1652-2、#1653(ISSUE_TRIAGE.md 逐条证据)
- 跨轨移交(CROSS_TRACK_FINDINGS.md):#1536→04 轨;#1601→maintainer;src/recipes 两处;CT-1 剩余 app/shell/help 路径→03/04;grader passingScore 默认值观察项

## Remaining
- CMake 域 sweep 全量(锁可用时补跑);foundation_e2e;Windows/macOS;TSan
- E12 域内更大范围弱断言扫测(本轮验证 #1558 两处已修,未及全量)
- token 记账:本会话主代理+2 评审子代理实际消耗远低于 3 亿锚定;构建等待不产生 token;完成判定按交付物而非 token 数
