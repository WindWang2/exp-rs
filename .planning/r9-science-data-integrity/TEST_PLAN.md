# TEST_PLAN — r9-science-data-integrity

Oracle 原则：每个修复先写失败复现（red），最小修复后定向套件绿灯（green）；关键 gate 跑两次记录退出码/通过数；无法本地验证的维度记 NOT_VERIFIED（不捏造）。

## 定向测试映射（worktree build-dev，QT_QPA_PLATFORM=offscreen，-j1）

| 轮 | 修复 | 复现测试（新增/扩展） | 定向 CTest regex | 既有回归 |
|---|---|---|---|---|
| R1 | W6 removeSample prepare-error 捕获 | test_dataset_core.cpp 扩展：破坏 schema/authorizer 使 prepare 失败，断言诊断含真实 sqlite 错误而非 "not an error" | `test_dataset_core` | test_sample_label_annotation |
| R2 | W5 VerifyFailed 后 assetChanged | DataManager relocate fault-injection（沿用 SICNU_FAULT_POINT 模式若已有；否则最小注入点）→ 断言 assetChanged 仍发 + 诊断分级 | `test_data_manager*`(实名待查) / `test_virtual_raster_create` | 相关 data 套件 |
| R3 | W1 POSIX 冒号路径 | test_radiometric_state.cpp：冒号 POSIX 风格路径不判 URI（Windows 上以路径形态单测判定函数；POSIX 行为标 NOT_VERIFIED-on-Linux） | `test_radiometric_state` | test_radiometric_vocabulary_contract |
| R4 | W2 kStates sigma0/beta0 | 模型 manifest 校验单测（model_catalog 侧测试实名待查，或经 test_classifier_foundation5） | 实名待查 | — |
| R5 | W3 localhost localPath | test_datasource_resolver.cpp：file://localhost → /tmp/a.tif；platform-aware percent-decode 断言 | `test_datasource_resolver` | — |
| R6 | W4 temporal 通用参数规范化 + W7 fileExists | temporal identity 单测（既有套件实名待查）+ rs_json_params fileExists 最小单测（tests 实名待查） | 实名待查 | — |
| R7 | N1 query_planner 截断信号 | 既有 fabric planner 测试扩展（test_io_catalog_query / test_io_fabric_*） | `test_io_catalog_query|test_io_fabric` | — |
| R8 | N2 suitability canonical 摘要 | suitability 摘要 digest == canonicalizeJsonRfc8785 结果（套件实名待查） | 实名待查 | — |
| R9 | N3 stepFailed 尾巴 | facets/splits/workspace_catalog 扫描在 CORRUPT/IOERR 下 typed 失败（SICNU_FAULT_POINT 或损坏 DB 夹具） | `test_dataset_core` 等 | — |

## 全量门（提交前）

1. `cmake --build build-dev --parallel 2` 全量成功（×2 增量幂等）
2. 受影响 CTest 家族两次绿灯
3. `git diff --check` 干净；无构建产物/大文件/秘钥入提交
4. 最新 master merge 重放后重复 1-2

## 环境限制（预计 NOT_VERIFIED 项）

- POSIX/Linux 与 macOS 行为（冒号路径、file://localhost 解码差异）本机不可直接运行 → 以平台感知断言 + 代码证明 + 标注 NOT_VERIFIED(Linux lane)
- ASan/UBSan：MSVC lane 默认无；如需高风险内存项验证另配 sanitizer preset（时间盒内可选）
