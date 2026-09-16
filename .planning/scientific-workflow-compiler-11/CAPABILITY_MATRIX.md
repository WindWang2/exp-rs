# CAPABILITY_MATRIX — before/after

| 能力 | before(master@a5b11b7f) | after(本 track) | 状态 |
|---|---|---|---|
| 单景时间事实 | acquisition_time 字符串（band_facts.cpp:162） | + cadence/regularity/coverage 规范化事实（workflow_facts） | planned |
| bounded grounding probe | 仅全量 spatial:understand | + fact-scoped 预算探针 | planned |
| 跨节点时间/数值域/波段/输出 identity 检查 | temporal 仅 demand 级 | + 4 检查族 | planned |
| repair prepared decision | refusal 文本 | + 可执行 wiring+params decision 文档 + 确定性排序 | planned |
| 执行 provenance 投影 | metadata{plan_fingerprint,...} | + metadata.compiler 规范块 + compile sidecar | planned |
| 失败回溯 | diagnose_run 重复环守卫 | + fact/contract 级 zh-CN explain | planned |
| compiler eval cases | 无专属 case 族 | + temporal/mixed/refusal 族 | planned |
| bridge compiler drift 守护 | no_drift(bridge 级) | + compiler surface drift/budget node 测试 | planned |
