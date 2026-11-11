# REVIEW_R2 — R29-T02（独立 reviewer 新上下文；绑定 130990a396..88498f0a8f，rebase 等价性由 patch-id 验证）

## R1 修复核验：14/14 全部确认进入提交代码

（reviewer 逐项给出 file:line 证据，见其报告；REVIEW_R1 处置表与实际代码一一对应。）

## R2 发现与处置

| # | 严重度 | 发现 | 处置 |
|---|---|---|---|
| 1 | **P1（阻塞）** | R1-3 修复自身引入 UAF：body 已开始执行时 worker abandon → runOperatorJob 帧销毁而 host 线程仍在 op->run() 中 | **已修**：`started` 原子标志（body 前置位）；abandon 仅在 `cancel && !started`；已 started 的 body 必须等协作完成。复检确认现实路径关闭 + 内存序充分；理论三窗口 TOCTOU 由 grace 后 `started` 复查加固关闭 |
| 2 | P2 | warp 中间名只含 recipe digest：输入内容变化 + 多输入中途失败 → 旧 VRT 引用部分替换的中间文件（混合代际） | **已修**：中间名加每输入 (size, mtime-secs) token；变化输入寻址新名，旧代完整存活至清扫。残留（秒粒度同秒同尺寸替换+后续失败才复现）= staging 名可接受 |
| 3 | P3 | warp 分支 int cast 缺 #633 上界 guard（>INT_MIN wrap 风险） | **已修**：kMaxWarpDimension=1e9 守卫 + typed GeoError |
| 4 | P3 | 链③ broken fixture 丢 layout 名 → 未真正瞄准既有产物路径 | **已修**：`{"kind":"map_spec","layout_name":"chain3-map"}`（produce 由 layout_name 派生产物文件名，失败 run 命中同一文件名） |
| 5 | P3 | H-E 无法检测跨工作区伪造命中 | **已修**：断言 already_published==false + tiles_computed>0 |
| 6 | P3 | ctime stat 失败静默回退与注释矛盾；cp -p 无自动化回归 | **已修**：stat 失败 fail-closed；新增 H-D2（同尺寸+setFileTime 恢复 mtime+新内容 → 重算） |
| 7 | P3 | waitSlice 在 GUI 线程处理用户输入事件（调用方自认为阻塞） | **已修**：`ExcludeUserInputEvents`（host lane 的 queued 调用不受影响） |
| 8 | P3(process) | rebase 的 master commit 不得夹带 | **确认**：88ad45bf48 即 origin/master tip，PR diff = master..HEAD 恰为 5 个本轨 commit（reviewer 以 patch-id 验证字节等价） |

## 复检结论（reviewer 原文）

- R1 修复 14/14 确认；R2 修复：#1 FIXED-WITH-ISSUE（残留为理论级+已加固关闭）、#2-#8 FIXED-CORRECT。
- **R2 re-review: PASS**。
- 合并前跟进：提交工作树（已执行）+ 可选 started 复查加固（已采纳）。

## 最终验证（R2 修复后，绑定最终 HEAD）

- test_r29_t02_published_claim 88 断言✅（含 H-D2 cp-p 回归）
- test_r29_t02_virtual_warp 144✅ / test_r29_t02_chains 100✅
- 回归（r27_t02_job_engine 3 cases / r28_t02_task_lifecycle_batch / agent_ops_task_center_executor / chunk_resume_11）：28/28✅ + job_engine 20✅ + lifecycle 21✅
- 加固后重跑：published_claim 88✅ / chains 100✅ / job_engine 20✅ / lifecycle 21✅
