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

### 1.2 完整 ctest 双跑（收口门禁）

（占位——qgis 尾巴构建完成后回填：`ctest -R "lab|teaching|pack|copilot|autonomy" -j1` 连续两轮，全新构建目录）

### 1.3 假绿陷阱（本轨根因级发现，白名单外移交）

`cmake/raise-compiler-stack.sh` 对确定性编译错误重试 12 次后 **exit 0**（实测：
`echo 'int main(){ this_does_not_compile }'` 经包装返回 0 且无 .o 产出）。后果：ninja
视为成功 → 链接陈旧归档 → 测试跑旧代码"全绿"。本轨对策：/tmp/r4_verify.sh 协议
（删对象真重建 + 全量日志查 FAILED）。**脚本在白名单外，移交修复**（建议：循环后
`exit $status` 改为校验输出文件存在 + 返回真实 status；或彻底移除包装改用
-fno-.../降低优化档位规避 ICE）。

## 2. 交付对账（对照 3.2 下限）

| 交付物 | 下限 | 实测 |
|---|---|---|
| 边界矩阵类 | ≥15 | B01-B22 = 22 类（DECISIONS.md） |
| 矩阵用例 | ≥18 | 18 用例 / 92 断言（test_lab_pack_boundaries_r4） |
| 头文件模块失败路径 | 22/22 | MODULE_FAILURE_PATHS.md 22 行全表（每行 typed 断言+提交号） |
| parity 残余 | ≥4 | 4 用例（restore 字节稳定/乱序不变/部分失败完整性/并发==串行） |
| 凭据 fail-closed 三路径 | 3/3 | labAsk / harness:autonomy_status / labReference × 4 token 态（5 用例） |
| 批量失败语义 | 1 决策+≥3 用例 | 断点续传语义（DECISIONS.md）+ 7 用例 |
| 原子提交 | ≥16 | （收口回填） |
| 触碰文件 | ≥14 | （收口回填，git diff --name-count） |

## 3. 关键决策记录

- #1336/#1335 均未合并 → 冻结 lab_pack.cpp/gen_lab_packs.py（WP-A 矩阵先落 admin 层，复用 lab_pack.h 公有原语）；lab_copilot:432 自行携带一行修复（与 #1335 同行，rebase 收敛）。
- 批量语义二选一 → 断点续传（既有 checkpoint 契约的钉死与补强）。
- B21 jsoncpp 尾逗号归类勘正：pack_schema 或 pack_field 均为 typed 拒绝。
- CSV BOM 真缺陷（QStringLiteral 码点双重编码）批内修复，与 run_classroom_batch.py parity。

## 4. 基线对账

部分基线（qgis-free 19 二进制）全绿 + test_lab_data_pack master 既有编译破损（#1335 认领）
→ 详见 BASELINE.md §7。完整 ctest 基线与双跑结果收口回填。
