# R22-01 science TEST_RESULTS（交付时快照）

## 环境
- worktree: agent/r22-science-20261025T133250Z @ master 911e0b17 + 4 commits
- build: build-r22-science（Debug, Unix Makefiles, -j4 上限, 共享锁内串行）
- 运行封装: /tmp/r22s-run.sh（QT_QPA_PLATFORM=offscreen；PROJ_LIB/LD_LIBRARY_PATH
  同 R21-A：/tmp/r21a-libs + seismic prefix + /tmp/r21a-deb）
- 主机内存全程 >39Gi 可用；无 OOM/swap kill。

## 核心科学回归（RED→GREEN→GREEN×2 全记录见 RED_GREEN.md）
| 二进制 | 断言 | 用例 | 跑1 | 跑2 |
|---|---|---|---|---|
| test_r22_science_numerics | 351,467 | 7 | PASS | PASS |
| test_r22_science_random | 201,069 | 6 | PASS | PASS |
| test_r22_science_resampler | 53 | 5 | PASS | PASS |
| test_primitives5（基线） | 171 | 17 | PASS | PASS |
| test_math_utils（基线） | 77 | 6 | PASS | PASS |
| test_resampler（基线，含语义更新例） | 434 | 13 | PASS | PASS |
| test_feature_matcher | 58 | 4 | PASS | PASS（跑2 覆盖） |
| test_endmember_extraction | 72 | 4 | PASS | PASS（跑2 覆盖） |
| test_classifier_kmeans | 2,202 | 4 | PASS | PASS |

## 受影响族（单跑，纳入关键族第二轮抽查）
| 二进制 | 断言 | 用例 | 结果 |
|---|---|---|---|
| test_classification_pipeline | 240 | 26 | PASS |
| test_obia_segmentation（含 multires 12 例子集 833） | 850 | 14 | PASS |
| test_d14_geometric_registration_e2e | 8,351 | 3 | PASS |
| test_rs_operators（含 rs:proximity） | 9,081 | 82 | PASS |

## RED 证据（修复前同二进制）
- numerics: 7 例 4 FAIL（含 issue 反例 2.449≠2.828、对拍首错、stddev 64≠0.25）
- resampler: 5 例 4 FAIL（边缘 NoData 类）
- random: 黄金自检（开发期缺陷修正记录见 RED_GREEN.md）

## 未执行 / 环境限制
- 未跑全仓 ctest（容量；按任务书"最窄→受影响族→跨域集成"策略执行上表）。
  其余未列测试族不受本轨改动影响（改动文件闭包分析 + R1 评审交叉核对）。
- 线上 CI 不参与完成门（任务书 §4）；PR 附不等待 CI 说明。
- Windows/macOS STL 实际运行未执行（单主机）；跨平台主张基于标准条款
  （mt19937 引擎算法被 [rand.eng.mersenne] 唯一规定；替换掉的
  shuffle/distribution 算法标准未规定）+ 黄金序列锚点，非多 STL 实测。
  黄金序列测试正是为了让任何偏离在任一平台立刻 RED。
