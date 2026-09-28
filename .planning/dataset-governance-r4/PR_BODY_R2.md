# fix(tests): master configure broken by leftover union-merge marker + stale e2e red surfaced by readiness sentinels (Track 13 R4 post-merge verification)

## 摘要（诚实口径）

本 PR 是 Track 13（dataset governance R4，PR #1350，已合并）的**合并后验证轮**，
包含两个真实缺陷修复与完整验证证据；无新增功能、无新方向：

1. **P0：master 测试树 configure 全坏**。tests/CMakeLists.txt 残留一个孤立的
   `=======`（引入者 7bb6398c05，"Merge ... into tmp-merge-1345" 的 union 合并
   保留了两侧但遗留分隔行），自该提交起任何 master 检出的 cmake configure
   即失败：`Parse error ... got unquoted argument with text "======="`。
   修复 = 删除该行（1a4289a108）。全树扫描确认无第二处（先例 #1294）。
2. **域内陈旧红灯：Example D**。test_dataset_e2e_examples（master 长期
   NOT_BUILT 19 之一）的 Example D 只在内存构造 LabelSchema、从未注册进
   store——#1350 的注记入库门槛正确拒绝未注册 schema，但该二进制不构建
   导致红灯整段时期不可见。修复 = `saveLabelSchema` 先注册（490415c1b0），
   二进制 4/4 用例 58 断言全绿。

## 实测基线与领先/落后

- 分支基点：origin/master `a726d17a62`（#1354 合并点）。
- 领先 origin/master：**2 个提交**（1a4289a108 P0 + 490415c1b0 Example D），
  外加 planning 文档提交。
- 白名单域漂移评估（42a8d0fe46..a726d17a62）：`src/dataset`、`src/data`
  **0 文件变动**；`tests/` 仅他轨道的 tests/test_data_manager_reap.cpp。

## 与在途 PR 的文件重叠说明

- 唯一在途 PR #1365（persistence-consistency）也触碰 tests/CMakeLists.txt
  （注册其新目标）与 tests/test_d19_benchmark.cpp。本 PR 对
  tests/CMakeLists.txt 的改动是删除 1 行孤立标记；若产生文本冲突，按
  "两侧都保留、删分隔行" 语义解决即可。

## 验证证据（本地，未等待线上 CI）

构建：build-gcc15-r2（全新目录，/usr/bin/g++-15，Debug，ENABLE_TESTS=ON，
ninja -j2；gcc-15 对 qgis_gui 重模板 TU 出现 3 次 ICE 段错误，经 ninja 级
重试与 ulimit -s 262144 跨过——建议上游把 cmake/raise-compiler-stack.sh
接为编译 launcher）。机器全程 ≥28GiB/62GiB 可用，未触及 -j1 降级线。

Oracle（`ctest -R "dataset|fingerprint|split|leak|ontology" -j1`）：

- 全正则面（补齐全部命中的 108 个二进制后）：**179 用例，178 通过，
  连续两轮完全一致**（logs/ctest_r2_final_run{1,2}.log）。
- Round-1 可比子集（14 个本域二进制）：50/50 通过 ×2（logs/ctest_r2_run{1,2}.log）。
- 唯一失败为**白名单外、master 既有**：test_scientific_state_gdal
  "bare dataset projects honest unknowns and the FSM default"——测试把
  fixture TIFF 写入 CMAKE_SOURCE_DIR/build-rs14-passport 却从不创建该目录，
  任何全新树必失败；已实验证明目录存在即通过（11 断言）。RS14 科学状态域
  所有，本轨道不改其文件（与第一轮对 rs:temporal_sar_fusion sidecar 的
  处置一致），建议该域所有者补 `fs::create_directories` 或注册侧
  `file(MAKE_DIRECTORY)`。

## 独立真值复核（仓外，不依赖本仓构建）

- 指纹 KAT：python hashlib 对 test_dataset_fingerprint_determinism.cpp 钉死
  的 canonical 字节重算 SHA-256 → 逐位一致。
- 生成器/种子 KAT：test_split_reproducibility.cpp 的 22 个钉死向量
  （SplitMix64 seed 0 / 0xDEADBEEF、Pcg32 seed 0 / 42、hashSeed、seedFor ×3）
  仅凭算法文本在 python 重算 → 22/22 一致。第一轮"独立权威"声明属实。

## Round 1 交付物计数刷新（均在 master）

扰动矩阵 12 行（≥12）；确定性 KAT 用例在树；API_AUDIT 22 行处置非空；
DECISIONS 7 条规则（≥5）；Round-1 提交 19 个（≥16）。

## 用户可感知的行为变化

- 修复 1 前：检出 master 后 cmake configure 测试树必然失败（所有开发者/CI）。
- 修复 2 前：Example D 一旦被构建即红（NOT_BUILT 哨兵机制使其可见性刚刚恢复）。
- 其余无行为变化。

## 未解决项清单（继承 + 新增）

- 上游 src/agent PUBLIC-link 修复（第一轮 E5）——完成后可撤 WHOLE_ARCHIVE
  临时停gap（第一轮评审 P2-9 申报项）。
- test_scientific_state_gdal 的暂存目录缺陷（本次定性，白名单外，建议 RS14
  域修复）。
- gcc-15 ICE 的系统性缓解：把 cmake/raise-compiler-stack.sh 接为
  CMAKE_CXX_COMPILER_LAUNCHER（本次以重试+栈上限临时跨过）。
- #1365 若与本 PR 在 tests/CMakeLists.txt 文本冲突，按上述语义解决。
