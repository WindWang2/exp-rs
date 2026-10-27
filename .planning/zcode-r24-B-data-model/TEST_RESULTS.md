# R24-B TEST_RESULTS（Windows 本机实跑；Linux/macOS = NOT_EXECUTED）

- Worktree `agent/r24_b-20261027T022545Z`，HEAD 见 FINAL_REPORT；构建：MSVC 19.38 RelWithDebInfo + Ninja -j2 + vcpkg 依赖 + 系统 Qt 6.8.0。
- 运行方式：`ctest --test-dir build-r24_b -R <pattern> -j1 --output-on-failure`（offscreen，chcp 65001，PATH 含 Qt/qca/keychain bin）。

## 正式 GREEN ×2（全部 8 套件，两轮均 18/18 通过，0 失败）

| 套件 | 用例数 | R1 | R2 |
|---|---|---|---|
| r24_b_uri_percent | 1 | 100% | 100% |
| r24_b_leakage_bucket | 2 | 100% | 100% |
| r24_b_lineage_reattach | 4 | 100% | 100% |
| r24_b_dataset_txn_gate | 2 | 100% | 100% |
| r24_b_detection_nodata | 2 | 100% | 100% |
| r24_b_eo_preflight | 2 | 100% | 100% |
| r24_b_publish_fence | 3 | 100% | 100% |
| r24_b_metamorphic | 2 | 100% | 100% |

## RED 演示（外科式回退单个修复→重建→失败→恢复→通过）

| 修复 | RED 操作 | RED 结果 | GREEN 恢复 |
|---|---|---|---|
| P1-B dataset 事务门 | dataset_store{,_samples}.cpp 回退基座 5fe642ec6 | **0/2 失败**（racer 抓到 both-succeed/stale-commit） | 2/2 通过 |
| P2a bucketOf 护栏 | 仅回退 bucketOf 体（无检查转换） | **1/2 失败**（unbucketed==0，极端样本入垃圾桶） | 2/2 通过 |
| GAP-G2 lineage 导出 | reproduction_bundle.cpp 回退基座（3 键切片） | **2/4 失败**（无重挂） | 4/4 通过 |
| P1-A 检测 NoData 哨兵 | detection_tile_engine.cpp 回退基座 | **1 用例失败**（fed mean=-4899.5 ≠ 100） | 2 用例通过 |
| P1 尾批丢弃 | 禁用两处循环后无条件 flush | **2/819 断言失败**（x64..95 条带全 0 类） | 819/819 通过 |

## 邻域回归（既有测试）

- `test_dataset_core`「version lifecycle」用例：exe 直跑 26 断言全过（经 ctest 的 UTF-8 测试名 mojibake 为宿主代码页问题，用 chcp 65001 后仍受 -R 匹配串转码影响；直跑为准）。
- `test_experiment_evaluation` 重写契约用例（re-attaches）随套件语义变更，由 test_r24_b_lineage_reattach 全面钉住（4 用例含 legacy 兼容/敌意/数据集侧）。
- 其余大仓套件 NOT_EXECUTED（构建面覆盖：全部 8 个 r24_b 目标 + 其依赖闭包 3515 目标编译通过，BUILD_EXIT=0）。

## 环境备注（如实）

- ctest 首轮"通过"曾被 DISCOVERY_FAILED 占位遮蔽（重目标 DLL 缺失所致）——已通过补 PATH（qca/keychain bin）+ 重建发现修复，最终两轮为真实执行（detection 8 断言/publish 21 断言/metamorphic 819 断言级别）。
- 直接运行输出留档：`test_r24_b_detection_nodata`: All tests passed (8 assertions in 2 test cases)；`test_r24_b_publish_fence`: (21 in 3)；`test_r24_b_metamorphic`: (819 in 2)；`test_r24_b_eo_preflight`: (8 in 2)。
