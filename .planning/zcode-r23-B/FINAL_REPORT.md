# R23-B FINAL_REPORT — 地理空间数据、模型推理运行时与实验可复现闭环

**BASE**: `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`（执行起点 origin/master）；执行中 master 前移，已并入 **`5fe642ec63`**（merge commit `1e1f909aa3`，tests/CMakeLists.txt 与 .gitignore 冲突按 union 协议解决，兄弟注册块 8/8 end-marker、22/22 add_test 完整性核验通过）
**HEAD**: 见 `git rev-parse HEAD`（push 时点）；分支 `agent/r23-b-data-model-runtime-20261026T164130Z`；worktree `/home/kevin/projects/rs-studio/exp-rs-r23-B-20261026T164130Z`（保留，不清理）

## 工作包终态
| WP | 状态 | 交付 |
|---|---|---|
| W1 #1576/#1577 provider TU 合并回归 | **DONE** | 恢复 9b42bc93f7 丢失的 selectDevice 三件套 + 删除残留 m_artifactPath + mutable mutex + TRT artifactPath；新增 stub-compile 编译门（ctest，3.5-4.7s）+ 变异反证；RED=master 3+1 编译错 |
| W2 derived output.format 头契约门（B5 残留） | **DONE** | 共享谓词 ensureDerivedModeMatchesHeadContract；单模型引擎 + ensemble 折叠双接缝（R2 P2-2/N1 闭环）；RED=exit 42（6 断言失败于 master 行为）→ GREEN×2 27/3；变异反证两轮 |
| W3 run+metric 单事务（A5 残留） | **DONE** | upsertRunWithMetric 单事务 + writeMetricInTxnLocked 复用提取；导入器补偿腿退役；fork 崩溃窗口 A/B + 触发器原子性 + 冲突/改名拒绝；GREEN×2 37/3；r4 A5 用例经新路径绿 |
| W4 probeRemote 受控传输（#1637 sub-4） | **DONE** | CPLHTTPFetch→httpFetchStatus（per-hop guard+DNS pinning）；probe 契约族 25/6 + offline 34/10 绿；判别用例离线不可构造性如实声明（DECISIONS #6） |
| B01-B11 其余 | NOT_STARTED（按证据裁剪） | Issue 池 B 域 144 条全量分诊：除上述外全部 FIXED_OPEN/OUT_OF_SCOPE（ISSUE_TRIAGE.md + triage_*.md 逐条 file:line 证据） |

## 提交清单（origin/master 视角）
ca1413a4ef（W1）→ 09f5f67e75（W2）→ 2252536bb2（W3）→ ef154e3ae8（W4）→ ac0931f9bf（docs）→ 679feb773e（R1 闭环）→ d0e25e5099（R2 闭环）→ fc21d0d242（R2-N1）→ 1e1f909aa3（merge master）

## 测试（全部本机实跑，exit code 级；构建 build-r23-B Release/系统依赖）
- 双跑 GREEN：r23_b 三套件（27/3、37/3、gate）+ test_model_runtime 110440/26 + test_experiment_consistency_r4 298/11 + test_r21_b_end_to_end 4142/2 + test_io_remote_range 25/6 + test_offline_mode 34/10
- 单跑+抽查：test_dataset_core 512/19、test_r22_data_security_consistency 94/9、test_io_vector_contract 100339/14（#1491 本机判定：CI 失败不复现 → 线上环境性归因）
- merge 后（master 5fe642ec63）全族复跑：上表全部 exit 0（含 test_model_ensemble 127/16 含新增用例）
- NOT_EXECUTED：线上 CI（按任务书）、GPU/TRT/OV 真机（无 SDK；W1 gate 为替代编译证据）、TSan、Windows/macOS

## 双轮独立评审
- **R1**：APPROVE，0 P0-P2；4 P3（2 修 1 记录 1 澄清误读）→ 679feb773e 闭环；评审者独立复跑全族 + 复现 W2 RED 变异
- **R2**：APPROVE-WITH-FIXES（40/40 双连接并发探针全 typed conflict、零 busy 退化）；P2×2 + P3×4 → d0e25e5099 闭环；R2 还独立发现 master 补偿腿的并发赢家删除缺陷（HEAD 单事务天然消除）
- **R2 确认轮**：原 6 项全 CLOSED，但发现修复引入的 N1（ensemble 门无 mode 守卫 → 默认 Probability 请求被拒）→ fc21d0d242 修复 + 回归钉（embedding 头 + 默认请求必须跑通）+ 全族复绿

## Issue 状态
- 实修：#1576、#1577（合并回归恢复 + 常态编译门）、#1637 sub-4、r21-B RISK A5/B5 残留
- FIXED_OPEN 待维护者核验关闭：#1541 #1545-#1548 #1553 #1554 #1575 #1577-#1587 #1603-#1605 #1635 #1637(1-3) 等（证据在 ISSUE_TRIAGE.md）
- 未动：#1558（他轨域）、#1467（src/core 域）、#1491（线上环境性）

## 风险与恢复
- RISK_REGISTER R1-R16：R10/R14/R15/R16 记录不修（含理由）；R13 OV 覆盖缝已注释如实化
- 恢复：worktree/branch 保留；构建目录 build-r23-B 可增量复验；所有 RED/GREEN 命令在 RED_GREEN.md / TEST_RESULTS.md
- 与并行 PR 关系：#1719（r24_b）与本轨在 tile 引擎/dataset/experiment 域可能交叠，其合并时按 union 注册块协议复核（本轨已核验 8/8 兄弟块完整）

## 诚实声明
- 3×10^8 token 为预算级别声明，非实际消耗；本轮实际为单会话长循环（含 ~10h 构建锁排队）
- W4 无离线判别用例（公网首跳不可构造）；W3 崩溃窗口用例为 fork 复现导入器时序（非生产进程内注入）；均已在 DECISIONS/RISK 记录
- BLOCKED：无
