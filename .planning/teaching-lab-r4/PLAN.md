# PLAN — Track 12 Teaching & Lab Pack R4

执行顺序按"#1336/#1335 冻结面"编排：先 teaching/teaching_admin 侧（B/C/D/E/F），#1336 合并后 rebase 再做 lab_pack 侧 WP-A 延伸。

## 新增测试载体（全部在白名单命名模式内）

| 文件 | WP | 内容 |
|---|---|---|
| `tests/test_teaching_credential_paths_r4.cpp` | D | 三路径（labAsk 教师面 / harness:autonomy_status 会话层 / harness:execute_plan 角色降级）× 凭据四态（正确/错误/未设/空）fail-closed 一致性；admin_errors 错误码合同 |
| `tests/test_teaching_parity_r4.cpp` | C | transcript parity ≥4（乱序事件/并发提交/恢复会话转录连续/部分失败回滚完整性）+ 会话族 3 模块失败路径 |
| `tests/test_teaching_batch_r4.cpp` | B | runBatchAssessment 失败语义（空批/全失败/同学生重复发现/检查点损坏降级/中途失败持久化）+ class_summary 失败路径 |
| `tests/test_lab_pack_boundaries_r4.cpp` | A | 边界矩阵 ≥15 类 ≥18 用例（EOL/编码/资产/清单完整性），入口 validatePackDocument/inventoryPacks（#1336 合并后延伸 PackVerifier 直测） |
| `tests/test_teaching_cockpit_authority_r4.cpp` | E | registry 漂移注入（拒绝而非回退硬编码）、空/缺两态、5 模块失败路径 |
| `tests/test_teaching_authoring_failures_r4.cpp` | F | authoring 族 5 模块失败路径 + fixture 引用不存在资产复用 WP-A 入口 |

## 提交序列（每个原子可编译）

1. planning 工件（BASELINE/PLAN/DECISIONS + 账本）
2. WP-D：`lab_copilot.cpp:432` `return diff;`→`return diff == 0;`（扩展点修复 1 处，记账）
3. WP-D：三路径测试目标 + 注册
4. WP-C：parity 测试目标 + 注册
5. WP-B：batch 语义测试目标 + 注册
6. WP-A：`json_util.h` canonical-bytes sha256 单入口 + `data_pack_manager.cpp:226/258` 接入
7. WP-A：边界矩阵测试（EOL/编码类）
8. WP-A：边界矩阵测试（资产/清单类）
9. WP-E：cockpit 权威漂移注入 + 失败路径
10. WP-F：authoring 失败路径 + fixture 一致性
11. WP-G：MODULE_FAILURE_PATHS 22/22 收口 + 防回归注册
12+. 评审修复 / 归档 / 文档段落（令牌轮换运维）

## WP-A admin 层修复要点（#1336 未合并期间的落点）

`data_pack_manager.cpp:226` `sha256OfFile`（原始字节）与 `:258-259` `fi.size()`（磁盘字节）都拿 git 规范化前的字节去对 committed-fixture 的 pins——Windows CRLF 检出下必假阳。修法：`json_util.h` 增 `canonicalFileSha256(path)` + `canonicalFileSizeOnDisk` 语义（镜像 #1336 权威规格：前 8000 字节含 NUL→二进制原样；否则 CRLF→LF、孤立 CR 保留；分块流读），`data_pack_manager` 两处换用。#1336 合并后 rebase：评估把规范化原语上移 `lab_pack.h` 暴露、json_util 委托，消除镜像。

## 验证纪律

- 每提交：`ninja -C build-dev -j2 <target>` + 目标 ctest；关键验证双跑。
- 收口：全新构建目录双跑 `ctest -R "lab|teaching|pack|copilot|autonomy" -j1`。
- RSS>70% 降 -j1（记录）。
