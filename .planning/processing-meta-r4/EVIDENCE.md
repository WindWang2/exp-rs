# EVIDENCE — hardening/r4-processing-meta（终稿）

执行环境：worktree `/home/kevin/project/exp-rs-processing-meta-r4`；cmake `/home/kevin/toolchain/cmake-dist/bin/cmake`，ninja `/home/kevin/pwb-sdks/root/usr/bin/ninja`，全程 `-j2`；测试环境 `LD_LIBRARY_PATH=/home/kevin/pwb-sdks/root/usr/lib QT_QPA_PLATFORM=offscreen CTEST_PARALLEL_LEVEL=1`。
日志目录：`.planning/processing-meta-r4/logs/`（baseline_ctest_run1.log / final_ctest_run1.log / final_ctest_run2.log）。

## E1. 快照 gate（WP-D）——双跑绿 ✅

```
contract_inventory --source-root . --check    data/contracts/contract_graph.snap.json
contract_inventory --source-root . --census-check data/contracts/determinism_census.snap.json
```
- 再生成：`--out` → nodes 1265 / edges 527 / **findings 0**；`--census-out` → entries 195（字节未变：census 源扫描不含侧车 authored 文本）。
- 双跑：graph `--check` 两轮、census `--census-check` 两轮，**4 次全部 exit 0**（2026-09-27 实测，见会话记录与下述测试断言）。
- 本地测试断言（tests/test_snapshot_gate_r4.cpp，ctest 内双跑）：
  - `#959 live graph and census generation are byte-stable across two runs` — **Passed 385.06s / 403.22s（双跑）**
  - `#960 committed snapshots equal the fresh generation byte-for-byte` — **Passed 200.31s**（基线为红——master 快照本就陈旧，本轨道再生成后转绿）
  - `#961 synthetic snapshot drift is attributed, not just counted` — **Passed 57.22s**（扰动归因）

## E2. capability 页面（WP-E）——零漂移 ✅

`capability_knowledge_tool gen-pages .` 写出 11 页（+255 行，0 删除，全部为侧车 authored 键渲染）；随后 `gen-pages . --check` → **`gen-pages: zero diff`**。页面提交与侧车提交分离，无任何手改字节。

## E3. 完备性门禁（WP-G）——红→绿 ✅

`#194 capability authored enrichment census: applicability, teaching_use, prerequisites and limitations` — 补齐前 **Failed**（TDD 设计态红，111 缺口）→ 补齐后 **Passed ×2（0.46s / 0.49s，双跑）**。D1 原契约（summary/failure_modes/io）同文件保持绿。

## E4. 基线红绿分布（Phase 0）

`ctest -R "capabilit|contract|meta|registry|snapshot" -j1`（meta 编辑前，logs/baseline_ctest_run1.log）：
77 项，62 绿 / 15 红；15 红中 12 项 NOT_BUILT（Not Run），**真实红 3 项均为 master 既有**：
1. `Help ↔ registry: no phantom and no uncovered commands (#869 class)`（mission.task.resume / mission.task.retry 无 help 条目）——#1336/#1337 领域，commands.json 不在本轨道白名单，HELP_CONSISTENCY.md 记录处置；
2. `Contract snapshot is fresh (byte-compare against live graph)`——master 快照陈旧，#1337 领域，本轨道再生成后修复；
3. 本轨道 `#960`（与 2 同根因，测试职责证明）。
另：master 在此基线 27 个测试可执行断链 + test_capability_knowledge.cpp 缺右括号（#1335 领域），本轨道白名单内最小修复（foreach 链接块 + 1 括号，标注合并后删除/以对方版本为准）。

## E5. 终局 Oracle 双跑（logs/final_ctest_run1.log / run2.log）

`ctest -R "capabilit|contract|meta|registry|snapshot" -j1` 连续两轮：
- 两轮**失败集逐项 diff 相同**（RUN1==RUN2）：64/77 绿，13 红 = 12 NOT_BUILT（Not Run，master 即未构建）+ **1 项 master 既有 help 红（#674）**。
- 相对基线**零新增失败**，且基线真实红 3 项中 2 项（#690 快照陈旧、#960）由本轨道修复转绿。
- 补充双跑：`#194` census 门禁 ×2 绿、`#959` 字节稳定 ×2 绿、`contract_inventory --check/--census-check` ×2 绿×2 快照。

## E6. help 完备性（WP-F）

`HELP_CONSISTENCY.md`：76/76 条目静态对照 + 18 条 `command.rs.*` purpose ↔ 算子 summary 语义对照（零矛盾）。既有合成校验 `test_help_integrity_12` 在终局两轮中**通过**；唯一 help 缺口（mission.task.resume/retry，#674）为 master 既有、在途 PR 领域，按对照表"处置"列记录。

## E7. 覆盖矩阵（WP-A）

`META_COVERAGE_MATRIX.md`（gen_matrix.py 自动生成，可重跑复核）：157 注册行全归位；sparse 56/157（+gdal:polygonize=57，设计不变量维持）；**authored 四键缺口补齐后 0/0/0/0**；determinism/memory_policy 157/157 声明；gpu 显式 51、accuracy 有值 0（不臆造）。

## E8. Not Run 集合实测收口（独立评审 P2 #5）

- foreach 清单内的 test_chunk_contract_11 / test_verification_metamorphic_11 实测**均成功链接**（foreach 修复有效；早前 Not Run 是 ninja 首败截断调度的子集产物）。
- 直跑验证：`test_verification_metamorphic_11` **All tests passed（1122 assertions / 6 cases）**；`test_chunk_contract_11` 8/9 用例绿，1 失败为 master 既有断言类型不匹配（test_chunk_contract_11.cpp:338 期望 std::length_error，实现抛 int 域溢出的类型化错误；chunk/tile 代码不在本轨道 151 文件 diff 内，定义上先在，记 backlog）。
- 家族 ctest 其余 Not Run：本会话构建子集的兄弟占位目标未建（如 test_chunk_graph）+ 1 个既有断链（test_interactive_session_contract，不在 foreach 清单，#1335 领域）。

## 提交清单（origin/master..HEAD，17 个原子提交）

11 × meta(capability) 批量（batch 00-10，每批 10/10/10/10/10/10/10/10/10/11/1 算子）+ meta(knowledge) 页面 + docs(planning) + test(snapshot) + test(capability) census/括号 + meta(contracts) 快照 + test(capability) 名称小写修正（ 使 census 用例进入家族 -R 选择；ctest -R 大小写敏感，#959 同理需显式 -R 选择，两者均已显式双跑并记录）。
