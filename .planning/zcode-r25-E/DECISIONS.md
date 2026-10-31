# DECISIONS — R25-E

| # | 决策 | 理由 |
|---|---|---|
| D1 | #1695-17c 主缺陷（GUI 线程发现扫描）不重修 | r24-c #1721 已修；本轨只收残留：close-time join + 双扫描（CT-C6 明确移交 teaching 后端 owner=本轨） |
| D2 | discovery 取消语义 = 返回空列表（非部分列表） | 部分发现会静默评子集还显得完整（#1466 vanish 纪律）；"未发现"是唯一诚实答案 |
| D3 | seam 字段（preDiscovered/Skips）不进 batchConfigDigest | transport≠grading authority；否则每次重启 checkpoint 失效，破坏 #1694 resume 契约。测试断言 checkpoint 跨 seam 采用 |
| D4 | 取消发生在发现阶段 → dock 不发布空成绩册，typed 快速失败 | 空成绩册会被误读为"全班没交作业"；0 行已评分=无可保留 |
| D5 | dock 内部发现也接收 cancelFlag | 析构/取消的等待从"整目录扫描"降到"单条目"，任何路径一致 |
| D6 | run_lab.cmd 只改一行 + 契约测试（脚本↔规则↔overlay 三方一致） | r23-D CT-D1 的移交方案；不改 bundle（overlay 自校准已对）；grade_all.cmd 参数化不属缺陷 |
| D7 | 空白作答拒绝 = record+reload 双边界（#1678 纪律） | 只堵 record 边界可被手改文件绕过；reload 边界只拒"无 numeric 载荷且全空白"，numeric 空文本合法保留 |
| D8 | #1653 不重修 | 51eb7d1a6 已修（三 TU include checks_numeric.h），FIXED_OPEN |
| D9 | grading_evidence/run_integrity 块只从 row/report 自身字段取值 | 不捏造：缺证据保持可见的空值；Oracle #4 fail-closed 精神 |
| D10 | 学生 foundry E2E 不重写 | test_r23_d_ct6_lab_flow + test_teaching_foundation_e2e 已覆盖生成→流水线→评分→验证；本轨补脚本契约(E2)+教师全环(E7) |
| D11 | 构建复用 r25-A 的 pybind11-src（FETCHCONTENT_SOURCE_DIR） | 共享宿主网络受限；协议允许复用兼容依赖缓存；不共用 build 目录 |
