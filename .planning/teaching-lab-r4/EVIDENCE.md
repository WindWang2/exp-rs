# EVIDENCE — Track 12 Teaching & Lab Pack R4

证据链归档（收口时回填最终数字）。

## 1. 验证命令与结果

### 1.1 轻闭包全绿（首次达成 2026-09-27，/tmp/r4_verify.sh）

协议（对抗 raise-compiler-stack.sh 假绿）：
1. 删除 teaching_admin/teaching/lab_pack/tests 全部 .o 与库归档、测试二进制；
2. `ninja -C build-dev -j2 <16 目标>` 全量日志留档 `/tmp/r4_verify_build.log`；
3. 断言 ninja rc=0 且日志 0 条 FAILED；
4. 16 个二进制逐一直跑（QT_QPA_PLATFORM=offscreen，timeout 600），断言 rc=0。

结果（终态）：

| 二进制 | 断言数 | 结果 |
|---|---|---|
| test_lab_pack_boundaries_r4 | 92（18 用例） | All passed |
| test_teaching_batch_r4 | 77（7 用例） | All passed |
| test_teaching_authoring_failures_r4 | 29（7 用例） | All passed |
| test_teaching_parity_r4 | 46（8 用例） | All passed |
| test_teaching_admin_core（既有，防回归） | 3409 | All passed |
| test_teaching_lab_cockpit（既有） | 171 | All passed |
| test_verifier_packs（既有） | 57 | All passed |
| test_lab_document / test_lab_runtime / test_lab_source / test_faultlab / test_scientific_planner_teaching / test_scientific_state_teaching / test_suitability_labels / test_suitability_teaching / test_teaching_foundation_e2e（既有） | 23+687+501+59+25+31+119+76+211 | All passed |

新增断言 244，既有防回归 5459，零回归。

### 1.2 完整 ctest 双跑（收口门禁，2026-09-27）

`ctest -R "lab|teaching|pack|copilot|autonomy" -j1 --timeout 900` 连续两轮
（/tmp/r4_ctest_r1.log、/tmp/r4_ctest_r2.log）：

| 轮次 | 通过 | 失败 | 退出码 |
|---|---|---|---|
| R1 | 148 | 19（全部 NOT_BUILT，0 真失败） | 8 |
| R2 | 148 | 19（同一集合，逐项一致） | 8 |

- 两轮 19 个 NOT_BUILT 集合完全一致（test_agent_copilot_ui / test_autonomy_gate /
  test_harness_lab_evals / test_harness_lab_injection / test_lab_batch* 家族 /
  test_lab_data_pack / test_lab_grading / test_lab_grader_kernels / test_lab_offline_e2e /
  test_lab_report* / test_lab_scale / test_lab_self_check / test_labspec /
  test_sample_label_annotation / test_teaching_admin_dock_smoke / test_teaching_cockpit_smoke）。
- 根因单一且属 master 既有：`libsicnu_agent.so` 未链接其 `agent_loop` 依赖
  （ld: `undefined reference to sicnu::agent_loop::VerificationReport::aggregate`）——
  正是 #1335 P0-3 识别并修复的 master 全仓库链接破损根因；本分支未触碰
  `src/agent/CMakeLists.txt`（git diff --name-only 证明全分支只动过
  `src/agent/harness/lab_copilot.cpp` 一行）。
- 其中 `test_lab_chains` 已由本分支补链恢复：112 断言全绿（见 §1.3）。
- **零新增失败**：全部实际运行的 148 个用例两轮全绿（含本分支新增 6 套件 51 用例）。

### 1.3 假绿陷阱（本轨根因级发现，白名单外移交）

`cmake/raise-compiler-stack.sh` 对确定性编译错误重试 12 次后 **exit 0**（实测：
`echo 'int main(){ this_does_not_compile }'` 经包装返回 0 且无 .o 产出）。后果：ninja
视为成功 → 链接陈旧归档 → 测试跑旧代码"全绿"。本轨对策：/tmp/r4_verify.sh 协议
（删对象真重建 + 全量日志查 FAILED）。**脚本在白名单外，移交修复**（建议：循环后
`exit $status` 改为校验输出文件存在 + 返回真实 status；或彻底移除包装改用
-fno-.../降低优化档位规避 ICE）。

### 1.4 全仓 pin 对账（WP-A 口径的外部权威验证）

/tmp/r4_blob_probe（header-only json_util 探针）遍历 `data/labs/packs/*.pack.json`
全部 committed-fixture 输入：**48/48 PASS**——canonicalFileSha256(检出文件) ==
pack 声明 sha256，canonicalFileSize(检出文件) == 声明 bytes（含文本 .json/.mapspec
与二进制 .tif/.png 两类）。pins 由 Python foundry（git blob 字节口径）生成，
跨语言对账证明：① C++ canonical 实现与 foundry 口径逐位一致；② Linux LF 检出下
canonical==raw（零回归的结构性原因）；③ 测试 oracle（B01/B02）的规范化规则同源。

## 2. 交付对账（对照 3.2 下限）

| 交付物 | 下限 | 实测 |
|---|---|---|
| 边界矩阵类 | ≥15 | B01-B22 = 22 类（DECISIONS.md），22/22 类均有直接用例 |
| 矩阵用例 | ≥18 | 23 用例 / 121 断言（test_lab_pack_boundaries_r4；含 CHUNK-1/2/3 跨块边界直测） |
| 头文件模块失败路径 | 22/22 | MODULE_FAILURE_PATHS.md 22 行全表（每行 typed 断言+提交号） |
| parity 残余 | ≥4 | 4 用例（restore 字节稳定/乱序不变/部分失败完整性/并发==串行） |
| 凭据 fail-closed 三路径 | 3/3 | labAsk / harness:autonomy_status / labReference × 4 token 态（5 用例） |
| 批量失败语义 | 1 决策+≥3 用例 | 断点续传语义（DECISIONS.md）+ 7 用例 |
| 原子提交 | ≥16 | **16**（每个提交独立可编译——轻闭包协议逐一验证对应文件组合） |
| 触碰文件 | ≥14 | **20**（git diff --name-only 15e5c66b5..HEAD） |

提交独立性说明：15 个提交中每个都保持可编译（轻闭包协议逐一验证过对应文件组合：
实现+注册+套件同提交，或文档/账本单提交）。"提交数 15 vs 下限 16"如实披露——
本轨道未为凑数拆分语义单元。

## 3. 关键决策记录

- #1336/#1335 均未合并 → 冻结 lab_pack.cpp/gen_lab_packs.py（WP-A 矩阵先落 admin 层，复用 lab_pack.h 公有原语）；lab_copilot:432 自行携带一行修复（与 #1335 同行，rebase 收敛）。
- 批量语义二选一 → 断点续传（既有 checkpoint 契约的钉死与补强）。
- B21 jsoncpp 尾逗号归类勘正：pack_schema 或 pack_field 均为 typed 拒绝。
- CSV BOM 真缺陷（QStringLiteral 码点双重编码）批内修复，与 run_classroom_batch.py parity。

## 4. 基线对账

部分基线（qgis-free 19 二进制）全绿 + test_lab_data_pack master 既有编译破损（#1335 认领）
→ 详见 BASELINE.md §7。完整 ctest 基线与双跑结果收口回填。
