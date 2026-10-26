# r23-B PR_BODY（骨架——完成后以实测数据替换所有 TODO）

## BASE / HEAD
- BASE: ad98a7a7041b9d6952f5d5fa4ac5cc46ac181dea（执行时点 origin/master）
- HEAD: TODO（push 时 git rev-parse HEAD）
- 分支：agent/r23-b-data-model-runtime-20261026T164130Z

## 范围（B 白名单内）
| Slice | 内容 | 文件 |
|---|---|---|
| W1 | #1576/#1577 provider TU 合并回归恢复 + stub-compile 编译门（ctest test_r23_b_provider_stub_compile） | openvino_provider.cpp、tensorrt_provider.cpp、tests/test_r23_b_provider_stub_compile.sh、tests/fixtures/provider_stubs/** |
| W2 | derived output.format × head role/confidence 前置门（r21-B B5 残留） | tile_inference_engine.cpp、tests/test_r23_b_derived_output_roles.cpp |
| W3 | ExperimentStore::upsertRunWithMetric 单事务（r21-B A5 崩溃窗口闭合）；导入器补偿腿退役 | experiment_store.{h,cpp}、reproduction_bundle_import.cpp、tests/test_r23_b_run_metric_transaction.cpp |
| W4 | probeRemote 切换受控传输（#1637 sub-4：重定向跳过私网检查盲区） | remote/probe_remote.cpp、process_network_policy.h（注释） |

tests/CMakeLists.txt：三个 append-only 注册块（与 r23-C 轨 union 合并协议）。

## 测试与证据
- RED/GREEN ×2 明细：.planning/zcode-r23-B/RED_GREEN.md
- 套件结果（exit code 级）：.planning/zcode-r23-B/TEST_RESULTS.md
- 性能：.planning/zcode-r23-B/PERF_BASELINE.md（stub gate 3.8s；其余 TODO）

## 双轮独立评审
- R1：TODO（REVIEW_R1.md 摘要 + findings 闭环状态）
- R2：TODO（REVIEW_R2.md 摘要 + findings 闭环状态）

## Issue 状态
- 本轨实修：#1576、#1577（合并回归恢复）、#1637 sub-4（传输统一）、r21-B RISK A5/B5 残留闭合
- 分诊结论：B 域其余 open issue 全部 FIXED_OPEN（证据 .planning/zcode-r23-B/ISSUE_TRIAGE.md + triage_*.md），建议维护者核验后关闭；本轨不擅自批量关闭

## 风险与遗留
- 见 RISK_REGISTER.md；W4 判别用例的离线不可构造性见 DECISIONS.md #6

## 重现命令
```
cmake -S . -B build-r23-B -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=ON \
  -DSICNU_VENDOR_GDAL=OFF -DSICNU_WITH_ONNX_RUNTIME=OFF -DSICNU_BUILD_OTB=OFF \
  -DSICNU_EMBED_PYTHON=OFF -DSICNU_TOLERANT_TEST_DISCOVERY=ON
flock "$HOME/.cache/exp-rs-build.lock" cmake --build build-r23-B --parallel 2 \
  --target test_r23_b_provider_stub_compile_script_check test_r23_b_derived_output_roles \
            test_r23_b_run_metric_transaction test_model_runtime test_experiment_consistency_r4
ctest --test-dir build-r23-B --output-on-failure --parallel 1 \
  -R 'r23_b|test_model_runtime|test_experiment_consistency_r4'
```
（目标名以 tests/CMakeLists.txt 注册为准，push 前核对。）
