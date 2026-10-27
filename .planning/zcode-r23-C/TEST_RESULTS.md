# TEST_RESULTS — r23-C（全部命令于 worktree 根/build-r23-C 执行，QT_QPA_PLATFORM=offscreen）

## 环境
- g++ 16.2.1 / cmake 4.4.4 / Debug / -j2 + 全局 flock（与 r23-A/r23-B 并行轨互斥实测生效）
- 基线 origin/master=ad98a7a704；HEAD 见 FINAL_REPORT

## 基线健康（修复前，seam 已入但行为中性）
| 套件 | 结果 |
|---|---|
| `./tests/test_task_center` | All passed — 533 assertions / 34 cases (exit 0) |
| `./tests/test_r22_agent_job_logs` | All passed — 24031 assertions / 4 cases (exit 0) |

## WP-1（C05：TaskCenter 累积/增量绝对索引空间，R2-F3/F6）
- **RED**（消费端未修复时，`./tests/test_r23_c_task_center_log_spaces`）：
  - Case A (R2-F6)：`REQUIRE(countLine(...,"engine internal tail")==1)` → 展开值 **0 == 1**（引擎内部追加行未投递）
  - Case B (R2-F3)：`REQUIRE(countLine(...,bLine(forwarded))==1)` → 展开值 **0 == 1**（应投递切片首行丢失）
  - 汇总：test cases 2 | 0 passed | 2 failed（assertions 12 | 10 passed | 2 failed）
  - 首次 RED 运行另暴露测试自身两处缺陷（按引用捕获 future 的 UAF、dtor 双 set_value），已作为独立测试修复提交（57f3f0e511 / f878807475），修复后 RED 稳定如上
- **GREEN1**（修复 fb1b2e8489 后）：All passed — 21 assertions / 2 cases
- **GREEN2**（复跑）：All passed — 21 assertions / 2 cases
- 回归：`./tests/test_task_center` 533/34 全绿 ×2；`./tests/test_r22_agent_job_logs` 24031/4 全绿 ×2

## WP-2（C10：成绩册 JSON/CSV 成对原子发布，#1695-17e）
- **RED**（生产文件临时还原为 origin/master 版本后构建运行，`./test_r23_c_teaching_pair_publish`）：
  - Case 1：失败发布后 `REQUIRE(jsonNow == stale)` 展开显示 jsonNow 为**新报告内容**（JSON 已被部分发布，CSV 槽位为目录）→ 正是部分发布缺陷
  - 汇总：test cases 3 | 2 passed | 1 failed（assertions 25 | 24 passed | 1 failed）
  - 流程事故记录：RED 用的 `git checkout origin/master -- <两文件>` 把回退写入 index，被随后的测试提交 f878807475 误带入；发现后从 57df09e502 逐字恢复（commit 41aa25c7f3）并重验
- **GREEN1**：All passed — 25 assertions / 3 cases
- **GREEN2**：All passed — 25 assertions / 3 cases
- 回归（同域批量套件，修复后单跑）：`test_teaching_batch_r4` 77/7 全绿；`test_r18_e_batch_checkpoint` 33/3 全绿；`test_r15_E_batch_refusal_accounting` 62/9 全绿

## WP-3（C09：%n 复数形式，#1695-17b）
- 无运行时断言载体（源串语言学修复）；`teaching_admin_dock.cpp` TU `-fsyntax-only` exit 0 CLEAN；resources/translations/sicnu_zh_CN.ts 无该串条目（改源串无翻译回归面）
- 验收：源码 grep `entr%2` 0 命中（提交后核验）

## ctest 口径
- `ctest --test-dir build-r23-C -R 'r23_c' --output-on-failure --parallel 1` 于 push 前终门禁执行（结果记入 FINAL_REPORT）

## 已知基线红区分
- 无：本轨定向套件全部绿色；未运行全量 ctest（三轨并行下全量编译不可行，按任务契约以定向闭包+同域 smoke 为准）
