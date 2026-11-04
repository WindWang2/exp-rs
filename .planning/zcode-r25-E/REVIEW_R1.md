# REVIEW_R1 — R25-E（独立评审人 1，全新上下文子代理，只读）

- 结论：**APPROVE-WITH-FIXES**（1×P1、3×P2、6×P3）
- 范围：git diff origin/master...HEAD 全量 + 周边生产码（batch_assessment/dock/session_state/feedback_pack/class_summary/spec_runtime）+ 规则文件逐字节比对
- 亮点确认：取消语义不变式（cancelled⇒empty）全路径核验成立；seam 确为 transport（digest 排除 + 跨路径 checkpoint 采用证明）；CT-D1 切换连贯（overlay 逐参数一致）；UTF-8 序列逐码点核验正确（自同步性排除假阳性）；dock throw 路径正确 re-arm 且不触碰 checkpoint

## 发现与处置（全部采纳）

| # | 级别 | 发现 | 处置（commit a8d38071eb） |
|---|---|---|---|
| 1 | P1 | loadBatchReportFromFile 丢弃 skippedFiles/resumed/truncated/checkpointInvalidated/cancelledEarly → 生产摘要 run_integrity 恒全零（发布 JSON 说 skipped:1、控制台说 0） | loader 补解析五成员（缺省 0=诚实"未记录"）；教师 E2E 改从 RELOADED 报告建摘要（生产路径）+ resume 轮回断言 |
| 2 | P2 | 取消发生在内部发现时，最终无条件 checkpointAll() 会把既有 checkpoint 重写为 {total:0,rows:[]}，摧毁前次运行耐久行 | cancelledDuringDiscovery 时跳过最终 checkpoint；新增三段式测试（A 完成留痕→B 取消→C 证明采用 A 的行） |
| 3 | P2 | spec 允许空白 declared choice：recordAnswer 可记录（匹配自身）但 reload 拒绝 → 会话写后不可载 | spec 验证器拒绝空白 choice；isBlankAnswerText 提升为共享 API（session_state.h 单一定义，三边界不漂移）；新增 spec 拒绝测试（ASCII+U+3000） |
| 4 | P2 | mid-walk 测试 5ms 翻转可能晚于遍历完成（伪红方向）且 flat-pass 取消检查零变异覆盖 | 1ms 翻转（安全方向）+ wideTree 加 200 个根级文件（两 pass 均有载荷）+ 确定性"翻转先于遍历"变体 |
| 5 | P3 | "bounded to ONE entry" 表述夸大（allEntries 的 readdir 不可分段） | 头文件+dock 注释改为"one directory listing plus one entry" |
| 6 | P3 | 去空洞测试的 ++gradedBeforeCancel 裸 int（2 worker 数据竞争/TSAN 红） | std::atomic<int> + fetch_add |
| 7 | P3 | 泄露守卫"回归"种植在旧成员 message，不在新 grading_evidence | 种植移至 topDeduction |
| 8 | P3 | cancelledEarly 无"空类不取消"对照（突变 items.isEmpty() 可存活） | 新增空目录对照测试（total 0 且 !cancelledEarly） |
| 9 | P3 | Unicode 空白匹配两个窄边缘（NEL U+0085、过长编码不计为空白） | 接受并在 isBlankAnswerText 文档注明（非法/争议 UTF-8，非诚实作答文本） |
| 10 | P3 | 已核验无需动作项（CT-D1/seam/取消不变式/守卫扫描面） | 记录 |

评审人 mutation 建议已吸收为测试强化（#4/#7/#8）；修复后全部相关套件复跑见 TEST_RESULTS.md。
