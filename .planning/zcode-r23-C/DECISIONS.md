# DECISIONS — r23-C

| # | 决策 | 理由 | 备选与否决原因 |
|---|---|---|---|
| D-1 | R2-F3/F6 修复放在消费端（task_center.cpp 累积分支）而非生产端（job_engine 进度回调改 delta） | R2-F3 修复建议；消费端是唯一 `logLinesOffset`/累积双形状的汇合点，一处修复覆盖所有 cumulative 生产者（进度回调、引擎内部 append、submit 追赶 snapshot）；生产端改造会改变 snapshot()/其他 listener 的可见形状，影响面大 | 生产端统一 delta：更彻底但触碰多类 listener 契约，超最小正确方案 |
| D-2 | 新增 `TaskCenter::deliverJobRecordForTests`（public 一行传给私有 onJobRecord） | engine listener 为单一替换槽、同步投递，测试无法从外部排序记录以钉住乱序下的 exactly-once 契约；仓库已有 `*ForTests` 惯例（shutdownForTests/waitUntilIdleForTests） | friend class 测试：改动面更大；多线程压力测试：不可确定性，会闪断 |
| D-3 | RED 测试用真实 TaskCenter+JobEngine 提交（真映射/真 eviction），记录注入用引擎真实形状字段（offset/droppedLogLines/logLines）构造 | 契约 RED 需确定性；记录形状与生产者逐字段一致（进度回调 cumulative、log 回调 delta、引擎内部 append） | 纯 mock：脱离生产消费路径，说服力弱 |
| D-4 | WP 集合 = {WP-1, WP-2, WP-3}（+WP-4 视预算） | 均为白名单内、证据充分 LIVE、可独立验收的小切片；任务契约禁止凑工作量 | 大而全（重写 UI/接线 temporal）：违反 YAGNI 与"不扩新产品线" |
| D-5 | 不关闭任何 open issue | 任务契约："别擅自批量关闭已有 issue"；在 PR body 报告 FIXED_OPEN 证据供人工复核 | — |
| D-6 | goal-loop 技能从官方仓库一键安装后按其 SKILL 协议手工执行（会话启动时不在技能列表） | 任务指定；安装成功（~/.zcode/skills/goal-loop） | 未安装时手工同协议 |
