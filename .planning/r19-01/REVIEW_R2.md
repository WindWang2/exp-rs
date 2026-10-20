# R19-01 REVIEW_R2 — 第二轮独立评审（Reviewer 2）

- 评审者角色：独立 general-purpose 子代理（非实现者，与 R1 相互独立）
- 审查对象：e12a9afbc..HEAD 增量 5 提交（时点：40674def9/d040e9325/17b24d394/bfbd8d768/501bcaedd），重点 = 增量 + 跨轨
- 方式：叶库契约/链接符号级论证、executor 语义全注册点审计、gh 实测 #1663/#1664 diff 比对（59 次工具调用）
- 结论：**REQUEST-CHANGES**（窄范围：2×P2 + 5×P3）

## 发现与处置（493ab3e5f 闭环）
| 级别 | 位置 | 问题 | 处置 |
|---|---|---|---|
| P2 | src/sdk/CMakeLists.txt | EXPRS_OPERATOR_CONTRACT_HEADERS 漏 rs_param_validation.h——装出含符号的叶库却装不出声明；且 rs_operator.h 文档把叶库消费者指向 .so 才有的 out-of-line validateParams | **已修**：头入安装面；validateParams 改头文件内联委托叶库自由函数（单入口、处处可链）|
| P2 | src/cli/sicnu_worker_main.cpp:444 | 隔离 worker 路由（TaskCenter isolate → makeIsolatedWorkerExecutor → worker 直调 run()）绕过 JobEngine 门——进程隔离恰是 #1578 威胁模型放大器 | **已修**：worker 执行前 validateParams（该文件无开放 PR 竞争；CROSS_TRACK 补记）|
| P3 | test_chunked_fabric_e2e 注释 | 16MiB 依据成立但注释算术错（11 tiles；实际 queue (S+1)·cap·I=6 + in-hand 6 + 1 = **13**）| 已修：注释改为 13 并给出旧 10MiB 下 minimal-peak=10 不 <10 的退化证明 |
| P3 | rs_operator.h 双入口漂移缝 | funnel 调自由函数 vs 文档说 validateParams | 已修（随 P2-1 内联化收敛为单入口）|
| P3 | pybind_sicnu_operators:120 / batch_processing_dialog:913 / plugin_operator_adapter:176 | 残余未设门直连 run() 入口（他轨写集）| **记录** CROSS_TRACK_FINDINGS X1 扩充（后续票建议）|
| P3 | product_adapters.cpp:62 / sensor_profile.cpp:59 | 姊妹 readFileText 仍无界整读（manifest.safe/MTL/MTD/profiles）| **已修**：同 4MiB 分块有界模式 ×2 + S1 超限 manifest 测试钉（2/2 绿）|
| P3 | rs_param_validation 非数值 bound 跳过无钉 | — | 已修：badBound 钉（T1.2 现 7/7 绿）|

## R2 确认无问题的项（摘）
- 叶库契约守住：rs_param_validation 严格 std+jsoncpp（与 rs_operator_error.h 同预算）；无状态自由函数双副本无 #573 ODR/RTTI 增量
- 链接论证：adapter 的 mOp->schema() 虚分发不产生 out-of-line 符号需求（vtable 经 .so 内 registry 构造路径已加载）——域 85 目标 exit 0 的代码层原因
- JobExecutor 全注册点审计：app/CLI fallback 与 processing: 前缀均汇入已设门的 RsOperatorAdapter::execute；else 分支设门位置正确（唯一缺口即上述 worker P2，已修）
- 501bcaedd 非"洗绿"：Advisory 为文档化 never-gates 判决，gated 配置下不可达；F-A-13 与 Advisory 在基线 e12a9afbc 已存在——修复的是 master 漂移
- **跨轨零重叠**（实测 gh pr diff 1663/1664 --name-only 比对）；工作区 leak-probe-root/leak-test-root 为调试残骸不在提交中（本轨已清理）

## 复测（493ab3e5f 后，全绿）
- T1.1 14/14（含 3 负向钉）· T1.2 7/7（含 badBound）· T1.3 6/6 · S1 manifest 2/2
- 双阶段构建 exit 0（216/216，含 sicnu_worker 目标）
- 最终全域 sweep：进行中（结果落 DOMAIN_SWEEP_RESULTS.md）
