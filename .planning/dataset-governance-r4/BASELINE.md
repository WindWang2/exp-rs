# BASELINE — Track 13: Dataset Governance Hardening R4（实测 2026-09-27, Linux）

## 1. 实测基线

- `origin/master` SHA：`15e5c66b543ef3874cb929f17529ef456bd6c059`（与提示词写作时一致，未前进；本地 master `eac910dff9` 落后 159 提交，本轨道全部基于 origin/master）。
- 工作分支：`hardening/r4-dataset-governance`，隔离 worktree `/home/kevin/projects/rs-studio/exp-rs-dataset-governance-r4`（仓库根 master 只读未动）。
- 开放 issue：`gh issue list --state open` → **0**（实测确认）。

## 2. 在途 PR 盘点与 file-overlap map（实测 7 个）

| PR | 分支 | 文件数 | 与本域白名单重叠 |
|---|---|---|---|
| #1334 | fix/review-p1-security | 30 | 无 |
| #1335 | fix/review-p0-build-restore | 19 | `tests/test_data_platform_surface.cpp` |
| #1336 | hardening/closure-ui-runtime-r4 | 13 | `tests/test_data_manager_panel.cpp` |
| #1337 | hardening/closure-workflow-contracts-r4 | 25 | `tests/test_data_parse_integrity_12.cpp` |
| #1338 | hardening/closure-io-processing-r4 | 29 | `src/data/artifact_store.cpp`、`src/data/governance/governance_store.cpp`、`src/data/governance/import_center.cpp`、`src/data/workspace_catalog.cpp`、`tests/test_data_parse_integrity_12.cpp` |
| #1339 | hardening/r4-i18n-help | 38 | 无 |
| #1340 | hardening/r4-operator-oracles | 24 | 无 |

**结论**：
- **`src/dataset/` 零占用** —— 本轨道核心域安全。
- `src/data/` 四个文件被 #1338 占据；WP-F 触碰 src/data 时避开这四个文件，若引用一致性修复必须动它们，PR 描述写明"合并 #1338 后需 rebase"。
- 测试面 `test_data_parse_integrity_12.cpp` 被 #1337/#1338 双占；本轨道新增用例落在新文件，不与之重叠。

## 3. 评审材料

- `PROJECT_REVIEW_DOSSIER_5.0.md`、`AUDIT_DOSSIER_ISSUES_747_760.md`、`PR_TRIAGE_REPORT_2026-09-16.md`、`docs/PARALLEL_TRACKS_10.md` 在 master 已不存在（实测 `ls` 全部 No such file）→ 按提示词预案以 PR 描述为准。#1335 描述提及"剩余 142 个既有失败测试"属其分支语境；#1336 的 EOL 教训（canonical_bytes：NUL-in-first-8000-bytes 判二进制 + git 二进制安全归一）直接作为本轨道指纹设计的先例（先例代码在 PR #1336 diff 中，未合入 master，master `grep canonical_bytes` 零命中——本轨道在 dataset 侧自行落地等价思路）。

## 4. 计数锚定复核（Linux 实测，find 口径）

| 锚点 | 写作值 | 实测 | 备注 |
|---|---|---|---|
| `src/dataset` 文件数 | 47 | **47**（含 CMakeLists；非构建文件 46） | 口径钉死：含 CMakeLists.txt |
| `src/data` 文件数 | 84 | **84**（非构建文件 83） | 子目录 governance/ internal/ providers/ 实存 |
| dataset 头文件 | 22 | **22**（名单逐一对上） | 全名单见提示词，与本测一致 |
| canonical 真源 | 1 处 | `src/data/execution_fingerprint.cpp:184 canonicalizeJsonRfc8785`（定义）；`src/dataset/dataset_fingerprint.cpp:19`（dataset 侧唯一消费点） | feature_table.cpp:181 也消费 |
| SplitEngine | split.h:173 | **split.h:173** | 一致 |
| SplitMix64 / Pcg32 | deterministic_random.h | **:29 / :52** | 一致 |
| seedFor 消费点 | 枚举入表 | split.cpp:833、split.cpp:1087（purpose="split"）；patch_generator.cpp:287（"patch.random"）、:304（"patch.stratified"）；定义 deterministic_random.cpp:22 | 共 4 消费点 |
| dataset 侧既有测试 | 11 | test_dataset_core、test_mlops9_split、test_split_leakage、test_stratified_split、test_spatial_block_leakage、test_sample_label_annotation、test_sample_fixtures、test_dataset_quality_scale、test_d19_dataset_foundry、test_dataset_e2e_examples、test_execution_fingerprint | 另有邻域：test_edit_sample_tools、test_io_subdataset_inventory（不在本轨道核心） |
| #1336 canonical_bytes 先例 | 1 | master 零命中（PR 未合并）；先例在 #1336 diff（`scripts/gen_lab_packs.py` + C++ twin） | 本轨道落地等价思路于 dataset 域 |

## 5. 指纹链现状快照（设计输入，Linux 检出）

- `git config core.autocrlf`：**未设置**（Linux 主仓库与 worktree 均无）；Windows CRLF 检出差异无法在本机直接复现，扰动矩阵以**构造字节级输入对**（LF/CRLF/BOM/键序/尾随字节）等价覆盖。
- 指纹入口契约：`makeDatasetFingerprint(const QJsonObject&)` —— 输入是**已解析 JSON**，剥 `fingerprint` 字段后 `canonicalizeJsonRfc8785` → SHA-256。字节级扰动（EOL/BOM/尾随字节）只能从**解析路径**渗入：`QJsonDocument::fromJson`（foundry_service.cpp:68、dataset_store 的 textToJson）。扰动矩阵用例 = 同语义 JSON 文本的字节变体 → 平台解析入口 → 指纹相等断言。
- canonical 序列化已确认的性质（读 execution_fingerprint.cpp:117-187）：键按 QString 排序（UTF-16 码元序 = RFC 8785 JCS 顺序）；字符串经 QJsonDocument JSON 转义（locale 无关）；数字：0 归一、整数域直写、最短往返小数（'g' 精度 1..17）。**已知偏差**：'g' 格式指数带填充（如 `1e-07` vs ES6 `1e-7`）且大数走 `1.23457e+18` 形态——这是本仓 canonical 契约的既定形态（改动会翻动全部既有 pins，超出"修缺陷"边界），已知答案向量以**本仓 canonical 形态 + 独立 SHA-256 实现（python hashlib）**为真值，不以 RFC 8785 参考实现为真值。
- 平台解析行为待 WP-A 实测钉死：UTF-8 BOM / CRLF / 尾随 NUL / 尾随换行 下 `QJsonDocument::fromJson` 的接受/拒绝形态。
- 切分指纹：`splitManifestFingerprint`（split.cpp:750-756）剥 fingerprint 后走同一 canonical 入口——WP-A 的口径统一收益自动传导到切分与版本 diff。
- 随机：hashSeed = FNV-1a(UTF-8) + SplitMix64 finalizer；seedFor = SplitMix64(hashSeed(purpose) ^ rootSeed)（deterministic_random.cpp:7-28）。"split.x 不扰动 split"性质源于哈希派生，无层级逻辑——用例直接钉。

## 6. 本轨道边界声明

白名单：`src/dataset/`、`src/data/`（引用一致性相关行）、`tests/test_dataset*`、`tests/test_data*`、`.planning/dataset-governance-r4/`。
**最小必要例外**：`.gitignore` 追加一行 `!.planning/dataset-governance-r4/`（仓库既有约定：`.planning/*` 默认 ignore + 逐轨道白名单例外，见 .gitignore:142-150；不加则规划工件无法入 PR）。账本 `.goal-loop-ledger.md` 已被 .gitignore:338 覆盖，不入库。

## 7. 构建环境

- 全新构建目录 `build-gcc15`：`cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -DENABLE_TESTS=ON -DENABLE_LOCAL_BUILD_SHORTCUTS=ON -DCMAKE_{C,CXX}_COMPILER=/usr/bin/{gcc,g++}-15`（gcc-15 栈；默认 gcc 16.2 未验证，按既往轨道教训直接用 gcc-15；**不走 raise-compiler-stack.sh**——既往教训：该脚本吞编译失败留陈旧 .o）。
- 资源红线：`ninja -j2`、`CTEST_PARALLEL_LEVEL=1`；RSS>70%（62Gi 总量 → 阈值 ≈43Gi）降 -j1。
- 测试框架：Catch2（`catch2/catch_test_macros.hpp`），注册宏 `sicnu_add_test`（tests/CMakeLists.txt）。
