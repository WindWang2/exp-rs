# R21-B FINAL_REPORT — 地理空间数据/模型推理运行时/实验可复现

**分支**: `agent/r21-b-data-model-runtime-20261023T185101Z` @ `de2dbd121b`（24 commits）
**Base**: `911e0b17a3533ca0d79c1b68b000d6052c4c3c61`（执行时点 origin/master）
**PR**: 见 PR_BODY.md

## 交付总览
| 类别 | 数量 | 明细 |
|---|---|---|
| 生产修复 | 6 项 | A1 双 AssetId · A4 版本戳 write-once+fail-closed · A5 孤儿 metric 补偿 · A6 TOCTOU 误码 · B5 mask_threshold role 门控 · #1647-1 Python API 注册 |
| 新测试 | 8 个文件/10 用例 | 全部有 RED 实证（6 个真 RED）+ 保护性 4 个 |
| 独立评审 | 2 轮 | R1 REQUEST-CHANGES（10 项闭环含 P0）→ R2 REQUEST-CHANGES（5 项 P0/P1/P2 闭环 + 4 项 P3 记录） |
| 复验 | R20-02 F1–F6 四族 + 19 域族 sweep | 全绿 |

## Oracle 完成态
- O-B1 ✅：test_model_runtime（110,440 断言/26）、model_catalog_v2（436/24）、workspace_services（212/17）、fault_injection（162/9）两遍绿
- O-B2 ✅：test_r21_b_end_to_end 导入→数据集→推理→run/lineage→bundle 重放 + 零半成品失败腿（4142 断言，pristine 即过=保护性）
- O-B3 ✅：80 轮双连接对抗（40×2 lane）稳定（3 次实测 0.28-0.30s）
- O-B4 ✅：PERF_BASELINE 同条件实测；GPU/TSan NOT_EXECUTED 声明
- O-B5 ✅：dataset_version_migration（33/4）、model_library_manifests（237/6）等兼容族全绿
- O-G1 ✅：126 issues 全量分诊（ISSUE_TRIAGE.md）；0 open PR 盘点
- O-G2 ✅：6 项修复 RED 实锤（输出留档 RED_GREEN.md）+ GREEN×2
- O-G3 ✅：PERF_BASELINE.md
- O-G4 ✅：R1+R2 独立 reviewer（fresh 上下文、只读）
- O-G5 ✅：TEST_RESULTS.md 未执行项逐项归因
- O-G6 ✅：PR diff 28 文件 100% 白名单；无 merge commit（无回滚面）
- O-G7 ✅：无 TODO 假实现；A5 死代码、B5 死变量经审查后删除

## 环境断裂与修复（跨轨影响）
vendored libgdal 的 wrapper2_* 符号供给方（libarmadillo.so.15）从 vendor prefix 消失 + Qt6Core 6.12 需 PCRE2_10.47（vendored 为 10.46）→ 所有新链接失败。采用 r19-02 link-shim + `--allow-shlib-undefined` + 系统共享 zlib/zstd/expat/sqlite3 配方（CROSS_TRACK_FINDINGS X1，A/C 轨同样受影响）。

## 遗留（如实声明）
- GPU/TSan/Windows/macOS lane：无环境 NOT_EXECUTED
- A5 崩溃窗口（metrics 与 run 安装之间进程崩溃）：收窄未闭合（需跨两表单事务）
- B5 derived-output lane（output.format=labels/mask）仍无 role 门控：既有契约缺口（output.format 与 head role 无关联校验），记录未扩范围
- A1 后既有 Missing 资产按旧拼法的 findByPath 查询：行为变化已记录
- 100k logical extent 吞吐 mock：未新建（既有 workspace_stress/model_runtime_stress 覆盖）

## Issue 关联建议（已修未关，可核验）
#1541 #1542 #1543 #1545 #1546 #1547 #1548 #1553 #1554 #1555 #1556 #1575 #1577 #1578 #1579 #1580 #1581 #1582 #1583 #1584 #1585 #1586 #1587 #1603 #1604 #1605 #1608 #1609 #1635 #1637 #1642 #1647(1+2)
