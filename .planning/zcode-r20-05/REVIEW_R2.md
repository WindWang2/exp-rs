# R20-05 REVIEW_R2 — 独立评审第二轮(fresh subagent #2,只读,109 工具调用)

范围:`origin/master...HEAD` 全 8 提交(含 R1 处置 7b1d6a2778)+ R1 记录复核。
评审员自行从本 worktree 构建受影响 TU 并独立运行四个套件(非采信作者声明)。

**裁决:READY-WITH-FIXES(零 P0/P1/P2,P3×5,已全部处置)**

## 独立验证(评审员自述)
- discovery 记账:编译探针实证 head skips=4 每路径恰一次、无双重记账;base(master TU)skips=0 → R1-P0 的 RED/GREEN 声明属实;flat pass 的 `isFile` 先于 hidden 检查正是防重记机制;NFC 归因一致;`skipCount==1` 在 Unix 点前缀布局成立;**test_r15_E_batch_refusal_accounting 9 case/62 断言实跑全绿**
- 恢复忠实性:与 r15-E 原版(8eded4a682)diff 仅注释差异
- rubric 委托:data//examples//models//tools/ 全查——**仓库不存在任何 sicnu.grader.rubric/1 真实文档**(扁平方言仅测试词表),无"引擎拒绝合法存量 rubric"回归;两消费者按严重度处理错误码漂移;**test_teaching_admin_rubric_weights_r16_c_t06 10 case/19 断言实跑全绿**
- gridEvidence lambda:引用捕获+左值传参,多次 fail()/最终 Pass 无 move 后使用;逐轴连续性条件对 3-原点格点数学精确(含 e=3w+1 单像素空档案例);GdalGridProbe(≤0)→nullopt 已处理
- **test_r18_e_completion_ledger 4 case/36 断言(含 nestbomb)实跑全绿**;5 个改动 TU 全部编译干净
- **test_r15_E_teaching_stats_honesty 6 case/33 断言实跑全绿**(覆盖空课程/105/-7 钳制/重复 lab 去重/stale-hint 派生/indeterminate-fail 分流)
- 下游不变量:checkpoint 按 (studentId,path) 键控,skips 不入 checkpoint;report.total 保持 items-only;buildClassSummary 有 total>0 守卫;无计数不变量破坏

## P3 发现与处置(本提交)
| # | 位置 | 发现 | 处置 |
|---|---|---|---|
| 1 | teaching_admin_dock.cpp:412-434 | 预览日志仍称"reserved-name file(s)…rename",而恢复后多数 skip 是 hidden/nested,补救动作不同 | 文案泛化为"entr(y/ies)…see the report for each refusal's reason and remedy"(修复了替换引入的括号笔误并复验语法) |
| 2 | ORACLES.md:25 | "discovery 半区 master 已绿"陈旧误判(R1-P0 已推翻)未被同步;套件复选框未回填 | 更正 + 回填(标注 R2 评审员独立运行结果与本轨待复跑项) |
| 3 | OWNERSHIP.md | R1 处置新增的 5 类路径未入允许写表(违反该文档自身规则) | 追加缺失行 |
| 4 | 5 个 pin+catch 站点 | 注释同时引 grader_json(Json::Exception)与实际所写 std::exception 两锚点 | ledger 注释改为精确说明(遵循邻近 lab_session_state 形式,Json::RuntimeError is-a std::exception,两者皆 fail-closed) |
| 5 | batch_assessment.cpp:312 单参 discoverSubmissions | 无调用者(与 R1 删除的死 API 同类) | 保留:该重载是头文件文档化的 legacy 便捷入口("pass nullptr for the legacy skip-silent behavior"),非本轨变更引入的死亡;移除属公共 API 清理,超出最小修复——记录于账本 |

## 结论
R1 处置属实;代码独立验证全绿;剩余为文档/文案级,已随本提交闭环。
