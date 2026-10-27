# r23-B: 地理空间数据、模型推理运行时与实验可复现闭环

## BASE / HEAD
- BASE: `ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea`；执行中并入最新 master `5fe642ec63`（merge `1e1f909aa3`，共享文件 union 协议，兄弟注册块 8/8 完整性核验）
- HEAD: 本 PR 分支头（见 PR commits）
- 分支: `agent/r23-b-data-model-runtime-20261026T164130Z`

## 交付（4 个生产 slice + 3 组新测试，全部 B 白名单内）
| Slice | 内容 | 决定性证据 |
|---|---|---|
| **W1 #1576/#1577** | union 合并 9988aa15a9 丢弃的 OpenVINO selectDevice 定义、残留未声明 `m_artifactPath`、`health() const` 锁非 mutable mutex；TRT `artifactPath()` 返回未声明成员。两个 option 门控 TU（默认 OFF）在 master 完全无法编译且无人发现 | RED：master TU stub-compile 3+1 个编译错；GREEN×2：0 错；**新增 ctest 编译门 `test_r23_b_provider_stub_compile`**（stub SDK 头 -fsyntax-only，~4s）+ 变异反证（回喂 master TU 必 FAIL） |
| **W2（r21-B B5 残留）** | derived output.format（labels/mask/confidence）折叠 head 0 类平面，但从未校验 head 契约——embedding/detection/uncertainty 头或 distance/logit 语义会被折叠成伪造产品。共享谓词 `ensureDerivedModeMatchesHeadContract` 同时接入单模型引擎与 ensemble 折叠（R2 发现的旁路）两处 seam | RED：exit 42（6 断言落 "failed to open input raster" 判别器）；GREEN×2 27/3；变异反证两轮；22 个 models/ 清单扫描零误伤；ensemble 用例 + N1 回归钉（embedding 头 + 默认 Probability 请求必须跑通） |
| **W3（r21-B A5 残留）** | 导入器 saveMetricRecord→upsertRun 两调用 + 失败补偿：崩溃窗口仍留孤儿 metric 行。新增 `ExperimentStore::upsertRunWithMetric` 单事务（BEGIN IMMEDIATE，两行同生共死），补偿腿退役 | fork 崩溃窗口 A/B（子进程按 HEAD 时序死亡→孤儿可观测）；注入 run 写失败→两表皆空；冲突/跨 run 拒绝零变更；GREEN×2 37/3；既有 r4 A5 触发器用例与 r21 e2e（4142/2）经新路径全绿 |
| **W4 #1637 sub-4** | probeRemote 首跳私网检查不覆盖 CPLHTTPFetch 内部自动重定向。切换到受控传输 httpFetchStatus（per-hop guard + DNS pinning 自动生效） | probe 契约族 25/6 + offline 34/10 绿；离线判别用例不可构造性如实声明（DECISIONS #6） |

## 测试（全部本机实跑 exit=0；详见 .planning/zcode-r23-B/TEST_RESULTS.md）
`ctest -R r23_b` 7/7；test_model_runtime **110440/26**；test_model_ensemble 127/16；test_experiment_consistency_r4 298/11；test_r21_b_end_to_end 4142/2；test_dataset_core 512/19；test_r22_data_security_consistency 94/9；test_io_vector_contract 100339/14；test_io_remote_range 25/6；test_offline_mode 34/10 —— merge 最新 master 后全族复跑仍全绿。NOT_EXECUTED：线上 CI（按任务书不等待）、GPU/TRT/OV 真机（无 SDK，W1 gate 为替代编译证据）、TSan、Windows/macOS。

## 双轮独立评审
- **R1**：APPROVE（0 P0-P2；4 P3 → 2 修 1 记录 1 澄清）；评审者独立复现 RED 变异与全族
- **R2**：APPROVE-WITH-FIXES（40/40 双连接并发探针全 typed `experiment.conflict`）；P2×2（ctest 前缀缺失、ensemble 旁路）+ P3×4 全闭环
- **R2 确认轮**：6/6 CLOSED + 发现修复引入的 N1（ensemble 门缺 mode 守卫）→ 修复 + 回归钉 + 全族复绿

## Issue 状态
- 本轨实修：#1576、#1577、#1637（sub-4）、r21-B RISK A5/B5 残留
- 144 open issues 全量分诊（.planning/zcode-r23-B/ISSUE_TRIAGE.md + triage_*.md 逐条 file:line 证据）：B 域其余全部 FIXED_OPEN，建议维护者核验后关闭；本轨不擅自批量关闭
- #1491：本机同 GDAL 3.13.3 实跑 test_io_vector_contract 全绿 → CI 失败为线上环境性，非 HEAD 源码缺陷

## 风险
RISK_REGISTER R1-R16（R10/R14/R15/R16 记录不修含理由；R13 OV 覆盖缝注释如实化）。与并行 PR #1719（r24_b）在 tile/dataset/experiment 域可能交叠，合并次序按 union 注册块协议复核。

## 重现命令
```bash
cmake -S . -B build-r23-B -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
  -DSICNU_VENDOR_GDAL=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF -DSICNU_BUILD_OTB=OFF \
  -DSICNU_EMBED_PYTHON=OFF -DSICNU_TOLERANT_TEST_DISCOVERY=ON
flock "$HOME/.cache/exp-rs-build.lock" cmake --build build-r23-B --parallel 2 --target \
  test_r23_b_derived_output_roles test_r23_b_run_metric_transaction \
  test_model_runtime test_model_ensemble test_experiment_consistency_r4 \
  test_r21_b_end_to_end test_io_remote_range test_offline_mode
ctest --test-dir build-r23-B --output-on-failure --parallel 1 -R "r23_b"
QT_QPA_PLATFORM=offscreen ./build-r23-B/tests/test_model_runtime   # 110440/26
```
（TRT/OV 门：`ctest -R test_r23_b_provider_stub_compile`，无需 SDK。）

不 auto-merge；不等待线上 CI。worktree 与分支保留供 review/恢复。
